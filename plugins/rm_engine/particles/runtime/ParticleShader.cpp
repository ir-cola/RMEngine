#include "rm_engine/particles/runtime/ParticleShader.h"

#include <algorithm>
#include <array>
#include <format>

namespace rm::particles {

namespace {

// 粒の標準の値 (96 バイト; 構造化バッファーは書いた通りに詰める)。
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
    uint trailHead;
    float trailTimer;
)";
constexpr uint32_t kStandardBytes = 96;

// どのモジュールも使えるもの (モジュールの上に置く)。
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
    uint TrailPoints;
    float TrailInterval;
    float TrailWidth;
    float Stretch;
    uint HasImage;   // 点は DotImage を見せる (でなければ DotShape)
    uint SheetCols;  // 絵のフレーム: 列と行
    uint SheetRows;
    uint SheetMode;  // 0: 生きている間にフレームを順に、1: 粒ごとに 1 フレーム
};

cbuffer ViewCB : register(b2) {
    float4x4 ViewProj;
    float3 CamRight;
    float ViewPad0_;
    float3 CamUp;
    float ViewPad1_;
    float3 CamPos;
    float ViewPad2_;
};

// 粒が当たる相手 (CollideShapes / CollideDepth)。
struct CollisionShape {
    float3 center;
    uint type;  // 0 球 (sizeX: 半径)、1 箱 (軸、半分の大きさ)、2 カプセル (axisY、sizeY: 線の半分、sizeX: 半径)、3 平面 (axisY: 法線)
    float3 axisX;
    float sizeX;
    float3 axisY;
    float sizeY;
    float3 axisZ;
    float sizeZ;
};
StructuredBuffer<CollisionShape> Colliders : register(t1);
Texture2D<float> SceneDepth : register(t2);
Texture2DMS<float> SceneDepthMS : register(t3);

cbuffer CollideCB : register(b3) {
    float4x4 DepthViewProj;  // 絵のカメラ (前のフレーム)
    float4x4 DepthInverse;   // そのクリップ空間 -> ワールド
    float3 DepthEye;
    uint ColliderCount;
    float2 DepthSize;  // ピクセル
    uint DepthMode;    // 0 無し、1 SceneDepth、2 SceneDepthMS
    float CollidePad_;
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

// 0 (含む) .. 1 (含まない)
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

// 点のなめらかなノイズ (0..1): 煙の見た目。
float Hash2(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float Noise2(float2 x) {
    float2 i = floor(x), f = frac(x);
    float2 u = f * f * (3 - 2 * f);
    return lerp(lerp(Hash2(i), Hash2(i + float2(1, 0)), u.x), lerp(Hash2(i + float2(0, 1)), Hash2(i + float2(1, 1)), u.x), u.y);
}

// エミッター自身の空間にある点 / 向きを、ワールドで表したもの。
float3 EmitterPoint(float3 local) { return mul(EmitterWorld, float4(local, 1)).xyz; }
float3 EmitterDirection(float3 local) {
    float3 d = mul(EmitterWorld, float4(local, 0)).xyz;
    float n = dot(d, d);
    return n > 1e-12 ? d * rsqrt(n) : float3(0, 1, 0);
}
// 生まれた時 0 .. 寿命の終わりで 1
float LifeRatio(Particle p) { return saturate(p.age / max(p.lifetime, 1e-6)); }

// 面 (法線 n) に向かう速度の成分を折り返し: その `bounce` の分を押し戻し、面に沿った成分を
// `friction` だけ遅くする。
void Bounce(inout Particle p, float3 n, float bounce, float friction) {
    float vn = dot(p.velocity, n);
    if (vn < 0) {
        float3 along = p.velocity - n * vn;
        p.velocity = along * (1 - friction) - n * vn * bounce;
    }
}

// q が形の外にどれだけ出ているか (中なら 0 より小さい) と、出る向き (n)。
float ShapeDistance(CollisionShape s, float3 q, out float3 n) {
    float3 d = q - s.center;
    if (s.type == 0) {
        float len = length(d);
        n = len > 1e-6 ? d / len : float3(0, 1, 0);
        return len - s.sizeX;
    }
    if (s.type == 1) {
        float3 l = float3(dot(d, s.axisX), dot(d, s.axisY), dot(d, s.axisZ));
        float3 o = abs(l) - float3(s.sizeX, s.sizeY, s.sizeZ);
        float3 out3 = max(o, 0);
        float outside = length(out3);
        float3 ln;
        if (outside > 0) {
            ln = out3 * sign(l) / outside;
        } else if (o.x > o.y && o.x > o.z) {
            ln = float3(l.x < 0 ? -1 : 1, 0, 0);
        } else if (o.y > o.z) {
            ln = float3(0, l.y < 0 ? -1 : 1, 0);
        } else {
            ln = float3(0, 0, l.z < 0 ? -1 : 1);
        }
        n = normalize(ln.x * s.axisX + ln.y * s.axisY + ln.z * s.axisZ);
        return outside + min(max(o.x, max(o.y, o.z)), 0);
    }
    if (s.type == 2) {
        float t = clamp(dot(d, s.axisY), -s.sizeY, s.sizeY);
        float3 e = d - s.axisY * t;
        float len = length(e);
        n = len > 1e-6 ? e / len : s.axisX;
        return len - s.sizeX;
    }
    n = s.axisY;
    return dot(d, s.axisY);
}

// 粒がコライダーの形で跳ね返る (`vanish` なら、触れたまま進む); `radius`: 粒自身の大きさを形に
// 足す。どれかに触れたら true。
bool CollideShapes(inout Particle p, float dt, float bounce, float friction, bool vanish, float radius) {
    bool hit = false;
    for (uint i = 0; i < ColliderCount; ++i) {
        float3 n;
        float dist = ShapeDistance(Colliders[i], p.position + p.velocity * dt, n) - radius;
        if (dist >= 0) continue;
        hit = true;
        if (vanish) {
            p.alive = 0;
            return true;
        }
        p.position += n * -dist;
        Bounce(p, n, bounce, friction);
    }
    return hit;
}

// ピクセルでの絵の深度 (0 近い .. 1 遠い) と、それが映すワールドの点。
float DepthAt(int2 px) {
    px = clamp(px, int2(0, 0), int2(DepthSize) - 1);
    return DepthMode == 2 ? SceneDepthMS.Load(px, 0) : SceneDepth.Load(int3(px, 0));
}
float3 DepthPoint(int2 px, float d) {
    float2 ndc = (float2(px) + 0.5) / DepthSize * float2(2, -2) + float2(-1, 1);
    float4 w = mul(DepthInverse, float4(ndc, d, 1));
    return w.xyz / w.w;
}

// 粒が、カメラに見えているもの - 絵の深度 - で跳ね返る (または触れたまま進む): 形はいらないが、
// 絵の外にあるものや隠れているものは無い。`thickness`: 面の裏へどれだけ深くまで入っても、まだ
// その面に触れているとみなすか (薄い壁: 小さく)。
bool CollideDepth(inout Particle p, float dt, float bounce, float friction, bool vanish, float thickness) {
    if (DepthMode == 0) return false;
    float3 q = p.position + p.velocity * dt;
    float4 c = mul(DepthViewProj, float4(q, 1));
    if (c.w <= 1e-4) return false;
    float3 ndc = c.xyz / c.w;
    if (abs(ndc.x) > 1 || abs(ndc.y) > 1 || ndc.z < 0 || ndc.z > 1) return false;
    int2 px = int2((ndc.xy * float2(0.5, -0.5) + 0.5) * DepthSize);
    float d = DepthAt(px);
    if (d >= 1) return false;  // 何も描かれていない
    float3 surface = DepthPoint(px, d);
    float behind = distance(q, DepthEye) - distance(surface, DepthEye);
    if (behind < 0 || behind > thickness) return false;
    if (vanish) {
        p.alive = 0;
        return true;
    }
    float3 a = DepthPoint(px + int2(1, 0), DepthAt(px + int2(1, 0)));
    float3 b = DepthPoint(px + int2(0, 1), DepthAt(px + int2(0, 1)));
    float3 n = cross(b - surface, a - surface);
    float len = length(n);
    n = len > 1e-9 ? n / len : normalize(DepthEye - surface);
    if (dot(n, DepthEye - surface) < 0) n = -n;
    p.position = surface + n * 0.01;
    Bounce(p, n, bounce, friction);
    return true;
}
)";

constexpr char kKernelsHead[] = R"(
RWStructuredBuffer<Particle> Particles : register(u0);
RWStructuredBuffer<float4> Trails : register(u1);  // 粒ごとに TrailPoints 個 (リング: trailHead が一番新しい)
RWByteAddressBuffer AliveCount : register(u2);      // 更新のあとに生きている粒 (数える間だけ結びつける)

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

constexpr char kUpdateHead[] = R"(    uint slot = (SpawnStart + id.x) % Capacity;
    for (uint k = 0; k < TrailPoints; ++k) Trails[slot * TrailPoints + k] = float4(p.position, 0);
    Particles[slot] = p;
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
        if (TrailPoints > 0) {
            p.trailTimer += dt;
            if (p.trailTimer >= TrailInterval) {
                p.trailTimer = 0;
                p.trailHead = (p.trailHead + 1) % TrailPoints;
                Trails[id.x * TrailPoints + p.trailHead] = float4(p.position, 1);
            }
        }
    }
    Particles[id.x] = p;
    if (p.alive != 0) AliveCount.InterlockedAdd(0, 1);
}

StructuredBuffer<Particle> Drawn : register(t0);
StructuredBuffer<float4> DrawnTrails : register(t4);
Texture2D DotImage : register(t5);
SamplerState DotSampler : register(s0);

struct ParticleOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float life : TEXCOORD1;                // 寿命の 0..1 (絵のフレーム)
    nointerpolation uint seed : TEXCOORD2;  // 粒自身の値 (自分のフレーム、自分の煙)
};

ParticleOut Hidden() {
    ParticleOut o;
    o.position = float4(0, 0, 2, 1);  // 絵の外: 何も描かない
    o.color = 0;
    o.uv = 0;
    o.life = 0;
    o.seed = 0;
    return o;
}

// カメラに向いた四角 (Stretch なら、速度の向きに長く: 火花)。
ParticleOut ParticleVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    Particle p = Drawn[instance];
    if (p.alive == 0 || !(p.size > 0)) return Hidden();
    const float2 corners[6] = {float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1)};
    float2 c = corners[vertex];
    float3 world;
    float speed = length(p.velocity);
    if (Stretch > 0 && speed > 1e-4) {
        float3 along = p.velocity / speed;
        float3 side = cross(along, CamPos - p.position);
        float sl = length(side);
        side = sl > 1e-8 ? side / sl : CamRight;
        float len = p.size + speed * Stretch;
        world = p.position + along * (c.y * len * 0.5) + side * (c.x * p.size * 0.5);
    } else {
        float s, k;
        sincos(p.rotation, s, k);
        float2 r = float2(c.x * k - c.y * s, c.x * s + c.y * k) * (p.size * 0.5);
        world = p.position + CamRight * r.x + CamUp * r.y;
    }
    ParticleOut o;
    o.position = mul(ViewProj, float4(world, 1));
    o.color = p.color;
    o.uv = c;
    o.life = LifeRatio(p);
    o.seed = p.seed;
    return o;
}

float4 ParticlePS(ParticleOut i) : SV_Target {
    // 絵 (フレームつき: 列 x 行のシート、左から右、上から下)。
    if (HasImage != 0) {
        uint cols = max(SheetCols, 1), rows = max(SheetRows, 1), frames = cols * rows;
        uint f = SheetMode == 0 ? min((uint)(i.life * frames), frames - 1) : i.seed % frames;
        float2 cell = float2(f % cols, f / cols);
        float2 uv = (cell + float2(i.uv.x * 0.5 + 0.5, 0.5 - i.uv.y * 0.5)) / float2(cols, rows);
        float4 t = DotImage.Sample(DotSampler, uv);
        return float4(i.color.rgb * t.rgb, saturate(i.color.a * t.a));
    }
    float d2 = dot(i.uv, i.uv);
    float a = 1;
    if (DotShape == 0) {  // やわらかい
        a = saturate(1 - d2);
        a *= a;
    } else if (DotShape == 1) {  // くっきり
        float w = max(fwidth(d2), 1e-4);
        a = saturate((1 - d2) / w);
    } else if (DotShape == 3) {  // 星: 光の十字とその真ん中
        float glow = 0.015 / (abs(i.uv.x * i.uv.y) + 0.015);
        a = saturate(max(glow * saturate(1 - d2), saturate(1 - d2 * 6)));
    } else if (DotShape == 4) {  // 煙: やわらかく、むらがある
        float n = Noise2(i.uv * 2.5 + float(i.seed % 997) * 0.37) * 0.6 + Noise2(i.uv * 5 + float(i.seed % 331)) * 0.4;
        a = saturate(1 - d2) * saturate(n * 1.4 - 0.15);
    }
    return float4(i.color.rgb, saturate(i.color.a * a));
}

// トレイル: 粒がいた所 (新しい順; 0 が今いる所)。
float3 TrailPoint(Particle p, uint instance, uint i) {
    if (i == 0) return p.position;
    uint k = (p.trailHead + TrailPoints - (i - 1)) % TrailPoints;
    return DrawnTrails[instance * TrailPoints + k].xyz;
}

// トレイルの点を通る帯。カメラに向き、端へ向かって細く薄くなる。
ParticleOut TrailVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    Particle p = Drawn[instance];
    uint seg = vertex / 6;
    if (p.alive == 0 || TrailPoints == 0 || seg >= TrailPoints) return Hidden();
    const float2 corners[6] = {float2(0, -1), float2(0, 1), float2(1, 1), float2(0, -1), float2(1, 1), float2(1, -1)};
    float2 c = corners[vertex % 6];
    float3 a = TrailPoint(p, instance, seg);
    float3 b = TrailPoint(p, instance, seg + 1);
    float3 pos = lerp(a, b, c.x);
    float3 dir = b - a;
    if (dot(dir, dir) < 1e-12) dir = CamUp;
    float3 side = cross(dir, CamPos - pos);
    float sl = length(side);
    side = sl > 1e-8 ? side / sl : CamRight;
    float f = (seg + c.x) / TrailPoints;  // 粒の所で 0 .. トレイルの端で 1
    ParticleOut o;
    o.position = mul(ViewProj, float4(pos + side * (c.y * p.size * TrailWidth * (1 - f) * 0.5), 1));
    o.color = float4(p.color.rgb, p.color.a * (1 - f));
    o.uv = float2(c.y, f);
    o.life = 0;
    o.seed = 0;
    return o;
}

float4 TrailPS(ParticleOut i) : SV_Target {
    float a = saturate(1 - i.uv.x * i.uv.x);
    return float4(i.color.rgb, saturate(i.color.a * a));
}
)";

// モジュールの値 (またはその初期値) の中の `param` の値。
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
    // 粒: 標準の値、そのあと各 attr を 1 度ずつ (2 つのモジュールが同じ型の 1 つを共有してもよい)。
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
    // 各モジュールを自分の名前空間に入れる: その param (P から LoadParams が設定する)、そのあと 1 行目から
    // そのコード。
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
