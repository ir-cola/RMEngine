// 書き出したゲームの焼いたモデル (plugins/rm_engine/space3d/runtime/MeshBake) のテスト: 書いて
// 同じに読み戻す、新しいバージョンのチャンクは飛ばす、壊れたものは断る、本物のモデルを
// エディターが読む形で読み、焼いて、読み戻す。

#include "Test.h"

#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/MeshFile.h"

#include <cstring>
#include <filesystem>

using namespace rm;
using namespace rm::space3d;

namespace {

MeshData Sample() {
    MeshData m;
    m.vertices = {{{0, 0, 0}, {0, 1, 0}, {0, 0}}, {{1, 0, 0}, {0, 1, 0}, {1, 0}}, {{0, 0, 1}, {0, 1, 0}, {0, 1}}, {{1, 0, 1}, {0, 0.5f, 0.5f}, {1, 1}}};
    m.indices = {0, 2, 1, 1, 2, 3};
    m.min = {0, 0, 0};
    m.max = {1, 0, 1};
    m.parts = {{0, 3, 0}, {3, 3, 1}};
    MeshMaterial a;
    a.name = "壁";
    a.baseColor = {0.5f, 0.25f, 1, 0.75f};
    a.alpha = AlphaMode::Mask;
    a.alphaCutoff = 0.3f;
    a.metallic = 0.2f;
    a.roughness = 0.6f;
    a.normalScale = 0.8f;
    a.emissive = {2, 0, 0, 1};
    MeshMaterial b;
    b.name = "roof";
    b.alpha = AlphaMode::Blend;
    m.materials = {a, b};
    return m;
}

bool SameMaterialNumbers(const MeshMaterial& x, const MeshMaterial& y) {
    return x.name == y.name && x.baseColor == y.baseColor && x.alpha == y.alpha && x.alphaCutoff == y.alphaCutoff && x.metallic == y.metallic &&
           x.roughness == y.roughness && x.normalScale == y.normalScale && x.emissive == y.emissive;
}

std::vector<BakedPictures> PicturesOf(const MeshData& m, const std::vector<TextureSampling>& sampling) {
    std::vector<BakedPictures> out;
    for (const MeshMaterial& mat : m.materials) {
        BakedPictures p;
        const int slots[4] = {mat.image, mat.metallicRoughnessImage, mat.normalImage, mat.emissiveImage};
        for (size_t s = 0; s < 4; ++s) {
            p.path[s] = slots[s] >= 0 ? m.images[static_cast<size_t>(slots[s])].file : std::string();
            if (slots[s] >= 0) p.sampling[s] = sampling[static_cast<size_t>(slots[s])];
        }
        out.push_back(p);
    }
    return out;
}

}  // namespace

RM_TEST(MeshBake_RoundTrip) {
    const MeshData m = Sample();
    std::vector<BakedPictures> pictures(2);
    pictures[0].path = {"Baked/a/0.dds", "", "Baked/a/1.dds", ""};
    const TextureSampling dots{asset::TextureFilter::Sharp, asset::TextureWrap::Mirror, true};
    pictures[0].sampling = {dots, {}, {}, {}};
    pictures[1].path = {"Baked/a/0.dds", "", "", "Baked/a/2.dds"};
    pictures[1].sampling = {dots, {}, {}, {}};
    const std::string bytes = WriteBakedMesh(m, pictures);
    std::string err;
    std::vector<TextureSampling> sampling;
    const auto back = ReadBakedMesh(bytes, &err, &sampling);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->indices == m.indices && back->parts == m.parts && back->min == m.min && back->max == m.max);
    CHECK(back->vertices.size() == m.vertices.size());
    for (size_t i = 0; i < m.vertices.size() && i < back->vertices.size(); ++i) {
        CHECK(back->vertices[i].position == m.vertices[i].position && back->vertices[i].normal == m.vertices[i].normal &&
              back->vertices[i].uv == m.vertices[i].uv);
    }
    CHECK(back->materials.size() == 2 && SameMaterialNumbers(back->materials[0], m.materials[0]) && SameMaterialNumbers(back->materials[1], m.materials[1]));
    // 絵: パスごとに画像 1 つ。それを使うマテリアルで共有し、そのスロットに入れる。
    CHECK(back->images.size() == 3);
    CHECK(back->materials[0].image == back->materials[1].image && back->materials[0].image >= 0);
    CHECK(back->materials[0].normalImage >= 0 && back->materials[0].metallicRoughnessImage == -1 && back->materials[1].emissiveImage >= 0);
    CHECK(back->images[static_cast<size_t>(back->materials[0].normalImage)].file == "Baked/a/1.dds");
    // 各絵の読み方 (くっきり・鏡のように反転・ストリーミング): 色の絵だけ。
    CHECK(sampling.size() == 3 && sampling[static_cast<size_t>(back->materials[0].image)] == dots &&
          sampling[static_cast<size_t>(back->materials[0].normalImage)] == TextureSampling{});
    // 読んだものからもう一度書く: 同じバイト列。
    CHECK(WriteBakedMesh(*back, PicturesOf(*back, sampling)) == bytes);
}

RM_TEST(MeshBake_UnknownChunksAndBroken) {
    const MeshData m = Sample();
    std::string bytes = WriteBakedMesh(m, {});
    // 新しいバージョンのチャンク: 飛ばす。
    std::string newer = bytes + "XTRA";
    const uint64_t three = 3;
    newer.append(reinterpret_cast<const char*>(&three), 8);
    newer += "abc";
    std::string err;
    const auto read = ReadBakedMesh(newer, &err);
    CHECK(read && read->indices == m.indices && read->images.empty());
    // 焼いたメッシュではない / 途中で切れた / 頂点番号を越える部分 / 新しい形式。
    CHECK(!ReadBakedMesh("hello world, not a mesh", &err) && !err.empty());
    CHECK(!ReadBakedMesh(std::string_view(bytes).substr(0, bytes.size() - 5), &err));
    MeshData bad = m;
    bad.parts = {{0, 99, 0}};
    CHECK(!ReadBakedMesh(WriteBakedMesh(bad, {}), &err));
    std::string later = bytes;
    const uint32_t two = kBakedMeshFormat + 1;
    std::memcpy(later.data() + 8, &two, 4);
    CHECK(!ReadBakedMesh(later, &err));
}

// 本物のモデルを、エディターが読むとおりに読み (LoadMeshFile: 取り込みの設定を適用)、焼いて読む。
RM_TEST(MeshBake_ARealModel) {
    const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path() / "docs" / "samples" / "models" / "house.gltf";
    std::string err;
    const auto mesh = LoadMeshFile(file, &err);
    CHECK(mesh.has_value());
    if (!mesh) return;
    std::vector<BakedPictures> pictures(mesh->materials.size());
    for (size_t i = 0; i < pictures.size(); ++i) {
        if (mesh->materials[i].image >= 0) pictures[i].path[0] = "Baked/house/" + std::to_string(mesh->materials[i].image) + ".dds";
    }
    const auto back = ReadBakedMesh(WriteBakedMesh(*mesh, pictures), &err);
    CHECK(back && back->vertices.size() == mesh->vertices.size() && back->indices == mesh->indices && back->parts == mesh->parts);
    CHECK(back && back->materials.size() == mesh->materials.size() && back->min == mesh->min && back->max == mesh->max);
    size_t withPicture = 0;
    for (const auto& mat : mesh->materials) withPicture += mat.image >= 0 ? 1 : 0;
    CHECK(withPicture > 0 && back && !back->images.empty());
}
