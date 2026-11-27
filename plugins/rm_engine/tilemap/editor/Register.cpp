#include "rm_engine/tilemap/editor/Register.h"

#include "editor/assets/AssetFileType.h"
#include "editor/assets/Thumbnails.h"
#include "editor/lang/Lang.h"
#include "editor/plugin/Software.h"
#include "platform/Image.h"
#include "platform/Shell.h"
#include "rm_engine/tilemap/runtime/Tileset.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace rm::tilemap {

namespace {

// サムネイル: 1 つ目の (画像のある) 絵の元の画像を、形を保って小さくしたもの (作業スレッドで)。
std::shared_ptr<void> PrepareThumbnail(const std::filesystem::path& file, const editor::AssetFinder& find, uint32_t size, std::string* error) {
    const auto bytes = platform::ReadFileBytes(file, error);
    if (!bytes) return nullptr;
    auto set = ReadTileset(*bytes, error);
    if (!set) return nullptr;
    for (const TileSource& s : set->sources) {
        if (s.image.Empty()) continue;
        const std::filesystem::path at = find(s.image.guid);
        if (at.empty()) continue;
        auto img = platform::LoadImageFile(platform::ThreadImagingFactory(), at, platform::PixelOrder::PremultipliedBgra, {size, size}, error);
        if (img) return std::make_shared<platform::Image>(std::move(*img));
    }
    if (error && error->empty()) *error = "絵の元の画像がありません";
    return nullptr;
}

// 縮めた画像を `size` x `size` の真ん中に置く (何もない所は透ける)。
bool DrawThumbnail(platform::Gfx&, const void* prepared, uint32_t size, platform::Image& out, std::string*) {
    const auto& img = *static_cast<const platform::Image*>(prepared);
    out.width = out.height = size;
    out.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
    const uint32_t w = std::min(img.width, size), h = std::min(img.height, size);
    const uint32_t ox = (size - w) / 2, oy = (size - h) / 2;
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(&out.pixels[(static_cast<size_t>(oy + y) * size + ox) * 4], &img.pixels[static_cast<size_t>(y) * img.width * 4], static_cast<size_t>(w) * 4);
    }
    return true;
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    using editor::TrKey;
    static const editor::AssetFileType kTilesetFile{
        .extension = kTilesetExtension,
        .kind = kTilesetKind,
        .type = &reflect::TypeOf<Tileset>,
        .label = TrKey(L"タイルセット"),
        .newName = TrKey(L"新しいタイルセット"),
        .folder = "Tiles",
        .note = TrKey(L"タイルの層の「タイルセット」の欄に入れると、パネル「タイル」のパレットに並びます。絵の元と切り方はパネル「タイル」の「タイルセット」でも変えられます"),
    };
    registry.Contribute(editor::kAssetFileTypePoint, kTilesetFile);
    static const editor::ThumbnailMaker kThumbnail{.kind = asset::Kind::Other,
                                                   .prepare = nullptr,
                                                   .draw = &DrawThumbnail,
                                                   .extension = kTilesetExtension,
                                                   .prepareWith = &PrepareThumbnail};
    registry.Contribute(editor::kThumbnailPoint, kThumbnail);
}

}  // namespace rm::tilemap
