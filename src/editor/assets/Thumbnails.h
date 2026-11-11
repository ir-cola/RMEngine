#pragma once
// Small pictures of the assets for the アセット panel (and the reference fields' lists):
//
//   a picture file (PNG / JPEG ...)    the picture itself, shrunk (WIC: platform/Image.h)
//   a kind a plugin draws (3D model)   drawn small by that plugin - the core knows nothing of 3D; a
//                                      plugin offers a ThumbnailMaker at kThumbnailPoint
//   anything else                      none (the panel shows the kind's picture, as before)
//
// One per open project (every software's panel shares them), kept by the asset's id. The slow part
// is done on a thread of its own (one worker, the newest asked first - what is on the screen now):
// a picture is decoded there whole; a plugin's kind is read and prepared there (ThumbnailMaker::
// prepare: the file, its pictures) and only drawn on the UI thread (draw: the GPU, a frame spends at
// most kFrameBudgetMs on that). While something is being made the panel asks for more frames, so
// each thumbnail appears as soon as it is ready. The file (or its .meta) is looked at again every
// second: when it changed the thumbnail is made again. Made a little bigger than asked (32 / 64 /
// 128 / 256 pixels), so making the panel's thumbnails bigger does not make them all again at every
// step. Ones not shown for a minute are forgotten.

#include "core/asset/AssetIndex.h"
#include "platform/Gfx.h"
#include "platform/Image.h"
#include "ui/Context.h"

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rm::editor {

class PluginRegistry;

// Where a plugin offers to draw a kind of asset small: registry.Contribute(kThumbnailPoint, maker).
inline constexpr char kThumbnailPoint[] = "core.thumbnail";

struct ThumbnailMaker {
    asset::Kind kind = asset::Kind::Model;
    // On the worker thread: reads `file` (and what it points to - a model's pictures - only inside
    // `root`, the project's Assets/) into whatever `draw` needs. Null: *error says why (Japanese).
    // Must not touch the GPU, the UI or anything of the UI thread; WIC through
    // platform::ThreadImagingFactory().
    std::shared_ptr<void> (*prepare)(const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                                     std::string* error) = nullptr;
    // On the UI thread, with Direct2D's drawing so far already sent to the GPU (it may use Direct3D):
    // draws what prepare made into `size` x `size` pixels - platform::PixelOrder::PremultipliedBgra,
    // see-through where there is nothing. False: *error says why.
    bool (*draw)(platform::Gfx& gfx, const void* prepared, uint32_t size, platform::Image& out, std::string* error) = nullptr;
};

class Thumbnails {
public:
    static constexpr double kFrameBudgetMs = 30.0;

    Thumbnails(platform::Gfx& gfx, const PluginRegistry& registry, std::filesystem::path root);
    ~Thumbnails();  // stops the worker (the thumbnail it is making is finished and dropped)
    Thumbnails(const Thumbnails&) = delete;
    Thumbnails& operator=(const Thumbnails&) = delete;

    // Whether files of this kind get one (pictures, and kinds a plugin draws).
    bool Has(asset::Kind kind) const;
    // The thumbnail of `file` (the asset `guid`), at least `pixels` big; null while it is not made
    // yet (another frame is asked for) or when it cannot be (the reason went to the console once).
    ID2D1Bitmap* Get(ui::Context& c, const std::string& guid, asset::Kind kind, const std::filesystem::path& file, uint32_t pixels);

private:
    struct Entry {
        platform::Image image;  // kept: a panel moved to another window needs its own bitmap
        uint32_t size = 0;      // made this big (0: not yet)
        uint32_t asked = 0;     // the worker is making it this big (0: nothing asked)
        bool failed = false;
        std::filesystem::file_time_type time{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        ID2D1DeviceContext* dc = nullptr;  // the bitmap belongs to this window's drawing
        double checkedAt = -1e9, usedAt = 0;
    };

    // What the worker is asked, and what it gives back.
    struct Job {
        std::string guid;
        asset::Kind kind;
        std::filesystem::path file;
        uint32_t size = 0;
        std::filesystem::file_time_type time{};
    };
    struct Done {
        Job job;
        bool ok = false;
        platform::Image image;          // a picture: done
        std::shared_ptr<void> prepared;  // a plugin's kind: still to draw
        std::string error;
    };

    const ThumbnailMaker* MakerFor(asset::Kind kind) const;
    void Work(std::stop_token stop);         // the worker thread
    void Collect(ui::Context& c);            // the worker's results into the entries (UI thread)
    void Finish(Done& d, ui::Context& c);    // one result: drawn if it must be, kept

    platform::Gfx& gfx_;
    const PluginRegistry& registry_;
    std::filesystem::path root_;
    std::unordered_map<std::string, Entry> entries_;
    double frame_ = -1;    // the time of the frame being drawn (a new one resets the budget)
    double spentMs_ = 0;   // spent making thumbnails in this frame
    double sweptAt_ = 0;

    // Shared with the worker (under mu_).
    std::mutex mu_;
    std::condition_variable_any wake_;
    std::vector<Job> jobs_;    // the newest last (taken first)
    std::vector<Done> done_;
    size_t working_ = 0;       // jobs given out and not collected yet (UI thread only)
    std::vector<Done> toDraw_;  // prepared, waiting for the next frame's budget (UI thread only)
    std::jthread worker_;      // last: stopped and joined first
};

}  // namespace rm::editor
