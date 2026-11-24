#include "rm_engine/game_x_3d/editor/MaterialEditor.h"

#include "editor/assets/AssetFileType.h"
#include "editor/assets/Thumbnails.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"
#include "rm_engine/game_x_3d/runtime/Material.h"
#include "rm_engine/space3d/editor/Thumbnail.h"
#include "rm_engine/space3d/runtime/Primitives.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace rm::game_x_3d {

namespace {

// サムネイルのワーカーが作るもの: マテリアルと、その各スロットの絵 (読めたもの)。
struct PreparedMaterial {
    Material material;
    std::array<std::optional<space3d::ThumbnailPicture>, space3d::kPictureSlots> pictures;
};

std::shared_ptr<void> PrepareMaterialThumbnail(const std::filesystem::path& file, const editor::AssetFinder& find, uint32_t size,
                                               std::string* error) {
    const auto bytes = platform::ReadFileBytes(file, error);
    if (!bytes) return nullptr;
    auto mat = ReadMaterial(*bytes, error);
    if (!mat) return nullptr;
    auto p = std::make_shared<PreparedMaterial>();
    p->material = std::move(*mat);
    const auto pictures = MaterialPictures(p->material);
    for (size_t slot = 0; slot < pictures.size(); ++slot) {
        if (pictures[slot].image.Empty()) continue;
        const std::filesystem::path at = find(pictures[slot].image.guid);
        if (!at.empty()) p->pictures[slot] = space3d::LoadThumbnailPicture(at, pictures[slot].srgb, size);
    }
    return p;
}

bool DrawMaterialThumbnail(platform::Gfx& gfx, const void* prepared, uint32_t size, platform::Image& out, std::string* error) {
    const auto& p = *static_cast<const PreparedMaterial*>(prepared);
    space3d::Renderer* renderer = space3d::ThumbnailRenderer(gfx, error);
    if (!renderer) return false;
    const space3d::GpuMesh* sphere = renderer->ShapeOf(space3d::Shape::Sphere);
    if (!sphere) {
        if (error) *error = "球を用意できませんでした";
        return false;
    }
    std::array<space3d::TextureRef, space3d::kPictureSlots> textures;
    for (size_t slot = 0; slot < textures.size(); ++slot) {
        std::string ignored;  // (読めない絵: それなしで描く)
        if (p.pictures[slot]) textures[slot] = renderer->UploadTexture(p.pictures[slot]->pixels, &ignored, p.pictures[slot]->sampling);
    }
    // シーンビューとゲームと同じ道 (MaterialLook)。球は 0 番の材質で塗る。
    const space3d::DrawMaterial look = MaterialLook(p.material, [&](const MaterialPicture&, size_t slot) { return textures[slot]; });
    return space3d::RenderThumbnail(gfx, *renderer, *sphere, std::span<const space3d::DrawMaterial>(&look, 1), size, out, error);
}

}  // namespace

void RegisterMaterialEditor(editor::PluginRegistry& registry) {
    using editor::TrKey;
    static const editor::AssetFileType kMaterialFile{
        .extension = kMaterialExtension,
        .kind = kMaterialKind,
        .type = &reflect::TypeOf<Material>,
        .label = TrKey(L"マテリアル"),
        .newName = TrKey(L"新しいマテリアル"),
        .folder = "Materials",
        .note = TrKey(L"メッシュの「マテリアル」の欄に入れると、その見た目で描きます。変えるとすぐシーンビューに出ます。Ctrl+Z で戻せます"),
    };
    registry.Contribute(editor::kAssetFileTypePoint, kMaterialFile);
    static const editor::ThumbnailMaker kMaterialThumbnail{.kind = asset::Kind::Other,
                                                           .prepare = nullptr,
                                                           .draw = &DrawMaterialThumbnail,
                                                           .extension = kMaterialExtension,
                                                           .prepareWith = &PrepareMaterialThumbnail};
    registry.Contribute(editor::kThumbnailPoint, kMaterialThumbnail);
}

}  // namespace rm::game_x_3d
