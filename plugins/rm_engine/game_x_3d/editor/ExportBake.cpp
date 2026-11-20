// 書き出すゲームの焼き込み (game_production/editor/GameExport.h: ExportBaker)。コードが 3D 空間の
// ものなので 3D コネクターにある:
//   3D モデル  エディターと同じように読み (LoadMeshFile: 取り込みの設定の大きさと上の軸)、
//              このエンジンの形で書く (MeshBake: Baked/<id>.rmmesh)。材質の各画像は GPU が
//              読める形にする (Baked/<id>/<n>.dds) - テクスチャキャッシュの DDS があればそれ、
//              なければ同じコード (PictureDecoder) で作り、キャッシュにも残す
// (スプライトの画像は ゲーム制作 × 2D のもの: game_x_2d/editor/ExportBake2D。)
// glTF / OBJ / FBX の読み込みと圧縮は、ゲームには入らない。

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

// テクスチャキャッシュ自身のコードで作った画像 1 枚 (そのフォルダにも残す)。できるまで待つ。
std::optional<space3d::TexturePixels> MakePicture(space3d::PictureDecoder& decoder, space3d::PictureDecoder::Source source, bool* fromCache,
                                                  std::string* error, space3d::TextureSampling* sampling = nullptr) {
    auto job = decoder.Request(std::move(source));
    while (!job->Done()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (fromCache) *fromCache = job->fromCache;
    if (sampling) *sampling = job->sampling;  // (その画像の取り込みの設定)
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
    // モデル: エディターと同じように読む。
    const auto mesh = space3d::LoadMeshFile(a.file, &why);
    if (!mesh) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    // その材質の画像。画像と読み方ごとに DDS を 1 つ (ModelPaint が見つけるのと同じ)。
    const fs::path assets = a.projectRoot / L"Assets";
    std::map<std::tuple<int, int, bool>, std::pair<std::string, space3d::TextureSampling>> made;  // (画像, 使い道, srgb) -> そのパスと読み方
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
                if (s.file.empty()) continue;  // プロジェクトの Assets/ の外: それなしで描く (エディターと同じ)
            }
            s.use = ps.use;
            s.srgb = ps.srgb;
            s.cacheFolder = cache;
            s.canBC7 = true;  // エディターのキャッシュと同じ (BC7 の無い GPU: ゲームが RGBA にする)
            bool cached = false;
            space3d::TextureSampling sampling;
            std::string pwhy;
            const auto pixels = MakePicture(decoder, s, &cached, &pwhy, &sampling);
            if (!pixels) continue;  // それなしで描く (エディターと同じ)
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
