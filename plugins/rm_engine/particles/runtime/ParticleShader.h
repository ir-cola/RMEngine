#pragma once
// パーティクル - one emitter's shaders, put together from its modules (pure text: tested in
// tests/TestParticles.cpp, the result compiled there too).
//
//   struct Particle { the standard values ..., every module's attrs }
//   namespace M0 { static <params>; <module 0's code> }    each module in its own namespace: its names
//   namespace M1 { ... }                                   never meet another's (two "first"s are fine)
//   SpawnMain   (cs_5_0)  a new particle: the standard start, then every module's Spawn in order
//   UpdateMain  (cs_5_0)  every module's Update in order, then the engine moves it (velocity, spin), ages it
//                         and keeps its trail (TrailPoints places, a ring)
//   ParticleVS / ParticlePS  a square facing the camera (or long along its velocity: Stretch), the dot's
//                         shape, its colour
//   TrailVS / TrailPS     a band through the trail's places, thinner and fainter to its end
//
// Modules may call the engine's CollideShapes (the colliders' shapes: CollisionShape) and CollideDepth (the
// picture's depth) - the built-in 当たる modules do.
//
// A module's params live in one constant buffer (one float4 per param, in order): changing a value only
// changes the buffer (PackParams) - the shaders are made again only when the modules change.

#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rm::particles {

struct ModuleInput {
    const ModuleSource* source = nullptr;
    std::string file;  // for the compiler's messages ("Wind.rmfx", "initial")
    std::span<const ParticleValue> values;
};

struct EmitterShader {
    std::string hlsl;
    uint32_t stride = 0;      // bytes of one Particle
    uint32_t paramSlots = 0;  // float4s in cbuffer Params (at least 1)
    std::vector<ModuleError> errors;  // two attrs of one name and different types
};

EmitterShader AssembleShader(std::span<const ModuleInput> modules);
// cbuffer Params: 4 floats per param, in the modules' order (a value not given: the param's default).
std::vector<float> PackParams(std::span<const ModuleInput> modules);

inline constexpr uint32_t kThreads = 64;  // [numthreads] of both kernels

// The constant buffers, as the shaders read them (b0 / b2).
struct EmitterConstants {
    float world[16];     // the emitter's world matrix (column-major, as space3d's Mat4)
    float position[3];
    float time;          // seconds since the effect started
    float dt;
    uint32_t spawnStart;  // the first slot new particles go into
    uint32_t spawnCount;
    uint32_t capacity;
    uint32_t spawnSerial;  // how many were born before (the random seeds)
    float age;             // seconds into this round (duration)
    uint32_t shape;        // ParticleShape
    float pad = 0;
    uint32_t trailPoints;  // 0: no trail
    float trailInterval;   // seconds between two of its places
    float trailWidth;      // times the particle's size
    float stretch;         // seconds: a particle is long by its speed times this (0: a square)
    uint32_t hasImage;     // the dots show the emitter's picture
    uint32_t sheetCols, sheetRows;  // its frames
    uint32_t sheetMode;    // ParticleSheet
};
static_assert(sizeof(EmitterConstants) == 144);

struct ViewConstants {
    float viewProj[16];  // column-major: mul(ViewProj, v)
    float right[3];      // the camera's right and up in the world (the squares face it)
    float pad0 = 0;
    float up[3];
    float pad1 = 0;
    float eye[3];  // where the camera is
    float pad2 = 0;
};
static_assert(sizeof(ViewConstants) == 112);

// A shape particles collide with (CollideShapes), in the world, as the shaders read it.
enum class CollisionType : uint32_t { Sphere = 0, Box = 1, Capsule = 2, Plane = 3 };
struct CollisionShape {
    float center[3];
    CollisionType type;
    float axisX[3];  // a box's axes (unit); a capsule's line goes along axisY; a plane's normal is axisY
    float sizeX;     // a box's half sizes; a sphere's / a capsule's radius
    float axisY[3];
    float sizeY;     // a capsule: half its line's length
    float axisZ[3];
    float sizeZ;
};
static_assert(sizeof(CollisionShape) == 64);

// What a picture's last frame drew (CollideDepth reads its depth).
struct CollideConstants {
    float viewProj[16];
    float inverse[16];  // clip -> world
    float eye[3];
    uint32_t count;     // shapes
    float size[2];      // pixels
    uint32_t depthMode;  // 0 none, 1 one sample, 2 multisampled
    float pad = 0;
};
static_assert(sizeof(CollideConstants) == 160);

}  // namespace rm::particles
