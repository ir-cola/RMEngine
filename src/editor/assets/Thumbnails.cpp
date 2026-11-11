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

// A result that says only why it could not be made.
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
            if (!wake_.wait(lock, stop, [&] { return !jobs_.empty(); })) return;  // stopped
            job = std::move(jobs_.back());  // the newest: what the panel shows now
            jobs_.pop_back();
        }
        Done d;
        d.job = std::move(job);
        // Nothing thrown here may leave the thread (that would end the whole editor): a file that
        // makes the reading run out of memory, or a plugin that throws, is just "cannot be made".
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
    if (it == entries_.end()) return;  // forgotten meanwhile
    Entry& e = it->second;
    if (e.asked == d.job.size) e.asked = 0;
    if (d.job.time != e.time) return;  // the file changed meanwhile: it is asked again
    // Newest first: a smaller one asked earlier may come after a bigger one - it never replaces it.
    if (!e.failed && e.image.width > 0 && d.job.size < e.size) return;
    if (d.ok && d.prepared) {
        const ThumbnailMaker* maker = MakerFor(d.job.kind);
        if (ID2D1DeviceContext* dc = c.P().DC()) dc->Flush();  // what Direct2D drew so far goes to the GPU first
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
    // Pictures cost nothing more here; a plugin's kind is drawn on the GPU, a few each frame.
    size_t used = 0;
    for (; used < toDraw_.size(); ++used) {
        if (spentMs_ >= kFrameBudgetMs) break;
        const auto start = std::chrono::steady_clock::now();
        Finish(toDraw_[used], c);
        spentMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        --working_;
    }
    toDraw_.erase(toDraw_.begin(), toDraw_.begin() + static_cast<std::ptrdiff_t>(used));
    if (working_ > 0) c.RequestAnimation();  // look again next frame (the worker does not wake the window)
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
    Entry& e = entries_[guid];
    e.usedAt = now;
    if (now - e.checkedAt >= kCheckSeconds) {
        e.checkedAt = now;
        std::error_code ec;
        const auto time = asset::ChangedAt(file, ec);  // the file or its import settings (.meta)
        if (ec) return nullptr;  // gone (the panel will see it soon)
        if (time != e.time) {
            e.time = time;
            e.size = 0;  // changed on disk: made again
        }
    }
    const uint32_t want = Bucket(pixels);
    if ((e.size == 0 || (!e.failed && e.size < want)) && e.asked < want) {
        // To the worker (started with the first job).
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
