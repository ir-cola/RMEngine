// Baking a sprite's image for the exported game (game_production/editor/GameExport.h: ExportBaker):
// the file as it is (never compressed further: the game reads it with WIC as the editor does) with the
// file's own size and the import settings くっきり / 大きさの上限 in front (runtime/BakedImage.h) - so
// the game draws the sprite at the size and in the way the 2D scene view shows it.

#include "rm_engine/game_x_2d/editor/ExportBake2D.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/asset/ImportSettings.h"
#include "platform/Image.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "core/asset/BakedImage.h"

#include <algorithm>
#include <format>
#include <optional>
#include <thread>

namespace rm::game_x_2d {

using asset::BakedImageInfo;
using asset::kMaxImageSize;
using asset::ReadBakedImage;
using asset::WriteBakedImage;

namespace {

using game_production::BakeResult;

BakeResult Bake(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    if (asset::KindOf(PathToUtf8(a.file.filename()), false) != asset::Kind::Image) return BakeResult::NotMine;
    std::string why;
    const auto bytes = platform::ReadFileBytes(a.file, &why);
    if (!bytes) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    // The file's own size (as the 2D scene view reads it) - on a thread of its own: its WIC factory
    // (and COM) end with it, whatever thread bakes (the headless export's is the main thread, whose
    // thread_local factory would be let go after COM at the process's end).
    std::optional<platform::Image> whole;
    std::thread([&] { whole = platform::LoadImageFile(platform::ThreadImagingFactory(), a.file, platform::PixelOrder::PremultipliedBgra, {}, &why); }).join();
    if (!whole) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    const auto imp = asset::ReadImport<asset::ImageImport>(a.file);
    BakedImageInfo info;
    info.width = whole->width;
    info.height = whole->height;
    info.sharp = imp.sharp;
    info.maxSize = static_cast<uint32_t>(std::max(imp.maxSize, 1));
    out.path = "Baked/" + a.id + ".rmimage";
    out.files.push_back({out.path, WriteBakedImage(info, *bytes)});
    out.note = std::format("画像を焼きました: {} ({} x {}{}・上限 {})", a.rel, info.width, info.height, info.sharp ? "・くっきり" : "", info.maxSize);
    return BakeResult::Done;
}

}  // namespace

void RegisterExportBake(editor::PluginRegistry& registry) {
    static const game_production::ExportBaker kBaker{&Bake};
    registry.Contribute(game_production::kExportBakePoint, kBaker);
}

}  // namespace rm::game_x_2d
