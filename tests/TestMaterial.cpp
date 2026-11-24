// マテリアル (game_x_3d/runtime/Material.h) のテスト: 描く数への変換 (色は画面の色 -> 線形、発光の
// 強さ、透明の扱い、絵の繰り返し)、絵のスロット、メッシュの欄の決まり (形は 0 番、モデルは i 番が
// 自身の i 番の代わり、空の欄はモデルのまま)、ファイルの読み書き (知らないキーを残す)、前のシーンの
// メッシュがそのまま読めること、書き出しの焼いた絵の欄。

#include "Test.h"

#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_x_3d/runtime/Material.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/runtime/Primitives.h"

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace rm;
using namespace rm::game_x_3d;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

const reflect::FieldDesc* FieldNamed(const reflect::TypeDesc& t, std::string_view name) {
    for (const reflect::FieldDesc& f : t.fields) {
        if (name == f.name) return &f;
    }
    return nullptr;
}

}  // namespace

RM_TEST(Material_LookTurnsPickedColorsIntoLight) {
    Material m;
    m.color = {0.5f, 1.f, 0.f, 0.7f};
    m.emission = {1.f, 0.5f, 0.f, 1.f};
    m.emissionStrength = 3.f;
    m.metallic = 0.25f;
    m.roughness = 0.4f;
    m.normalScale = 2.f;
    m.tiling = {10, 4};
    m.offset = {0.5f, -0.25f};
    const space3d::DrawMaterial d = MaterialLook(m);
    // 選んだ色は画面の色: 描くときは線形の光 (.mtl の色と同じ ScreenToLinear)。アルファはそのまま。
    CHECK(Near(d.color.r, space3d::ScreenToLinear(0.5f)) && Near(d.color.r, 0.21404f, 1e-3f));
    CHECK(Near(d.color.g, 1.f) && Near(d.color.b, 0.f) && Near(d.color.a, 0.7f));
    CHECK(Near(d.emissive.r, 3.f) && Near(d.emissive.g, space3d::ScreenToLinear(0.5f) * 3.f) && Near(d.emissive.b, 0.f));
    CHECK(Near(d.metallic, 0.25f) && Near(d.roughness, 0.4f) && Near(d.normalScale, 2.f));
    CHECK(d.uvScale.x == 10 && d.uvScale.y == 4 && d.uvOffset.x == 0.5f && d.uvOffset.y == -0.25f);
    CHECK(d.alpha == space3d::AlphaMode::Opaque);
    CHECK(!d.texture && !d.metalRough && !d.normal && !d.emission);  // (絵の渡し方が無い: 絵なし)
    // 透明の扱い: 不透明 / 切り抜き (しきい値も) / 半透明。
    m.alpha = MaterialAlpha::Cutout;
    m.alphaCutoff = 0.3f;
    CHECK(MaterialLook(m).alpha == space3d::AlphaMode::Mask && Near(MaterialLook(m).alphaCutoff, 0.3f));
    m.alpha = MaterialAlpha::Blend;
    CHECK(MaterialLook(m).alpha == space3d::AlphaMode::Blend);
    // 新しいマテリアル: 白・金属でない・繰り返し 1・ずらし 0・光らない (これまでの描き方と同じ数)。
    const space3d::DrawMaterial plain = MaterialLook(Material{});
    const space3d::DrawMaterial white;
    CHECK(Near(plain.color.r, 1) && Near(plain.color.a, 1) && plain.metallic == white.metallic && plain.roughness == white.roughness);
    CHECK(plain.uvScale.x == 1 && plain.uvScale.y == 1 && plain.uvOffset.x == 0 && plain.uvOffset.y == 0);
    CHECK(Near(plain.emissive.r, 0) && Near(plain.emissive.g, 0) && Near(plain.emissive.b, 0));
}

RM_TEST(Material_PicturesGoToTheirSlots) {
    Material m;
    m.colorImage.guid = "00000000000000000000000000000001";
    m.normalImage.guid = "00000000000000000000000000000003";
    m.emissionImage.guid = "00000000000000000000000000000004";
    // スロットの読み方は 3D 空間のもの (0 色 sRGB、1 金属 / ラフネスは数、2 法線、3 発光 sRGB)。
    const auto pics = MaterialPictures(m);
    CHECK(pics[0].image == m.colorImage && pics[0].srgb && pics[0].use == space3d::TextureUse::Color);
    CHECK(pics[1].image.Empty() && !pics[1].srgb && pics[1].use == space3d::TextureUse::Data);
    CHECK(pics[2].image == m.normalImage && !pics[2].srgb && pics[2].use == space3d::TextureUse::Normal);
    CHECK(pics[3].image == m.emissionImage && pics[3].srgb && pics[3].use == space3d::TextureUse::Color);
    // 絵のある欄だけを聞き、返した絵をそのスロットに入れる。
    std::vector<size_t> asked;
    std::vector<space3d::TextureRef> made;
    const space3d::DrawMaterial d = MaterialLook(m, [&](const MaterialPicture& p, size_t slot) {
        CHECK(!p.image.Empty());
        asked.push_back(slot);
        made.push_back(std::make_shared<const space3d::GpuTexture>());
        return made.back();
    });
    CHECK(asked == (std::vector<size_t>{0, 2, 3}));
    CHECK(d.texture == made[0] && !d.metalRough && d.normal == made[1] && d.emission == made[2]);
}

RM_TEST(Material_MeshSlotsReplaceOrKeep) {
    space3d::DrawMaterial a, b, own0, own1;
    a.metallic = 0.1f;
    b.metallic = 0.2f;
    own0.metallic = 0.7f;
    own1.metallic = 0.8f;
    const space3d::DrawMaterial own[] = {own0, own1};
    std::vector<space3d::DrawMaterial> store;
    // 形: 0 番の欄だけ。無ければ空 (白 x 描く色: これまでの灰色)。
    {
        const space3d::DrawMaterial* slots[] = {&a, &b};
        const auto out = MeshMaterials(true, {}, slots, store);
        CHECK(out.size() == 1 && out[0].metallic == 0.1f);
        const space3d::DrawMaterial* none[] = {nullptr, &b};
        CHECK(MeshMaterials(true, {}, none, store).empty());
        CHECK(MeshMaterials(true, {}, {}, store).empty());
    }
    // モデル: 欄 i が自身の i 番の代わり。空の欄はモデルのまま。モデルより多い欄は使わない。
    {
        const space3d::DrawMaterial* slots[] = {nullptr, &b, &a};
        const auto out = MeshMaterials(false, own, slots, store);
        CHECK(out.size() == 2 && out[0].metallic == 0.7f && out[1].metallic == 0.2f);
    }
    // 何も替えない (欄が無い・全部空): モデル自身のものをそのまま (写さない)。
    {
        const auto out = MeshMaterials(false, own, {}, store);
        CHECK(out.data() == own && out.size() == 2);
        const space3d::DrawMaterial* empty[] = {nullptr, nullptr};
        CHECK(MeshMaterials(false, own, empty, store).data() == own);
    }
    // 材質の無いモデル: 0 番の欄は使える (部品は UseSlotZeroForBareModel で 0 番を指す)。
    {
        const space3d::DrawMaterial* slots[] = {&a};
        const auto out = MeshMaterials(false, {}, slots, store);
        CHECK(out.size() == 1 && out[0].metallic == 0.1f);
        CHECK(MeshMaterials(false, {}, {}, store).empty());
    }
}

RM_TEST(Material_BareModelPartsUseSlotZero) {
    space3d::MeshData bare;
    bare.vertices.resize(3);
    bare.indices = {0, 1, 2};
    UseSlotZeroForBareModel(bare);
    CHECK(bare.parts.size() == 1 && bare.parts[0].material == 0 && bare.parts[0].indexCount == 3);
    // 材質のあるモデルはそのまま。
    space3d::MeshData made = bare;
    made.materials.resize(1);
    made.parts = {{0, 3, -1}};
    UseSlotZeroForBareModel(made);
    CHECK(made.parts[0].material == -1);
    // 単純な形は 1 つの部品で 0 番の材質 (マテリアルの 0 番の欄で塗る)。
    for (space3d::Shape s : {space3d::Shape::Box, space3d::Shape::Sphere, space3d::Shape::Cylinder, space3d::Shape::Capsule, space3d::Shape::Plane}) {
        const space3d::MeshData m = space3d::ShapeMesh(s);
        CHECK(m.parts.size() == 1 && m.parts[0].material == 0 && m.parts[0].firstIndex == 0 && m.parts[0].indexCount == m.indices.size());
    }
}

RM_TEST(Material_FileKeepsUnknownKeys) {
    Material m;
    m.color = {0.2f, 0.4f, 0.6f, 1};
    m.colorImage.guid = "0123456789abcdef0123456789abcdef";
    m.alpha = MaterialAlpha::Cutout;
    m.tiling = {10, 10};
    const std::string text = WriteMaterial(m);
    {
        const auto v = json::Parse(text);
        CHECK(v && v->Get("kind").AsString() == kMaterialKind && v->Get("format").AsInt() == 1);
        CHECK(v && v->Get("colorImage").AsString() == m.colorImage.guid && v->Get("alpha").AsString() == "Cutout");
    }
    std::string err;
    const auto back = ReadMaterial(text, &err);
    CHECK(back && back->colorImage == m.colorImage && back->alpha == MaterialAlpha::Cutout && back->tiling.x == 10 && Near(back->color.g, 0.4f));
    // 新しい版が足したキーと、知らない列挙の名前は、読んで書き直しても残る (憲法 6)。
    auto doc = json::Parse(text);
    CHECK(doc.has_value());
    doc->Set("sheen", 0.3);
    doc->Set("alpha", "Glass");
    reflect::Unknowns unknowns;
    auto future = ReadMaterial(json::Write(*doc), &err, &unknowns);
    CHECK(future && future->alpha == MaterialAlpha::Opaque);  // (知らない列挙: 既定で動く)
    future->metallic = 0.5f;
    const auto again = json::Parse(WriteMaterial(*future, &unknowns));
    CHECK(again && Near(static_cast<float>(again->Get("sheen").AsDouble()), 0.3f));
    CHECK(again && again->Get("alpha").AsString() == "Glass" && Near(static_cast<float>(again->Get("metallic").AsDouble()), 0.5f));
    // マテリアルでないもの。
    CHECK(!ReadMaterial("{\"kind\": \"rm_engine.particle_effect\"}", &err) && !err.empty());
    CHECK(!ReadMaterial("{ not json", &err));
}

RM_TEST(Material_BakedPicturesRideAlong) {
    Material m;
    m.colorImage.guid = "0123456789abcdef0123456789abcdef";
    const std::string text = WriteMaterial(m);
    std::vector<BakedMaterialPicture> pictures(4);
    pictures[0] = {"Baked/abc/0.dds", 3};
    const std::string baked = WriteBakedMaterial(text, pictures);
    const auto read = ReadBakedPictures(baked);
    CHECK(read.size() == 4 && read[0].path == "Baked/abc/0.dds" && read[0].sampling == 3 && read[1].path.empty());
    std::string err;
    const auto still = ReadMaterial(baked, &err);  // (焼いたファイルもマテリアルとして読める)
    CHECK(still && still->colorImage == m.colorImage);
    CHECK(ReadBakedPictures(text).empty());  // (焼いていないファイル)
}

RM_TEST(Material_OldMeshRendererStillLoads) {
    // マテリアルの欄ができる前のシーンのメッシュ: そのまま読め、欄は空 (形は灰色のまま)。
    const auto old = json::Parse(R"({"shape": "Box", "model": "", "castShadows": false})");
    CHECK(old.has_value());
    MeshRenderer m;
    reflect::Unknowns u;
    reflect::Load(m, *old, &u);
    CHECK(m.shape == MeshShape::Box && !m.castShadows && m.materials.empty() && u.Empty());
    // 欄は id の並びで保存する。
    m.materials = {AssetRef{"0123456789abcdef0123456789abcdef"}, AssetRef{}};
    const json::Value j = reflect::Save(m);
    CHECK(j.Get("materials").IsArray() && j.Get("materials").Size() == 2 && j.Get("materials").At(0).AsString() == m.materials[0].guid);
    MeshRenderer back;
    reflect::Load(back, j);
    CHECK(back.materials.size() == 2 && back.materials[0] == m.materials[0] && back.materials[1].Empty());
    // 欄はマテリアルのファイルだけを受ける。
    const reflect::FieldDesc* f = FieldNamed(reflect::TypeOf<MeshRenderer>(), "materials");
    CHECK(f && f->assetKinds && std::string_view(f->assetKinds) == kMaterialExtension);
    CHECK(f && asset::KindMatches(asset::Kind::Other, f->assetKinds, "Materials/Red.rmmat"));
    CHECK(f && !asset::KindMatches(asset::Kind::Image, f->assetKinds, "Images/red.png"));
}
