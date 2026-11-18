// Baking for the exported game (game_production/editor/GameExport.h: ExportBaker), in the 3D connector
// because the code is 3D 空間's:
//   3D models  read as the editor reads them (LoadMeshFile: the import settings' size and up axis) and
//              written in this engine's form (MeshBake: Baked/<id>.rmmesh); each picture of their
//              materials as the GPU takes it (Baked/<id>/<n>.dds) - the texture cache's DDS when it is
//              there, else made by the same code (PictureDecoder) and kept in the cache too
// (Sprites' images are ゲーム制作 × 2D's: game_x_2d/editor/ExportBake2D.)
// The readers of glTF / OBJ / FBX and the compression never go into the game.

#include "rm_engine/game_x_3d/editor/ExportBake.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Texture.h"
#include "rm_engine/space3d/runtime/TextureFile.h"

#include <chrono>
#include <format>
#include <map>
#include <thread>
#include <tuple>

namespace fs = std::filesystem;

namespace rm::game_x_3d {

namespace {

using game_production::BakeResult;

// One picture made by the texture cache's own code (and kept in its folder), waited for.
std::optional<space3d::TexturePixels> MakePicture(space3d::PictureDecoder& decoder, space3d::PictureDecoder::Source source, bool* fromCache,
                                                  std::string* error, space3d::TextureSampling* sampling = nullptr) {
    auto job = decoder.Request(std::move(source));
    while (!job->Done()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (fromCache) *fromCache = job->fromCache;
    if (sampling) *sampling = job->sampling;  // (the image's import settings)
    if (!job->pixels && error) *error = job->error;
    return std::move(job->pixels);
}

std::string DdsText(const space3d::TexturePixels& p) {
    const std::vector<uint8_t> bytes = space3d::WriteDds(p);
    return std::string(bytes.begin(), bytes.end());
}

BakeResult Bake(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    const asset::Kind kind = asset::KindOf(PathToUtf8(a.file.filename()), false);
    if (kind != asset::Kind::Model) return BakeResult::NotMine;
    const fs::path cache = a.projectRoot / L"Cache" / L"Textures";
    space3d::PictureDecoder decoder;
    std::string why;
    // A model: as the editor reads it.
    const auto mesh = space3d::LoadMeshFile(a.file, &why);
    if (!mesh) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    // Its materials' pictures, one DDS per picture and way it is read (as ModelPaint finds them).
    const fs::path assets = a.projectRoot / L"Assets";
    std::map<std::tuple<int, int, bool>, std::pair<std::string, space3d::TextureSampling>> made;  // (image, use, srgb) -> its path, how it is read
    std::vector<space3d::BakedPictures> pictures(mesh->materials.size());
    int fromCache = 0, total = 0;
    for (size_t m = 0; m < mesh->materials.size(); ++m) {
        const auto slots = space3d::PictureSlots(mesh->materials[m]);
        for (size_t slot = 0; slot < slots.size(); ++slot) {
            const space3d::PictureSlot& ps = slots[slot];
            if (ps.image < 0 || ps.image >= static_cast<int>(mesh->images.size())) continue;
            const auto key = std::make_tuple(ps.image, static_cast<int>(ps.use), ps.srgb);
            if (const auto it = made.find(key); it != made.end()) {
                pictures[m].path[slot] = it->second.first;
                pictures[m].sampling[slot] = it->second.second;
                continue;
            }
            const space3d::MeshImage& img = mesh->images[static_cast<size_t>(ps.image)];
            space3d::PictureDecoder::Source s;
            if (!img.bytes.empty()) {
                s.bytes = std::make_shared<const std::vector<uint8_t>>(img.bytes);
                s.file = a.file;
                s.image = ps.image;
            } else {
                s.file = space3d::RelatedFile(a.file, img.file, assets);
                if (s.file.empty()) continue;  // not inside the project's Assets/: drawn without it (as in the editor)
            }
            s.use = ps.use;
            s.srgb = ps.srgb;
            s.cacheFolder = cache;
            s.canBC7 = true;  // as the editor's cache (a GPU without BC7: the game turns it into RGBA)
            bool cached = false;
            space3d::TextureSampling sampling;
            std::string pwhy;
            const auto pixels = MakePicture(decoder, s, &cached, &pwhy, &sampling);
            if (!pixels) continue;  // drawn without it (as in the editor)
            const std::string path = std::format("Baked/{}/{}.dds", a.id, made.size());
            out.files.push_back({path, DdsText(*pixels)});
            made[key] = {path, sampling};
            pictures[m].path[slot] = path;
            pictures[m].sampling[slot] = sampling;
            ++total;
            fromCache += cached ? 1 : 0;
        }
    }
    out.path = "Baked/" + a.id + ".rmmesh";
    out.files.insert(out.files.begin(), {out.path, space3d::WriteBakedMesh(*mesh, pictures)});
    out.note = std::format("モデルを焼きました: {} (三角形 {}・材質 {}・絵 {}、うちキャッシュから {})", a.rel, mesh->indices.size() / 3,
                           mesh->materials.size(), total, fromCache);
    return BakeResult::Done;
}

}  // namespace

void RegisterExportBake(editor::PluginRegistry& registry) {
    static const game_production::ExportBaker kBaker{&Bake};
    registry.Contribute(game_production::kExportBakePoint, kBaker);
}

}  // namespace rm::game_x_3d
