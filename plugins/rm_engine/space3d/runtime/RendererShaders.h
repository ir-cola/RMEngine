#pragma once
// レンダラーのシェーダー (HLSL のテキスト。Renderer3D.cpp がコンパイルする。
// tests/TestRendererShaders.cpp でテストする):
// 線、モデル (MeshVS / MeshPS。骨のあるモデルは SkinnedMeshVS / SkinnedShadowVS: 式は Skinning.h)、
// 床の影、シャドウマップ - と、プラグインが加われるもの:
//
//   向きの地図   RenderFrame::mapDirections / mapShapes: 描かれない形 (天候: コライダー) の深度を
//                ある向きから見たもの。MapPast で読む (kDirectionMapHlsl: プラグイン自身の
//                シェーダーが自分のものの前に置く - ExtraDraw はその cbuffer とテクスチャを受け取る)
//   面の塗り     RenderFrame::surface: 光を当てる前に全部の面に触るプラグインの HLSL
//                (天候: 濡れ) - その `Surface` 関数と数値 (cbuffer SurfaceData, b5)

#include <cstdint>
#include <string>
#include <string_view>

namespace rm::space3d {

// 向きの地図: 1 フレームにこの数まで。
inline constexpr uint32_t kDirectionMaps = 8;
// cbuffer DirectionMaps (b6) を GPU が読む形にしたもの。
struct DirectionMapConstants {
    float viewProj[kDirectionMaps][16];
    float info[kDirectionMaps][4];  // x: 1 = ある、y, z: そのタイルの左、上 (ピクセル)、w: 深度の範囲 (ワールド)
    float size[4];                  // x: 地図 1 枚の大きさ (ピクセル)、y: 数
};
static_assert(sizeof(DirectionMapConstants) % 16 == 0);
// cbuffer SurfaceData (b5): この数の float4。
inline constexpr uint32_t kSurfaceData = 16;

// cbuffer、テクスチャ (t8)、MapPast / MapPastAt。
extern const char kDirectionMapHlsl[];
// レンダラーのテキスト全体: ToScreen / ToLinear、そのシェーダー、地図の部分、そして `surface` -
// Surface(...) を定義するプラグインのコード (空: 無し。面はそのまま)。
std::string RendererShaderText(std::string_view surface = {});

}  // namespace rm::space3d
