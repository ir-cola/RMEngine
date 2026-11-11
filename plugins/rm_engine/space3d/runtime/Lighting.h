#pragma once
// 3D 空間: the lights of a frame, and the numbers the GPU reads for them (Renderer3D's constant
// buffer `Lights`, register b1). Pure - tested in tests/TestSpace3d.cpp.
//
//   std::vector<space3d::Light> lights = ...;   // from the scene (the game's 光 components)
//   const space3d::LightSetup s = space3d::MakeLights(lights, defaultDirection);
//   // s.block -> the GPU; s.shadowLight: which one's shadows the shadow map holds (-1: none)
//
// No light at all: one white directional light of strength 1 going `defaultDirection` (the look of
// the scene before there were lights), casting shadows.
// At most kMaxLights lights are drawn (directional ones included); the rest are left out (`dropped`).
// Only one light has shadows: the first directional light that casts them (the shadow map is one
// orthographic picture). Point lights and spots never cast shadows (yet).
//
// How much light a point gets (the shader, Renderer3D.cpp's LightAmount):
//   the sky (every face, a little more facing up) + Σ 0.58 × colour × strength × cos(angle) × fade × shadow
// fade: 1 for a directional light; (1 - distance / range)² for a point light, and for a spot also
// smoothly from the cone's edge to 3/4 of its half angle.

#include "base/Math.h"

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
    bool castShadow = true;    // directional only
};

inline constexpr size_t kMaxLights = 8;

// The constant buffer `Lights` as the GPU reads it (float4s, HLSL's packing).
struct LightBlock {
    float info[4];                   // x: how many lights
    float position[kMaxLights][4];   // xyz: where (point, spot); w: the type (0 directional, 1 point, 2 spot)
    float direction[kMaxLights][4];  // xyz: where the light goes (length 1); w: the range
    float color[kMaxLights][4];      // rgb: colour × strength; w: 1 = its shadows are in the shadow map
    float spot[kMaxLights][4];       // x: cos of the cone's edge (half angle), y: cos where it starts to fade
};
static_assert(sizeof(LightBlock) % 16 == 0);

struct LightSetup {
    LightBlock block{};
    int shadowLight = -1;            // index in block (-1: no light casts shadows)
    Vec3 shadowDirection{0, -1, 0};  // that light's direction (length 1)
    size_t dropped = 0;              // lights left out (more than kMaxLights)
};

LightSetup MakeLights(std::span<const Light> lights, Vec3 defaultDirection);

}  // namespace rm::space3d
