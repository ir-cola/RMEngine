// 書き出すゲームの焼き込み (game_production/editor/GameExport.h: ExportBaker)。コードが 3D 空間の
// ものなので 3D コネクターにある:
//   3D モデル  エディターと同じように読み (LoadMeshFile: 取り込みの設定の大きさと上の軸・クリップの圧縮)、
//              このエンジンの形で書く (MeshBake: Baked/<id>.rmmesh)。クリップは使っているものだけを 1 つずつ
//              Baked/<id>/clip<n>.rmclip に (UsedClips: アニメーター・状態とルール・スクリプトが名前で指すもの)。材質の各画像は GPU が
//              読める形にする (Baked/<id>/<n>.dds) - テクスチャキャッシュの DDS があればそれ、
//              なければ同じコード (PictureDecoder) で作り、キャッシュにも残す
//   マテリアル (.rmmat) そのファイルに、各スロットの絵を同じように DDS にしたもの (Baked/<id>/<スロット>.dds)
//              の場所と読み方を "baked" として足す (Material.h の WriteBakedMaterial: Baked/<id>.rmmat)。
//              絵の画像そのものも、名指されたアセットとしてゲームに入る (画像のベイカーが焼く)
// (スプライトの画像は ゲーム制作 × 2D のもの: game_x_2d/editor/ExportBake2D。)
// glTF / OBJ / FBX の読み込みと圧縮は、ゲームには入らない。

#include "rm_engine/game_x_3d/editor/ExportBake.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/game_x_3d/runtime/Material.h"
#include "rm_engine/space3d/runtime/ClipSettings.h"
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

// マテリアル: 絵はスロットの読み方 (色 / 数 / 法線) で DDS に。読めない・無い絵はそれなしで描く (エディターと同じ)。
BakeResult BakeMaterial(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    std::string why;
    const auto bytes = platform::ReadFileBytes(a.file, &why);
    const auto mat = bytes ? ReadMaterial(*bytes, &why) : std::nullopt;
    if (!mat) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    space3d::PictureDecoder decoder;
    const fs::path cache = a.projectRoot / L"Cache" / L"Textures";
    const auto pictures = MaterialPictures(*mat);
    std::vector<BakedMaterialPicture> baked(pictures.size());
    int total = 0, fromCache = 0, missing = 0;
    for (size_t slot = 0; slot < pictures.size(); ++slot) {
        const MaterialPicture& pic = pictures[slot];
        if (pic.image.Empty()) continue;
        const fs::path* file = nullptr;
        if (a.files) {
            if (const auto it = a.files->find(pic.image.guid); it != a.files->end()) file = &it->second;
        }
        if (!file) {
            ++missing;  // (消えた画像・プロジェクトの外: その絵なしで描く)
            continue;
        }
        space3d::PictureDecoder::Source s;
        s.file = *file;
        s.use = pic.use;
        s.srgb = pic.srgb;
        s.cacheFolder = cache;
        s.canBC7 = true;  // エディターのキャッシュと同じ (BC7 の無い GPU: ゲームが RGBA にする)
        bool cached = false;
        space3d::TextureSampling sampling;
        std::string pwhy;
        const auto pixels = MakePicture(decoder, s, &cached, &pwhy, &sampling);
        if (!pixels) {
            ++missing;
            continue;
        }
        const std::string path = std::format("Baked/{}/{}.dds", a.id, slot);
        out.files.push_back({path, DdsText(*pixels)});
        baked[slot] = {path, static_cast<int>(space3d::PackSampling(sampling))};
        ++total;
        fromCache += cached ? 1 : 0;
    }
    out.path = "Baked/" + a.id + kMaterialExtension;
    out.files.insert(out.files.begin(), {out.path, WriteBakedMaterial(*bytes, baked)});
    out.note = std::format("マテリアルを焼きました: {} (絵 {}、うちキャッシュから {}{})", a.rel, total, fromCache,
                           missing > 0 ? std::format("・読めない絵 {}", missing) : std::string());
    return BakeResult::Done;
}

BakeResult Bake(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    if (asset::Extension(PathToUtf8(a.file.filename())) == kMaterialExtension) return BakeMaterial(a, out, error);
    const asset::Kind kind = asset::KindOf(PathToUtf8(a.file.filename()), false);
    if (kind != asset::Kind::Model) return BakeResult::NotMine;
    const fs::path cache = a.projectRoot / L"Cache" / L"Textures";
    space3d::PictureDecoder decoder;
    std::string why;
    // モデル: エディターと同じように読む。
    auto mesh = space3d::LoadMeshFile(a.file, &why);
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
    // クリップ: 使っているものだけ、1 つずつ .rmclip に (モデルには名前とパスの一覧)。
    std::string clipNote;
    if (!mesh->clips.empty()) {
        static const std::vector<std::string> kNoDocuments;
        const std::vector<bool> used = space3d::UsedClips(mesh->clips, a.documents ? *a.documents : kNoDocuments);
        std::vector<std::string> left;
        for (size_t k = 0; k < mesh->clips.size(); ++k) {
            const space3d::Clip& clip = mesh->clips[k];
            if (!used[k]) {
                left.push_back(clip.name);
                continue;
            }
            const std::string path = std::format("Baked/{}/clip{}.rmclip", a.id, mesh->clipFiles.size());
            out.files.push_back({path, space3d::WriteBakedClip(clip)});
            mesh->clipFiles.push_back({clip.name, path});
        }
        clipNote = std::format("・クリップ {}", mesh->clipFiles.size());
        if (!left.empty()) {
            std::string names;
            for (const std::string& n : left) names += (names.empty() ? "" : "・") + n;
            clipNote += std::format (" (どれも名前で指していないので入れなかった {}: {}。入れるには、クリップの書き出しの「いつも入れる」)", left.size(), names);
        }
        mesh->clips.clear();
    }
    out.path = "Baked/" + a.id + ".rmmesh";
    out.files.insert(out.files.begin(), {out.path, space3d::WriteBakedMesh(*mesh, pictures)});
    out.note = std::format("モデルを焼きました: {} (三角形 {}・材質 {}・絵 {}、うちキャッシュから {}{})", a.rel, mesh->indices.size() / 3,
                           mesh->materials.size(), total, fromCache, clipNote);
    return BakeResult::Done;
}

}  // namespace

void RegisterExportBake(editor::PluginRegistry& registry) {
    static const game_production::ExportBaker kBaker{&Bake};
    registry.Contribute(game_production::kExportBakePoint, kBaker);
}

}  // namespace rm::game_x_3d
