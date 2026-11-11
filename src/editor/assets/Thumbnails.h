#pragma once
// Small pictures of the assets for the アセット panel:
//
//   a picture file (PNG / JPEG ...)    the picture itself, shrunk (WIC: platform/Image.h)
//   a kind a plugin draws (3D model)   drawn small by that plugin - the core knows nothing of 3D; a
//                                      plugin offers a ThumbnailMaker at kThumbnailPoint
//   anything else                      none (the panel shows the kind's picture, as before)
//
// One per open project (every software's panel shares them), kept by the asset's id. Made on the UI
// thread while the panel is drawn, a few at a time (a frame spends at most kFrameBudgetMs on them;
// the rest are made on the next frames). The file is looked at again every second: when it changed
// (saved again from a paint or 3D tool) the thumbnail is made again. Made a little bigger than
// asked (32 / 64 / 128 / 256 pixels), so making the panel's thumbnails bigger does not make them
// all again at every step. Ones not shown for a minute are forgotten.

#include "core/asset/AssetIndex.h"
#include "platform/Gfx.h"
#include "platform/Image.h"
#include "ui/Context.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace rm::editor {

class PluginRegistry;

// Where a plugin offers to draw a kind of asset small: registry.Contribute(kThumbnailPoint, maker).
inline constexpr char kThumbnailPoint[] = "core.thumbnail";

struct ThumbnailMaker {
    asset::Kind kind = asset::Kind::Model;
    // Draws `file` into `size` x `size` pixels: platform::PixelOrder::PremultipliedBgra, see-through
    // where there is nothing. What the file points to (a model's pictures) is read only inside `root`
    // (the project's Assets/). False: *error says why (Japanese). Called on the UI thread, with
    // Direct2D's drawing so far already sent to the GPU (it may use Direct3D).
    bool (*make)(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                 platform::Image& out, std::string* error) = nullptr;
};

class Thumbnails {
public:
    static constexpr double kFrameBudgetMs = 30.0;

    Thumbnails(platform::Gfx& gfx, const PluginRegistry& registry, std::filesystem::path root);

    // Whether files of this kind get one (pictures, and kinds a plugin draws).
    bool Has(asset::Kind kind) const;
    // The thumbnail of `file` (the asset `guid`), at least `pixels` big; null while it is not made
    // yet (another frame is asked for) or when it cannot be (the reason went to the console once).
    ID2D1Bitmap* Get(ui::Context& c, const std::string& guid, asset::Kind kind, const std::filesystem::path& file, uint32_t pixels);

private:
    struct Entry {
        platform::Image image;  // kept: a panel moved to another window needs its own bitmap
        uint32_t size = 0;      // made this big (0: not yet)
        bool failed = false;
        std::filesystem::file_time_type time{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        ID2D1DeviceContext* dc = nullptr;  // the bitmap belongs to this window's drawing
        double checkedAt = -1e9, usedAt = 0;
    };

    const ThumbnailMaker* MakerFor(asset::Kind kind) const;
    bool Make(ui::Context& c, asset::Kind kind, const std::filesystem::path& file, uint32_t size, platform::Image& out, std::string* error);

    platform::Gfx& gfx_;
    const PluginRegistry& registry_;
    std::filesystem::path root_;
    std::unordered_map<std::string, Entry> entries_;
    double frame_ = -1;    // the time of the frame being drawn (a new one resets the budget)
    double spentMs_ = 0;   // spent making thumbnails in this frame
    double sweptAt_ = 0;
};

}  // namespace rm::editor
