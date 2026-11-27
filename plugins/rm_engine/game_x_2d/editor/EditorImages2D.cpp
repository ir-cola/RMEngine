#include "rm_engine/game_x_2d/editor/EditorImages2D.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/BakedImage.h"
#include "core/asset/ImportSettings.h"
#include "editor/app/Worker.h"
#include "platform/Image.h"

#include <algorithm>
#include <filesystem>
#include <optional>

namespace rm::game_x_2d {

namespace {

constexpr double kImageCheckSeconds = 1.0;    // 画像ファイルを見直す間隔 (ディスクで変わったか)
constexpr double kImageForgetSeconds = 10.0;  // どこも使わないままこの時間がたった画像は手放す

}  // namespace

// アセットの id で一度だけ読む画像ファイル (同じ画像をいくつの物が使っても共有する)。
struct EditorImages2D::Entry {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    Image2D view;
    bool looked = false, loaded = false, told = false;
    std::filesystem::file_time_type time{};
    double checkedAt = 0, usedAt = 0;
    // 専用のスレッドで読む (ファイルと WIC)。ビットマップだけは、届いたときに UI スレッドで作る。
    // エントリを捨てると、ワーカーとこれから届く分も捨てる。
    std::unique_ptr<editor::Worker> worker;
    bool reading = false;
    struct Arrived {
        std::optional<platform::Image> image;  // nullopt: 理由は `error` にある
        std::string error;
        uint32_t width = 0, height = 0;
        bool sharp = false;
    };
    std::optional<Arrived> arrived;
};

EditorImages2D::EditorImages2D() = default;
EditorImages2D::~EditorImages2D() = default;

void EditorImages2D::Begin(double now, uint64_t generation, std::string who) {
    now_ = now;
    who_ = std::move(who);
    if (generation != generation_) {  // GPU のデバイスが作り直された: ビットマップは必要になったら読み直す
        generation_ = generation;
        entries_.clear();
    }
    std::erase_if(entries_, [&](const auto& kv) { return now_ - kv.second->usedAt > kImageForgetSeconds; });
}

const Image2D* EditorImages2D::Get(ui::Context& c, const AssetRef& ref, editor::SoftwareHost& host) {
    if (ref.Empty()) return nullptr;
    auto& slot = entries_[ref.guid];
    if (!slot) slot = std::make_unique<Entry>();
    Entry& m = *slot;
    m.usedAt = now_;
    auto shown = [&]() -> const Image2D* { return m.bitmap ? &m.view : nullptr; };
    if (m.arrived) {
        // ワーカーで読んだ: ビットマップはここで作る (Direct2D は UI スレッドのもの)。
        const bool again = m.bitmap != nullptr;
        Entry::Arrived got = std::move(*m.arrived);
        m.arrived.reset();
        m.reading = false;
        const std::string name = PathToUtf8(host.AssetFile(ref).filename());
        m.bitmap = got.image ? platform::BitmapFromImage(c.P().DC(), *got.image) : nullptr;
        if (got.image && !m.bitmap) got.error = "Direct2D のビットマップにできませんでした";
        if (m.bitmap) {
            m.view = {m.bitmap.Get(), got.width, got.height, got.sharp};
            log::Info("{}: 画像「{}」を{}読みました ({} x {} ピクセル)", who_, name, again ? "もう一度" : "", got.width, got.height);
        } else {
            m.view = {};
            log::Warn("{}: 画像「{}」を読めないので四角で描きます: {}", who_, name, got.error);
        }
    }
    if (m.reading) c.RequestAnimation();  // 読んでいる絵が届くまで描き直し続ける
    if (m.looked && now_ - m.checkedAt < kImageCheckSeconds) return shown();
    m.looked = true;
    m.checkedAt = now_;
    const std::filesystem::path file = host.AssetFile(ref);
    const std::string name = file.empty() ? ref.guid : PathToUtf8(file.filename());
    std::error_code ec;
    // (画像そのものか、.meta の取り込みの設定)
    const auto time = file.empty() ? std::filesystem::file_time_type{} : asset::ChangedAt(file, ec);
    if (file.empty() || ec) {
        m.bitmap.Reset();
        m.view = {};
        m.loaded = false;
        if (!m.told) {
            log::Warn("{}: 画像「{}」がプロジェクトに見つかりません (消されたか、アセットの外のファイル)", who_, name);
            m.told = true;
        }
        return nullptr;
    }
    m.told = false;
    if (m.loaded && time == m.time) return shown();  // 変わっていない
    m.loaded = true;
    m.time = time;
    // ワーカーへ渡す。新しい絵が届くまで古いビットマップは残す。
    if (!m.worker) m.worker = std::make_unique<editor::Worker>();
    m.reading = true;
    Entry* entry = &m;  // (エントリはワーカーの投稿より長く生きる: 投稿はエントリと一緒に捨てる)
    m.worker->Run([file, entry](std::stop_token stop, const editor::Worker::Post& post) {
        Entry::Arrived got;
        // 画像の取り込みの設定 (.meta): くっきり と、どこまで大きくしてよいか。
        const auto imp = asset::ReadImport<asset::ImageImport>(file);
        got.sharp = imp.filter == asset::TextureFilter::Sharp;
        const uint32_t most = std::min<uint32_t>(asset::kMaxImageSize, static_cast<uint32_t>(std::max(imp.maxSize, 1)));
        IWICImagingFactory* wic = platform::ThreadImagingFactory();
        // (まずファイル自身の大きさ。ビットマップより大きい絵は縮小するが、物の大きさは変えない)
        auto whole = platform::LoadImageFile(wic, file, platform::PixelOrder::PremultipliedBgra, {}, &got.error);
        if (whole) {
            got.width = whole->width;
            got.height = whole->height;
            const bool big = whole->width > most || whole->height > most;
            if (big && !stop.stop_requested()) {
                auto fitted = platform::LoadImageFile(wic, file, platform::PixelOrder::PremultipliedBgra, {most, most}, &got.error);
                if (fitted) whole = std::move(fitted);
            }
            got.image = std::move(whole);
        }
        if (stop.stop_requested()) return;
        post([entry, got = std::move(got)]() mutable { entry->arrived = std::move(got); });
    });
    return shown();
}

}  // namespace rm::game_x_2d
