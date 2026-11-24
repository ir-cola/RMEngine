#pragma once
// ゲーム制作 × 3D - マテリアル (.rmmat): メッシュの表面の見た目 - 色・色の絵・金属・ラフネス・
// 法線の絵・発光・透明の扱い・絵の繰り返しとずらし。1 つのファイルが 1 つのマテリアル。
// メッシュの部品 (MeshRenderer::materials) が指す: 形 (箱・球 …) は 0 番、モデルは i 番が
// モデル自身の i 番の材質の代わり (空の欄: モデルのまま)。
//
// ファイルはこの型をそのまま JSON にしたもの ({"kind": "rm_engine.material", "format": 1, "color": …}。
// 知らないキーは捨てずに書き戻す: 憲法 6)。エディター (アセットのパネルで選ぶとインスペクターで
// 編集できる: 核の editor::AssetFileType) もゲームも、この型と MaterialLook を通る (描き方は 1 本)。
//
// 色の決まり: 「色」「発光の色」は画面で選んだ色 (sRGB) として持ち、描くときに線形の光に直す
// (space3d::ScreenToLinear。OBJ の .mtl の色と同じ扱い: 選んだ色が光の当たった所でそのまま見える)。
// 発光は さらに「発光の強さ」を掛ける (1 を越えてよい)。アルファは直さない (透け具合そのもの)。
// 光の部品の色は前と同じく線形の光の量のまま (Light.h)。
//
// 純粋 (ファイルと GPU には触らない) - tests/TestMaterial.cpp でテストする。

#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Texture.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::game_x_3d {

inline constexpr char kMaterialExtension[] = ".rmmat";
inline constexpr char kMaterialKind[] = "rm_engine.material";

RM_ENUM(Label="透明の扱い")
enum class MaterialAlpha {
    RM_ITEM(Label="不透明") Opaque,
    RM_ITEM(Label="切り抜き") Cutout,
    RM_ITEM(Label="半透明") Blend,
};

RM_STRUCT(Label="マテリアル")
struct Material {
    RM_PROPERTY() std::string kind = kMaterialKind;  // ファイルの種類 (インスペクターには出ない)
    RM_PROPERTY() int format = 1;
    RM_PROPERTY(Edit, Label="色", Tooltip="表面の色。画面で選んだ色のとおりに見えます。色の絵があれば、絵に掛けます。アルファは「半透明」のときの透け具合", Min=0, Max=1)
    Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="色の絵", Tooltip="表面に貼る画像 (テクスチャ)。アセットのパネルからドラッグするか、クリックして選びます", AssetKind="image")
    AssetRef colorImage;
    RM_PROPERTY(Edit, Label="金属", Tooltip="0: 金属ではない (プラスチック・木・石) / 1: 金属 (周りを映し、色がつやの色になる)", Min=0, Max=1, Step=0.01)
    float metallic = 0.f;
    RM_PROPERTY(Edit, Label="ラフネス", Tooltip="表面のざらざら。0: 鏡のようにつるつる / 1: つやがない", Min=0, Max=1, Step=0.01)
    float roughness = 0.8f;
    RM_PROPERTY(Edit, Label="金属・ラフネスの絵", Tooltip="青のチャンネルが金属、緑のチャンネルがラフネス (glTF と同じ)。上の 2 つの数に掛けます", AssetKind="image")
    AssetRef metalRoughImage;
    RM_PROPERTY(Edit, Label="法線の絵", Tooltip="表面の細かいでこぼこ (ノーマルマップ。OpenGL の向き: 緑が上)", AssetKind="image")
    AssetRef normalImage;
    RM_PROPERTY(Edit, Label="法線の強さ", Tooltip="法線の絵のでこぼこをどれだけ強く見せるか (0: なし)", Min=0, Max=4, Step=0.01)
    float normalScale = 1.f;
    RM_PROPERTY(Edit, Label="発光の色", Tooltip="自分で光る色 (光が当たらなくても見える)。黒: 光らない", Min=0, Max=1)
    Color emission{0, 0, 0, 1};
    RM_PROPERTY(Edit, Label="発光の強さ", Tooltip="発光の色に掛ける数 (1 より大きくするとより明るく光る)", Min=0, Max=100, Step=0.1)
    float emissionStrength = 1.f;
    RM_PROPERTY(Edit, Label="発光の絵", Tooltip="光る所と色の画像。発光の色に掛けます", AssetKind="image")
    AssetRef emissionImage;
    RM_PROPERTY(Edit, Label="透明の扱い", Tooltip="不透明: 絵のアルファを使わない / 切り抜き: アルファがしきい値より小さい所は穴 (葉っぱ・金網) / 半透明: アルファで透ける (ガラス・水)")
    MaterialAlpha alpha = MaterialAlpha::Opaque;
    RM_PROPERTY(Edit, Label="切り抜きのしきい値", Tooltip="色のアルファ (色 x 色の絵) がこれより小さい所を描きません", Min=0, Max=1, Step=0.01, ShowIf="alpha=Cutout")
    float alphaCutoff = 0.5f;
    RM_PROPERTY(Edit, Label="絵の繰り返し", Tooltip="絵を横 (X)・縦 (Y) に何回並べるか。10 x 10 の平面なら 10, 10 で 1 m ごとに 1 枚", Min=-1000, Max=1000, Step=0.1)
    Vec2 tiling{1, 1};
    RM_PROPERTY(Edit, Label="絵のずらし", Tooltip="絵をどれだけずらすか (1 で絵 1 枚分)", Min=-1000, Max=1000, Step=0.01)
    Vec2 offset{0, 0};
};

// .rmmat の中身 -> マテリアル。マテリアルのファイルでなければ nullopt と *error (日本語)。
// この版の知らないキーは `unknowns` に (WriteMaterial が書き戻す)。
std::optional<Material> ReadMaterial(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns = nullptr);
std::string WriteMaterial(const Material& m, const reflect::Unknowns* unknowns = nullptr);

// マテリアルの 1 枚の絵: どの画像を、どのスロット (space3d::SlotKind: 0 色、1 金属 / ラフネス、
// 2 法線、3 発光) に、どう読むか (色の絵か・何に使うか)。
struct MaterialPicture {
    AssetRef image;
    bool srgb = true;
    space3d::TextureUse use = space3d::TextureUse::Color;
};
std::array<MaterialPicture, space3d::kPictureSlots> MaterialPictures(const Material& m);

// マテリアルを描く形にする唯一の道 (エディターのシーンビュー・サムネイル・ゲーム): 数 (色は線形へ、
// 発光は強さを掛けて、透明の扱い、絵の繰り返し) と、スロットごとに `picture` が返す絵 (null: なし。
// `picture` が空: 絵なし)。エディターはテクスチャのキャッシュから、ゲームは焼いた DDS から絵を渡す。
using MaterialPictureSource = std::function<space3d::TextureRef(const MaterialPicture& picture, size_t slot)>;
space3d::DrawMaterial MaterialLook(const Material& m, const MaterialPictureSource& picture = {});

// メッシュの部品が描く材質。`slots`: MeshRenderer::materials の順に、読めたマテリアル (null: 空の欄、
// 見つからない、読めない)。
//   形 (`shape`): 0 番の欄だけ。無ければ空 (= 白 x 描く色: これまでの灰色の形)。
//   モデル: モデル自身の材質 `own` の i 番を、欄 i があればそれに替える (空の欄はモデルのまま)。
//           モデルに材質が無くても 0 番の欄は使える (UseSlotZeroForBareModel)。own より多い欄は使わない。
// 何も替えないときは `own` をそのまま返す (`store` は使わない)。替えるときは `store` に作り、それを返す。
std::span<const space3d::DrawMaterial> MeshMaterials(bool shape, std::span<const space3d::DrawMaterial> own,
                                                     std::span<const space3d::DrawMaterial* const> slots,
                                                     std::vector<space3d::DrawMaterial>& store);

// 材質の無いモデル (OBJ だけ、など): その部品が 0 番の材質を使うようにする (0 番は無いので白のまま
// 描かれ、メッシュの 0 番のマテリアルを入れるとそれで塗る)。GPU に置く前に呼ぶ。
void UseSlotZeroForBareModel(space3d::MeshData& mesh);

// 書き出したゲームの .rmmat: 元のファイルに、焼いた絵 (スロットの順に 4 つ。path が空: なし) を
// "baked" として足したもの (game_x_3d/editor/ExportBake.cpp が作る)。
RM_STRUCT()
struct BakedMaterialPicture {
    RM_PROPERTY() std::string path;  // ゲームの中のパス ("Baked/<id>/0.dds")
    RM_PROPERTY() int sampling = 0;  // space3d::PackSampling (画像の取り込みの設定の読み方)
};
RM_STRUCT()
struct BakedMaterialPictures {
    RM_PROPERTY() std::vector<BakedMaterialPicture> baked;
};
// `bytes` (.rmmat の中身) に "baked" を足す / 読む (無い・読めない: 空)。
std::string WriteBakedMaterial(std::string_view bytes, const std::vector<BakedMaterialPicture>& pictures);
std::vector<BakedMaterialPicture> ReadBakedPictures(std::string_view bytes);

}  // namespace rm::game_x_3d
