#pragma once
// The renderer's shaders (HLSL text, compiled by Renderer3D.cpp; tested by tests/TestRendererShaders.cpp):
// the lines, the models (MeshVS / MeshPS), the floor's shadows, the shadow maps - and what plugins
// may join:
//
//   向きの地図   RenderFrame::mapDirections / mapShapes: the depth of shapes that are not drawn (天候: the
//                colliders) seen along a direction, read with MapPast (kDirectionMapHlsl: a plugin's
//                own shaders put it in front of theirs - an ExtraDraw gets its cbuffer and texture)
//   面の塗り     RenderFrame::surface: a plugin's HLSL that touches every surface before it is lit
//                (天候: 濡れ) - its `Surface` function and its numbers (cbuffer SurfaceData, b5)

#include <cstdint>
#include <string>
#include <string_view>

namespace rm::space3d {

// 向きの地図: at most this many in a frame.
inline constexpr uint32_t kDirectionMaps = 8;
// The cbuffer DirectionMaps (b6) as the GPU reads it.
struct DirectionMapConstants {
    float viewProj[kDirectionMaps][16];
    float info[kDirectionMaps][4];  // x: 1 = there is one; y, z: its tile's left, top (pixels); w: its depth range (world)
    float size[4];                  // x: a map's size (pixels); y: how many
};
static_assert(sizeof(DirectionMapConstants) % 16 == 0);
// The cbuffer SurfaceData (b5): this many float4s.
inline constexpr uint32_t kSurfaceData = 16;

// The cbuffer, the texture (t8) and MapPast / MapPastAt.
extern const char kDirectionMapHlsl[];
// The renderer's whole text: ToScreen / ToLinear, its shaders, the maps' part, and `surface` - a
// plugin's code defining Surface(...) (empty: none, the surface as it is).
std::string RendererShaderText(std::string_view surface = {});

}  // namespace rm::space3d
