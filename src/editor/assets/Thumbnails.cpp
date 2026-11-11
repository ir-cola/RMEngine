#include "editor/assets/Thumbnails.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/plugin/Software.h"

#include <chrono>

namespace rm::editor {

namespace {

constexpr double kCheckSeconds = 1.0;    // how often a file is looked at again
constexpr double kForgetSeconds = 60.0;  // a thumbnail not shown this long is forgotten

// The size made for `pixels`: the next of 32 / 64 / 128 / 256 (bigger is not needed in a panel).
uint32_t Bucket(uint32_t pixels) {
    uint32_t size = 32;
    while (size < pixels && size < 256) size *= 2;
    return size;
}

}  // namespace

Thumbnails::Thumbnails(platform::Gfx& gfx, const PluginRegistry& registry, std::filesystem::path root)
    : gfx_(gfx), registry_(registry), root_(std::move(root)) {}

const ThumbnailMaker* Thumbnails::MakerFor(asset::Kind kind) const {
    for (const auto& m : registry_.Contributions<ThumbnailMaker>(kThumbnailPoint)) {
        if (m.item->kind == kind && m.item->make) return m.item;
    }
    return nullptr;
}

bool Thumbnails::Has(asset::Kind kind) const { return kind == asset::Kind::Image || MakerFor(kind) != nullptr; }

bool Thumbnails::Make(ui::Context& c, asset::Kind kind, const std::filesystem::path& file, uint32_t size, platform::Image& out,
                      std::string* error) {
    if (kind == asset::Kind::Image) {
        auto image = platform::LoadImageFile(gfx_.Wic(), file, platform::PixelOrder::PremultipliedBgra, {size, size}, error);
        if (!image) return false;
        out = std::move(*image);
        return true;
    }
    const ThumbnailMaker* maker = MakerFor(kind);
    if (!maker) return false;
    if (ID2D1DeviceContext* dc = c.P().DC()) dc->Flush();  // what Direct2D drew so far goes to the GPU first
    return maker->make(gfx_, file, root_, size, out, error);
}

ID2D1Bitmap* Thumbnails::Get(ui::Context& c, const std::string& guid, asset::Kind kind, const std::filesystem::path& file, uint32_t pixels) {
    if (!Has(kind)) return nullptr;
    const double now = c.Time();
    if (now != frame_) {
        frame_ = now;
        spentMs_ = 0;
        if (now - sweptAt_ > kForgetSeconds / 4) {
            sweptAt_ = now;
            std::erase_if(entries_, [&](const auto& kv) { return now - kv.second.usedAt > kForgetSeconds; });
        }
    }
    Entry& e = entries_[guid];
    e.usedAt = now;
    if (now - e.checkedAt >= kCheckSeconds) {
        e.checkedAt = now;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        if (ec) return nullptr;  // gone (the panel will see it soon)
        if (time != e.time) {
            e.time = time;
            e.size = 0;  // changed on disk: made again
        }
    }
    const uint32_t want = Bucket(pixels);
    if (e.size == 0 || (!e.failed && e.size < want)) {
        if (spentMs_ >= kFrameBudgetMs) {
            c.RequestAnimation();  // the next frame makes more
        } else {
            const auto start = std::chrono::steady_clock::now();
            std::string error;
            platform::Image image;
            const bool ok = Make(c, kind, file, want, image, &error);
            spentMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (!ok) log::Info("サムネイル: 「{}」を作れないので種類の印で出します: {}", PathToUtf8(file.filename()), error);
            e.size = want;
            e.failed = !ok;
            e.image = ok ? std::move(image) : platform::Image{};
            e.bitmap.Reset();
            c.RequestRedraw();
        }
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
