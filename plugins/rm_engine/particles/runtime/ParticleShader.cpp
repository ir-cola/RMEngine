#include "rm_engine/particles/runtime/ParticleShader.h"

#include <algorithm>
#include <array>
#include <format>

namespace rm::particles {

namespace {

// The standard values of a particle (88 bytes; a structured buffer packs them as written).
constexpr char kParticle[] = R"(struct Particle {
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
    float4 color;
    float4 startColor;
    float size;
    float startSize;
    float rotation;
    float spin;
    uint seed;
    uint alive;
)";
constexpr uint32_t kStandardBytes = 88;

// What every module may use (above the modules).
constexpr char kCommon[] = R"(
cbuffer EmitterCB : register(b0) {
    float4x4 EmitterWorld;
    float3 EmitterPosition;
    float Time;
    float DeltaTime;
    uint SpawnStart;
    uint SpawnCount;
    uint Capacity;
    uint SpawnSerial;
    float EmitterAge;
    uint DotShape;
    float EmitterPad_;
};

cbuffer ViewCB : register(b2) {
    float4x4 ViewProj;
    float3 CamRight;
    float ViewPad0_;
    float3 CamUp;
    float ViewPad1_;
};

uint Hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

struct Random {
    uint state;
};

// 0 (included) .. 1 (not included)
float Rand(inout Random r) {
    r.state = Hash(r.state + 0x9e3779b9u);
    return (r.state & 0xFFFFFFu) / 16777216.0;
}
float RandRange(inout Random r, float a, float b) { return lerp(a, b, Rand(r)); }
float2 RandInDisc(inout Random r) {
    float a = Rand(r) * 6.2831853;
    return float2(cos(a), sin(a)) * sqrt(Rand(r));
}
float3 RandOnSphere(inout Random r) {
    float z = Rand(r) * 2 - 1;
    float a = Rand(r) * 6.2831853;
    float s = sqrt(max(0, 1 - z * z));
    return float3(s * cos(a), s * sin(a), z);
}
float3 RandInSphere(inout Random r) { return RandOnSphere(r) * pow(Rand(r), 1.0 / 3.0); }

// A point / a direction of the emitter's own space in the world.
float3 EmitterPoint(float3 local) { return mul(EmitterWorld, float4(local, 1)).xyz; }
float3 EmitterDirection(float3 local) {
    float3 d = mul(EmitterWorld, float4(local, 0)).xyz;
    float n = dot(d, d);
    return n > 1e-12 ? d * rsqrt(n) : float3(0, 1, 0);
}
// 0 when born .. 1 at the end of its life
float LifeRatio(Particle p) { return saturate(p.age / max(p.lifetime, 1e-6)); }
)";

constexpr char kKernelsHead[] = R"(
RWStructuredBuffer<Particle> Particles : register(u0);

[numthreads(64, 1, 1)]
void SpawnMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= SpawnCount) return;
    LoadParams();
    Particle p = (Particle)0;
    p.position = EmitterPosition;
    p.lifetime = 1;
    p.color = float4(1, 1, 1, 1);
    p.startColor = p.color;
    p.size = 0.1;
    p.startSize = p.size;
    p.alive = 1;
    p.seed = Hash(SpawnSerial + id.x);
    Random rnd;
    rnd.state = p.seed;
)";

constexpr char kUpdateHead[] = R"(    Particles[(SpawnStart + id.x) % Capacity] = p;
}

[numthreads(64, 1, 1)]
void UpdateMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Capacity) return;
    Particle p = Particles[id.x];
    if (p.alive == 0) return;
    LoadParams();
    float dt = DeltaTime;
    Random rnd;
    rnd.state = Hash(p.seed ^ asuint(p.age) ^ asuint(Time));
)";

constexpr char kDrawing[] = R"(    if (p.alive != 0) {
        p.position += p.velocity * dt;
        p.rotation += p.spin * dt;
        p.age += dt;
        if (p.age >= p.lifetime) p.alive = 0;
    }
    Particles[id.x] = p;
}

StructuredBuffer<Particle> Drawn : register(t0);

struct ParticleOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

ParticleOut ParticleVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    ParticleOut o;
    Particle p = Drawn[instance];
    if (p.alive == 0 || !(p.size > 0)) {
        o.position = float4(0, 0, 2, 1);  // outside the picture: nothing drawn
        o.color = 0;
        o.uv = 0;
        return o;
    }
    const float2 corners[6] = {float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1)};
    float2 c = corners[vertex];
    float s, k;
    sincos(p.rotation, s, k);
    float2 r = float2(c.x * k - c.y * s, c.x * s + c.y * k) * (p.size * 0.5);
    float3 world = p.position + CamRight * r.x + CamUp * r.y;
    o.position = mul(ViewProj, float4(world, 1));
    o.color = p.color;
    o.uv = c;
    return o;
}

float4 ParticlePS(ParticleOut i) : SV_Target {
    float d2 = dot(i.uv, i.uv);
    float a = 1;
    if (DotShape == 0) {
        a = saturate(1 - d2);
        a *= a;
    } else if (DotShape == 1) {
        float w = max(fwidth(d2), 1e-4);
        a = saturate((1 - d2) / w);
    }
    return float4(i.color.rgb, saturate(i.color.a * a));
}
)";

// The value of `param` in a module's values (or its default).
std::array<float, 4> ValueOf(const ModuleParam& param, std::span<const ParticleValue> values) {
    for (const ParticleValue& v : values) {
        if (v.name == param.name) return {v.x, v.y, v.z, v.w};
    }
    return param.value;
}

const char* HlslType(ParamType t) {
    switch (t) {
        case ParamType::Float: return "float";
        case ParamType::Float2: return "float2";
        case ParamType::Float3: return "float3";
        case ParamType::Float4:
        case ParamType::Color: return "float4";
        case ParamType::Int: return "int";
        case ParamType::Bool: return "bool";
    }
    return "float";
}

std::string Load(const ModuleParam& p, uint32_t slot) {
    switch (p.type) {
        case ParamType::Float: return std::format("P[{}].x", slot);
        case ParamType::Float2: return std::format("P[{}].xy", slot);
        case ParamType::Float3: return std::format("P[{}].xyz", slot);
        case ParamType::Float4:
        case ParamType::Color: return std::format("P[{}]", slot);
        case ParamType::Int: return std::format("(int)round(P[{}].x)", slot);
        case ParamType::Bool: return std::format("(P[{}].x != 0)", slot);
    }
    return "0";
}

uint32_t AttrBytes(const std::string& type) {
    if (type == "float2") return 8;
    if (type == "float3") return 12;
    if (type == "float4") return 16;
    return 4;
}

}  // namespace

EmitterShader AssembleShader(std::span<const ModuleInput> modules) {
    EmitterShader out;
    // The particle: the standard values, then each attr once (two modules may share one of one type).
    std::string attrs;
    std::vector<std::pair<std::string, std::string>> seen;
    out.stride = kStandardBytes;
    for (const ModuleInput& m : modules) {
        if (!m.source) continue;
        for (const ModuleAttr& a : m.source->attrs) {
            bool same = false;
            for (const auto& [name, type] : seen) {
                if (name != a.name) continue;
                same = true;
                if (type != a.type) out.errors.push_back({a.line, ModuleErrorKind::SameName, a.name});
            }
            if (same) continue;
            seen.emplace_back(a.name, a.type);
            attrs += std::format("    {} {};\n", a.type, a.name);
            out.stride += AttrBytes(a.type);
        }
    }
    std::string& h = out.hlsl;
    h += "#line 1 \"particles\"\n";
    h += kParticle;
    h += attrs;
    h += "};\n";
    h += kCommon;
    uint32_t slot = 0;
    for (const ModuleInput& m : modules) {
        if (m.source) slot += static_cast<uint32_t>(m.source->params.size());
    }
    out.paramSlots = std::max<uint32_t>(1, slot);
    h += std::format("\ncbuffer ParamsCB : register(b1) {{\n    float4 P[{}];\n}};\n", out.paramSlots);
    // Each module in its namespace: its params (set from P by LoadParams), then its code from line 1.
    std::string load = "\nvoid LoadParams() {\n";
    slot = 0;
    for (size_t i = 0; i < modules.size(); ++i) {
        const ModuleInput& m = modules[i];
        if (!m.source) continue;
        h += std::format("\nnamespace M{} {{\n", i);
        for (const ModuleParam& p : m.source->params) {
            h += std::format("static {} {};\n", HlslType(p.type), p.name);
            load += std::format("    M{}::{} = {};\n", i, p.name, Load(p, slot++));
        }
        std::string file = m.file;
        for (char& c : file) {
            if (c == '"' || c == '\\') c = '/';
        }
        h += std::format("#line 1 \"{}\"\n", file);
        h += m.source->code;
        h += std::format("\n}}\n#line 1 \"particles-{}\"\n", i);
    }
    load += "}\n";
    h += load;
    h += kKernelsHead;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].source && modules[i].source->hasSpawn) h += std::format("    M{}::Spawn(p, rnd);\n", i);
    }
    h += kUpdateHead;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].source && modules[i].source->hasUpdate) h += std::format("    if (p.alive != 0) M{}::Update(p, dt, rnd);\n", i);
    }
    h += kDrawing;
    return out;
}

std::vector<float> PackParams(std::span<const ModuleInput> modules) {
    std::vector<float> out;
    for (const ModuleInput& m : modules) {
        if (!m.source) continue;
        for (const ModuleParam& p : m.source->params) {
            const std::array<float, 4> v = ValueOf(p, m.values);
            out.insert(out.end(), v.begin(), v.end());
        }
    }
    if (out.empty()) out.assign(4, 0.f);
    return out;
}

}  // namespace rm::particles
