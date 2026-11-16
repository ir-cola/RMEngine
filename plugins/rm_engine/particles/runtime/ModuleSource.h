#pragma once
// パーティクル - a module's source (.rmfx): HLSL with a few lines of its own that the engine reads. Pure
// (text in, data out): tested in tests/TestParticles.cpp.
//
//   module "風で曲げる"                                  its name in the パーティクル tab (else the file's)
//   param float  strength = 1.0 [0, 10];   // 強さ        a field of the module: type, name, default,
//   param float3 direction = (1, 0, 0);    // 向き        [min, max] if any, the label after //
//   param color  tint = (1, 0.5, 0, 1);    // 色
//   attr  float  heat;                                   a value every particle carries (p.heat)
//
//   void Spawn(inout Particle p, inout Random rnd) { ... }            once, when a particle is born
//   void Update(inout Particle p, float dt, inout Random rnd) { ... } every frame
//
// Types of a param: float float2 float3 float4 color (a float4 shown as a colour) int bool. In the code a
// param is used by its name (strength); an attr as a field of the particle (p.heat). The `module`,
// `param` and `attr` lines are taken out of the code (as empty lines: the compiler's line numbers stay
// the file's).

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rm::particles {

enum class ParamType { Float, Float2, Float3, Float4, Color, Int, Bool };

// How many numbers a param of this type is (1..4).
int Components(ParamType type);

struct ModuleParam {
    std::string name;
    ParamType type = ParamType::Float;
    std::array<float, 4> value{};  // the default
    std::string label;             // after //; empty: the name
    bool hasRange = false;
    float min = 0, max = 0;
    int line = 0;  // 1-based
};

struct ModuleAttr {
    std::string name;
    std::string type;  // float float2 float3 float4 int uint
    int line = 0;
};

// What is wrong with a line the engine reads (the editor words it through Tr; Message() for logs).
enum class ModuleErrorKind {
    BadLine,       // a param / attr / module line it cannot read
    UnknownType,   // detail: the type
    BadName,       // detail: the name (not a-z A-Z 0-9 _, or starts with a digit)
    SameName,      // detail: the name (two params / attrs of one name)
    BadValue,      // detail: what was written
    NoFunction,    // neither Spawn nor Update
};
struct ModuleError {
    int line = 0;  // 0: the whole file
    ModuleErrorKind kind = ModuleErrorKind::BadLine;
    std::string detail;
};
std::string Message(const ModuleError& e);  // Japanese (logs)

struct ModuleSource {
    std::string title;  // `module "..."` (empty: none)
    std::vector<ModuleParam> params;
    std::vector<ModuleAttr> attrs;
    bool hasSpawn = false, hasUpdate = false;
    std::string code;  // the HLSL left (the engine's lines blanked)
    std::vector<ModuleError> errors;
};

ModuleSource ParseModule(std::string_view text);

// A param of this name (null: none).
const ModuleParam* FindParam(const ModuleSource& m, std::string_view name);

}  // namespace rm::particles
