#pragma once
// 3D 空間: the lights of a frame and their shadows - the numbers the GPU reads for them (Renderer3D's
// constant buffers `Lights` (b1) and `Shadows` (b2)) and where the shadow maps look from.
// Pure - tested in tests/TestSpace3d.cpp.
//
//   std::vector<space3d::Light> lights = ...;   // from the scene (the game's 光 components)
//   const space3d::LightSetup s = space3d::MakeLights(lights, defaultDirection, ambient);
//   // s.block -> the GPU; s.shadowLight: whose shadows the sun's maps hold (-1: none);
//   // s.localViewProj[0 .. localSlices): the point / spot lights' shadow maps
//
// No light at all: one white directional light of strength 1 going `defaultDirection` (the look of
// the scene before there were lights), casting shadows.
// At most kMaxLights lights are drawn (directional ones included); the rest are left out (`dropped`).
// Which ones (and in which order the point / spot lights get shadow maps): the directional lights
// first (they light everything, in the order given), then the point / spot lights that matter most
// where the camera is - the brighter (colour x strength) and the nearer (how far the eye is outside
// its reach) first (LightWeight); equal ones in the order given.
//
// Shadows:
//   directional  the first directional light that casts them. Its maps are orthographic: one fitted
//                to everything that casts while that is small (radius up to kSingleMapRadius - it
//                does not change when the camera moves), otherwise kCascades maps split along what
//                the camera sees (cascades: near ones sharp, far ones coarse), up to kShadowDistance.
//   point        6 perspective maps (one per side of a cube around it: +X -X +Y -Y +Z -Z).
//   spot         1 perspective map through its cone.
//   Point lights and spots share kLocalShadowSlices maps, given in the lights' order: a light that
//   does not fit any more has no shadows (`shadowsLeftOut`).
//
// How much light a point gets (the shader, Renderer3D.cpp's Shade - physically based, linear):
//   ambient (the ground's colour facing down .. the sky's facing up, × its strength)
//   + Σ colour × strength × fade × shadow, as diffuse and GGX highlight by the material
// fade: 1 for a directional light; (1 - distance / range)² for a point light, and for a spot also
// smoothly from the cone's edge to 3/4 of its half angle.

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rm::space3d {

enum class LightType : uint8_t { Directional, Point, Spot };

struct Light {
    LightType type = LightType::Directional;
    Vec3 position{0, 0, 0};    // point / spot (world)
    Vec3 direction{0, -1, 0};  // where the light goes: directional / spot (any length)
    Color color{1, 1, 1, 1};   // alpha is not used
    float intensity = 1.f;     // the strength (0: off)
    float range = 10.f;        // point / spot: where it fades to nothing (world units)
    float spotAngle = 30.f;    // spot: the whole cone (degrees)
    bool castShadow = true;
};

// The light every face gets whatever the lights (環境光): the sky's colour on faces turned up, the
// ground's on faces turned down, between them in between (and its reflection on shiny faces). Linear,
// as the lights' colours. The default is the look before it could be set (0.16 up, 0.08 down).
struct Ambient {
    Color sky{0.16f, 0.16f, 0.16f, 1};
    Color ground{0.08f, 0.08f, 0.08f, 1};
    float intensity = 1.f;
};

inline constexpr size_t kMaxLights = 8;
inline constexpr size_t kLocalShadowSlices = 8;     // point / spot shadow maps (a point light takes 6)
inline constexpr uint32_t kLocalShadowSize = 1024;  // pixels of each
inline constexpr int kCascades = 3;
inline constexpr float kSingleMapRadius = 25.f;     // casters within a ball this big: one fitted map
inline constexpr float kShadowDistance = 150.f;     // cascades reach this far in front of the eye at most

// The constant buffer `Lights` as the GPU reads it (float4s, HLSL's packing).
struct LightBlock {
    float info[4];                   // x: how many lights
    float ambientSky[4];             // rgb: the ambient light from above (× its strength)
    float ambientGround[4];          // rgb: ... from below
    float position[kMaxLights][4];   // xyz: where (point, spot); w: the type (0 directional, 1 point, 2 spot)
    float direction[kMaxLights][4];  // xyz: where the light goes (length 1); w: the range
    float color[kMaxLights][4];      // rgb: colour × strength; w: 1 = its shadows are the sun's maps
    float spot[kMaxLights][4];       // x: cos of the cone's edge (half angle), y: cos where it starts to fade
    float shadow[kMaxLights][4];     // point / spot: x: its first shadow map (-1: none); y: how far to look off
                                     // the surface per unit of distance from the light (normal offset); z: the
                                     // depth bias at distance 1 (the shader divides it by the distance: the
                                     // maps' depth is not even)
};
static_assert(sizeof(LightBlock) % 16 == 0);

struct LightSetup {
    LightBlock block{};
    int shadowLight = -1;            // index in block of the light with the sun's maps (-1: none)
    Vec3 shadowDirection{0, -1, 0};  // that light's direction (length 1)
    size_t dropped = 0;              // lights left out (more than kMaxLights)
    // The point / spot lights' shadow maps: what each sees (world -> -1..1, depth 0..1).
    std::array<Mat4, kLocalShadowSlices> localViewProj{};
    size_t localSlices = 0;
    size_t shadowsLeftOut = 0;       // point / spot lights that wanted shadows but found no map free
};

LightSetup MakeLights(std::span<const Light> lights, Vec3 defaultDirection, const Ambient& ambient = {}, Vec3 eye = {0, 0, 0});
// How much a point / spot light matters to a camera at `eye`: its brightness (the colour's luminance
// x strength), less the farther the eye is outside its reach (1 / (1 + d²), d = distance - range).
float LightWeight(const Light& light, Vec3 eye);

// A spot light's shadow map: from where it is, through its cone (a little wider for the soft edge).
Mat4 SpotShadowMatrix(Vec3 position, Vec3 direction, float spotAngle, float range);
// A point light's 6 shadow maps, the sides of a cube: +X, -X, +Y, -Y, +Z, -Z (a little wider than
// 90 degrees, so the soft edges meet). The shader picks the side by the largest of |x| |y| |z|.
std::array<Mat4, 6> PointShadowMatrices(Vec3 position, float range);

// Where the sun's cascades end, as distances in front of the eye: between nearZ and farZ, closer
// together near the eye (a mix of even and evenly growing steps; the last is farZ).
std::array<float, kCascades> CascadeSplits(float nearZ, float farZ);

// The sun's shadow maps for a frame.
struct SunShadowPlan {
    int count = 0;                              // 0: nothing casts; 1: one map fitted to the casters; kCascades
    std::array<Mat4, kCascades> viewProj{};     // world -> map
    std::array<float, kCascades> splitFar{};    // where each ends, in front of the eye (the single map: huge)
    std::array<float, kCascades> texel{};       // one texel's width in the world
    std::array<float, kCascades> depthRange{};  // how deep each map is (world units)
};
// `direction`: where the light goes; `casters`: the box of what casts; `view` / `projection`: the
// camera; `mapSize`: pixels. Cascades are fitted to balls around their part of the view and moved
// in whole texels, so they do not shimmer when the camera moves.
SunShadowPlan PlanSunShadows(Vec3 direction, const Bounds& casters, const Mat4& view, const Mat4& projection, uint32_t mapSize);

}  // namespace rm::space3d
