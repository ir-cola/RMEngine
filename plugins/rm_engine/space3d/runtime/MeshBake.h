#pragma once
// 3D 空間 - 書き出すゲーム用に「焼いた」モデル (設計 RME_PLATFORM 5-a: ゲーム用の形に変える。
// glTF / OBJ / FBX のリーダーはゲームには入れない)。書き出しは、モデルをエディターとまったく同じに
// 読み (LoadMeshFile: 取り込みの設定の大きさと上の軸を適用)、その結果をこのエンジン独自の形で
// 書く。ゲームはそれをそのまま GPU に載せる (Renderer::Upload)。純粋 - tests/TestSpace3d.cpp で
// テストする (書いて読み戻すと同じになる)。
//
//   "RMMESH\0\0"  format u32                                     その後にチャンク。各チャンクは tag (4) + size u64 + バイト列
//   BNDS  min, max (float 6 個)
//   VERT  count u32、続いて頂点ごとに position / normal / uv (float 8 個)
//   INDX  count u32、続いてインデックスごとに u32
//   PART  count u32、続いてパーツごとに firstIndex、indexCount u32、material i32
//   MATL  count u32、続いてマテリアルごとに: name、baseColor (4)、alpha u8、alphaCutoff、metallic、roughness、
//         normalScale、emissive (4)、4 枚の絵 (色、金属 / ラフネス、法線、発光) を
//         書き出したゲームのファイルのパスで (空: なし) - 絵は別に焼く (DDS)
//   SHRP  マテリアルごとに 4 バイト: 各絵を最近傍のテクセルで描くか (くっきり: その取り込みの設定)
// このバージョンが知らないタグのチャンクは飛ばす (キーの扱い。バイナリデータについての設計の決まりと同じ)。

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/TextureSampling.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

inline constexpr uint32_t kBakedMeshFormat = 1;

// マテリアルの 4 枚の絵 (Texture.h のスロット: 色、金属 / ラフネス、法線、発光) をパスで、と、
// それぞれの読み方 (その取り込みの設定: フィルタ・端の扱い・ストリーミング - 各 1 バイト、PackSampling)。
struct BakedPictures {
    std::array<std::string, 4> path;
    std::array<TextureSampling, 4> sampling{};
};

// `pictures`: `mesh` のマテリアルごとに 1 つ (足りない分: なし)。
std::string WriteBakedMesh(const MeshData& mesh, const std::vector<BakedPictures>& pictures);

// メッシュと、そのマテリアルの絵を、`file` がパスである画像として渡したもの (パスごとに画像 1 つ)。
// `samplingOfImage` (省略可) には、画像ごとの読み方が入る。焼いたメッシュでない、新しい形式、
// または壊れているときは nullopt と *error (日本語)。
std::optional<MeshData> ReadBakedMesh(std::string_view bytes, std::string* error, std::vector<TextureSampling>* samplingOfImage = nullptr);

}  // namespace rm::space3d
