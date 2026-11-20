#pragma once
// 天候 - what falls (rain and snow) and 濡れ, as the GPU draws them (WeatherDraw.h): the numbers, and
// the HLSL - the drops and flakes (an ExtraDraw of their own) and the touch on every surface that makes
// what the rain reached look wet (3D 空間's RenderFrame::surface). Both read 3D 空間's 向きの地図 (the
// colliders seen the way they fall: RendererShaders.h's MapPast).

#include "base/Math.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rm::weather {

// Rain or snow falling around the eye: drops / flakes in a box that moves with the eye (each keeps its
// place in the world while it falls; out of one side, in at the other), drawn after the solid things
// with the depth test, each made by the vertex shader from its number (no buffer). Fainter near the eye
// and the box's edge.
struct Precipitation {
    enum class Kind : uint8_t { Rain, Snow };
    Kind kind = Kind::Rain;
    float amount = 0;              // 0 .. 1: none .. kMaxRaindrops / kMaxSnowflakes
    float seconds = 0;             // the time they fall by (still: they hang where they are)
    Color light{1, 1, 1, 1};       // what lights them (linear)
    float windX = 0, windZ = 0;    // the wind now (m/s, world)
    float driftX = 0, driftZ = 0;  // how far the wind has carried them (world units)
    int map = -1;                  // the 向きの地図 of the way they fall (-1: none - nothing to hit)
};
inline constexpr uint32_t kMaxRaindrops = 16000;
inline constexpr uint32_t kMaxSnowflakes = 10000;
inline constexpr float kRainBox = 24.f;  // the box's side (world units)
inline constexpr float kSnowBox = 18.f;
// The wind's drift starts again from 0 this far (world units): a multiple of both boxes (they look
// the same there), and far enough that the clouds' jump is seldom (a big storm: hours).
inline constexpr float kDriftWrap = kRainBox * kSnowBox * 400.f;
// How many are drawn for an amount (0 .. the most).
uint32_t PrecipitationCount(const Precipitation& p);

// How they fall: down at least `least` m/s (each drop / flake up to `spread` faster), and `carried` of
// the wind's speed sideways. The shader is given these.
struct FallSpeed {
    float least, spread, carried;
};
FallSpeed FallOf(Precipitation::Kind kind);
// The way they go on average in a wind (length 1): what they hit is seen from there.
Vec3 FallDirection(Precipitation::Kind kind, Vec3 wind);

// The drops' and flakes' shaders: FallVS / FallPS (vs_4_0 / ps_4_0), after 3D 空間's kColorHlsl and
// kDirectionMapHlsl. Their numbers: FallConstants (b4).
extern const char kFallHlsl[];
struct FallConstants {
    float viewProj[16];
    float eye[4];
    float info[4];   // x: 0 rain / 1 snow, y: seconds, z: the box's side, w: its 向きの地図 (-1: none)
    float light[4];  // rgb: what lights them (linear)
    float right[4];  // xyz: the camera's right (world)
    float up[4];     // xyz: the camera's up
    float wind[4];   // xy: the wind now (m/s, world x z); zw: how far it has carried them
    float fall[4];   // x: the least fall (m/s), y: how much faster each may be, z: how much of the wind takes it
};
static_assert(sizeof(FallConstants) % 16 == 0);

// 濡れ and 積雪: the touch on every surface (3D 空間's RenderFrame::surface: Surface(...)) and its numbers -
// the ways the rain and snow fell, each with the 向きの地図 it is seen by (`maps`, -1: none - everything
// open), how far it seeps under what covers and how deep its snow lies (float4 1 + k: sun, shade, map,
// seep; float4 1 + kWetWays + k: x snow).
extern const char kWetSurfaceHlsl[];
std::vector<float> WetSurfaceData(std::span<const WetWay> ways, std::span<const int> maps);

// 積雪 with its height (RenderFrame::solids): SnowLayerVS / SnowLayerPS, after
// RendererShaderText(kWetSurfaceHlsl) - a layer for each way with snow: a grid of kSnowCells x kSnowCells
// cells of kSnowCell across the way around the eye, drawn with an index buffer and no vertex buffer.
// 足跡: SnowTrailVS / SnowTrailPS (the same way): a picture of kTrailCells x kTrailCells cells of
// kTrailCell around the eye, drawn each frame from the last (two of them; R32G32B32A32_FLOAT).
extern const char kSnowLayerHlsl[];
extern const char kSnowTrailHlsl[];
inline constexpr uint32_t kSnowCells = 384;
inline constexpr float kSnowCell = 0.125f;  // (48 m)
inline constexpr uint32_t kTrailCells = 512;
inline constexpr float kTrailCell = 0.09375f;  // (48 m)
struct SnowLayerConstants {
    float across[4];   // xyz: one axis across the way; w: a cell (m)
    float along[4];    // xyz: the other; w: cells a side
    float way[4];      // xyz: the way it fell; w: this layer's way
    float middle[4];   // xyz: the grid's middle (the eye, in whole cells across)
    float snow[4][4];  // each way: x: its snow (m), y: its 向きの地図 (-1: none)
    float trail[4];    // the footprints: xy: the first cell (world x z); z: a cell (m); w: cells a side (0: none)
    float trailMore[4];  // x: which count of footprints
};
static_assert(sizeof(SnowLayerConstants) % 16 == 0);
struct SnowTrailConstants {
    float trail[4];  // as SnowLayerConstants::trail
    float more[4];   // x: which count; y: how much it fills up now (m); z: where the lines up start (world y); w: the map from below
};
static_assert(sizeof(SnowTrailConstants) % 16 == 0);
// The layer of way `layer` (of `ways`, seen by `maps`) around `eye`; footprints read when `trails`.
SnowLayerConstants SnowLayerOf(Vec3 eye, std::span<const WetWay> ways, std::span<const int> maps, size_t layer, uint32_t trailCount, bool trails);
// The footprints' frame: `fill` m filled up since the last; `upMap`: the map of the moving shapes from below.
SnowTrailConstants SnowTrailOf(Vec3 eye, uint32_t count, float fill, int upMap);

}  // namespace rm::weather
