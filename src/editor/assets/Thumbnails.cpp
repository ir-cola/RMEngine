#include "editor/assets/Thumbnails.h"

#include "core/asset/ImportSettings.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/plugin/Software.h"

#include <chrono>
#include <exception>
#include <format>
#include <new>

namespace rm::editor {

namespace {

constexpr double kCheckSeconds = 1.0;    // ファイルをどれだけの間隔で見直すか
constexpr double kForgetSeconds = 60.0;  // これだけ長く出していないサムネイルは忘れる

// `pixels` のために作る大きさ: 32 / 64 / 128 / 256 のうち次の物 (パネルにはそれより大きいのは要らない)。
uint32_t Bucket(uint32_t pixels) {
    uint32_t size = 32;
    while (size < pixels && size < 256) size *= 2;
    return size;
}

}  // namespace

Thumbnails::Thumbnails(platform::Gfx& gfx, const PluginRegistry& registry, std::filesystem::path root)
    : gfx_(gfx), registry_(registry), root_(std::move(root)) {}

Thumbnails::~Thumbnails() {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
    }
}

const ThumbnailMaker* Thumbnails::MakerFor(asset::Kind kind) const {
    for (const auto& m : registry_.Contributions<ThumbnailMaker>(kThumbnailPoint)) {
        if (m.item->kind == kind && m.item->prepare && m.item->draw) return m.item;
    }
    return nullptr;
}

bool Thumbnails::Has(asset::Kind kind) const { return kind == asset::Kind::Image || MakerFor(kind) != nullptr; }

// 作れなかった理由だけを言う結果。
Thumbnails::Done Thumbnails::Failed(Job job, std::string why) {
    Done d;
    d.job = std::move(job);
    d.error = std::move(why);
    return d;
}

void Thumbnails::Work(std::stop_token stop) {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mu_);
            if (!wake_.wait(lock, stop, [&] { return !jobs_.empty(); })) return;  // 止まった
            job = std::move(jobs_.back());  // 一番新しい: パネルが今出している物
            jobs_.pop_back();
        }
        Done d;
        d.job = std::move(job);
        // ここで投げた物がスレッドの外へ出てはいけない (エディター全体が終わってしまう):
        // 読み込みでメモリが足りなくなるファイルや、例外を投げるプラグインは、ただ「作れない」にする。
        try {
            if (d.job.kind == asset::Kind::Image) {
                auto image = platform::LoadImageFile(platform::ThreadImagingFactory(), d.job.file, platform::PixelOrder::PremultipliedBgra,
                                                     {d.job.size, d.job.size}, &d.error);
                d.ok = image.has_value();
                if (image) d.image = std::move(*image);
            } else if (const ThumbnailMaker* maker = MakerFor(d.job.kind)) {
                d.prepared = maker->prepare(d.job.file, root_, d.job.size, &d.error);
                d.ok = d.prepared != nullptr;
            }
        } catch (const std::bad_alloc&) {
            d = Failed(std::move(d.job), "メモリーが足りなくなりました (壊れているか、大きすぎるファイル)");
        } catch (const std::exception& ex) {
            d = Failed(std::move(d.job), std::format("読んでいる途中で止まりました ({})", ex.what()));
        } catch (...) {
            d = Failed(std::move(d.job), "読んでいる途中で止まりました");
        }
        std::lock_guard lock(mu_);
        done_.push_back(std::move(d));
    }
}

void Thumbnails::Finish(Done& d, ui::Context& c) {
    const auto it = entries_.find(d.job.guid);
    if (it == entries_.end()) return;  // その間に忘れられた
    Entry& e = it->second;
    if (e.asked == d.job.size) e.asked = 0;
    if (d.job.time != e.time) return;  // その間にファイルが変わった: 頼み直す
    // 新しい物が先: 前に頼んだ小さいほうが大きいほうの後に来ることがある - それが大きいほうを
    // 置き換えることはない。
    if (!e.failed && e.image.width > 0 && d.job.size < e.size) return;
    if (d.ok && d.prepared) {
        const ThumbnailMaker* maker = MakerFor(d.job.kind);
        if (ID2D1DeviceContext* dc = c.P().DC()) dc->Flush();  // Direct2D がここまでに描いた物を、先に GPU へ送る
        try {
            d.ok = maker && maker->draw(gfx_, d.prepared.get(), d.job.size, d.image, &d.error);
        } catch (const std::exception& ex) {
            d.ok = false;
            d.error = std::format("描いている途中で止まりました ({})", ex.what());
        }
    }
    if (!d.ok) log::Info("サムネイル: 「{}」を作れないので種類の印で出します: {}", PathToUtf8(d.job.file.filename()), d.error);
    e.size = d.job.size;
    e.failed = !d.ok;
    e.image = d.ok ? std::move(d.image) : platform::Image{};
    e.bitmap.Reset();
    c.RequestRedraw();
}

void Thumbnails::Collect(ui::Context& c) {
    {
        std::lock_guard lock(mu_);
        for (Done& d : done_) toDraw_.push_back(std::move(d));
        done_.clear();
    }
    // 画像はここではこれ以上かからない。プラグインの種類は GPU で描き、毎フレーム少しずつ。
    size_t used = 0;
    for (; used < toDraw_.size(); ++used) {
        if (spentMs_ >= kFrameBudgetMs) break;
        const auto start = std::chrono::steady_clock::now();
        Finish(toDraw_[used], c);
        spentMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        --working_;
    }
    toDraw_.erase(toDraw_.begin(), toDraw_.begin() + static_cast<std::ptrdiff_t>(used));
    if (working_ > 0) c.RequestAnimation();  // 次のフレームでまた見る (worker はウィンドウを起こさない)
}

ID2D1Bitmap* Thumbnails::Get(ui::Context& c, const std::string& guid, asset::Kind kind, const std::filesystem::path& file, uint32_t pixels) {
    if (!Has(kind)) return nullptr;
    const double now = c.Time();
    if (now != frame_) {
        frame_ = now;
        spentMs_ = 0;
        if (now - sweptAt_ > kForgetSeconds / 4) {
            sweptAt_ = now;
            std::erase_if(entries_, [&](const auto& kv) { return now - kv.second.usedAt > kForgetSeconds && kv.second.asked == 0; });
        }
        Collect(c);
    }
    // GPU のデバイスが作り直された: ビットマップはすべて捨てる (取っておいた画像から作り直す)。
    if (gfx_.Generation() != generation_) {
        generation_ = gfx_.Generation();
        for (auto& [id, entry] : entries_) {
            entry.bitmap.Reset();
            entry.dc = nullptr;
        }
    }
    Entry& e = entries_[guid];
    e.usedAt = now;
    if (now - e.checkedAt >= kCheckSeconds) {
        e.checkedAt = now;
        std::error_code ec;
        const auto time = asset::ChangedAt(file, ec);  // ファイルまたはその取り込みの設定 (.meta)
        if (ec) return nullptr;  // なくなった (パネルがすぐ気づく)
        if (time != e.time) {
            e.time = time;
            e.size = 0;  // ディスクで変わった: 作り直す
        }
    }
    const uint32_t want = Bucket(pixels);
    if ((e.size == 0 || (!e.failed && e.size < want)) && e.asked < want) {
        // worker へ (最初の仕事で起動する)。
        e.asked = want;
        ++working_;
        {
            std::lock_guard lock(mu_);
            jobs_.push_back({guid, kind, file, want, e.time});
        }
        if (!worker_.joinable()) worker_ = std::jthread([this](std::stop_token stop) { Work(stop); });
        wake_.notify_one();
        c.RequestAnimation();
    }
    if (e.failed || e.image.width == 0) return nullptr;
    ID2D1DeviceContext* dc = c.P().DC();
    if (!e.bitmap || e.dc != dc) {
        e.bitmap = platform::BitmapFromImage(dc, e.image);
        e.dc = dc;
    }
    return e.bitmap.Get();
}

}  // namespace rm::editor
