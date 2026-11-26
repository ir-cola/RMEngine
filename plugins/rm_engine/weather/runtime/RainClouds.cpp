#include "rm_engine/weather/runtime/RainClouds.h"

#include "rm_engine/weather/runtime/Lightning.h"

#include <algorithm>
#include <cmath>

namespace rm::weather {

namespace {

float Smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}

// ワールドのマス (wi, wj) の段 `layer` の種 (同じマスはいつも同じ雲)。
uint32_t PuffSeed(int64_t wi, int64_t wj, int layer) {
    const auto u = [](int64_t v) { return static_cast<uint32_t>(static_cast<uint64_t>(v)); };
    return (u(wi) * 73856093u) ^ (u(wj) * 19349663u) ^ (static_cast<uint32_t>(layer + 1) * 83492791u);
}

}  // namespace

const char kRainPuffHlsl[] = R"(
cbuffer Puffs : register(b4) {
    float4x4 puffViewProj;
    float4 puffLight;  // rgb: 照らす光
    float4 puffEye;    // xyz: カメラ、w: 薄れきる距離
};
struct PuffIn {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;     // 板の上の場所 (-1 .. 1)
    float3 look : TEXCOORD1;   // x: 暗さ、y: 濃さ、z: 模様の種
};
struct PuffOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 look : TEXCOORD1;
    float dist : TEXCOORD2;
};
PuffOut PuffVS(PuffIn i) {
    PuffOut o;
    o.pos = mul(puffViewProj, float4(i.pos, 1));
    o.uv = i.uv;
    o.look = i.look;
    o.dist = length(i.pos - puffEye.xyz);
    return o;
}
float PuffHash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float PuffNoise(float2 p) {
    float2 c = floor(p), f = frac(p);
    f = f * f * (3 - 2 * f);
    return lerp(lerp(PuffHash(c), PuffHash(c + float2(1, 0)), f.x), lerp(PuffHash(c + float2(0, 1)), PuffHash(c + float2(1, 1)), f.x), f.y);
}
float4 PuffPS(PuffOut i) : SV_Target {
    float2 p = i.uv * 2.2 + i.look.z * 0.731;
    float n = PuffNoise(p) * 0.55 + PuffNoise(p * 2.1 + 5.3) * 0.3 + PuffNoise(p * 4.3 + 9.1) * 0.15;  // (0 .. 1)
    float r = length(i.uv);
    float body = 1 - smoothstep(0.3 + 0.3 * n, 1.0, r);  // 縁はもくもく
    float a = saturate(i.look.y * body * (0.75 + 0.5 * n));
    a *= smoothstep(6.0, 40.0, i.dist) * (1 - smoothstep(puffEye.w * 0.75, puffEye.w, i.dist));
    if (a <= 0.002) discard;
    float shade = saturate(i.look.x * (0.8 + 0.35 * (0.5 - 0.5 * i.uv.y)));  // (板の下ほど暗い)
    float3 lit = puffLight.rgb * 0.9;
    float3 dark = puffLight.rgb * 0.08;
    float3 c = lerp(lit, dark, shade) * (0.85 + 0.3 * n);
    return float4(c, a);
}
)";

void RainCloudPuffs(const PlaceClouds& map, int cells, float cell, Vec3 eye, std::vector<CloudPuff>& out) {
    out.clear();
    if (cells <= 0 || !(cell > 0) || !std::isfinite(cell) || !std::isfinite(map.firstX) || !std::isfinite(map.firstZ) || !std::isfinite(eye.x) ||
        !std::isfinite(eye.z))
        return;
    const size_t count = static_cast<size_t>(cells) * static_cast<size_t>(cells);
    if (map.rains.size() != count) return;
    struct Spot {
        float d2;
        float x, z, rain;
    };
    std::vector<Spot> spots;
    const float reach2 = kRainCloudReach * kRainCloudReach;
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float r = map.rains[static_cast<size_t>(j) * static_cast<size_t>(cells) + static_cast<size_t>(i)];
            if (!std::isfinite(r) || r < kLeastRainForPuff) continue;
            const float x = map.firstX + static_cast<float>(i) * cell, z = map.firstZ + static_cast<float>(j) * cell;
            const float dx = x - eye.x, dz = z - eye.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 > reach2) continue;
            spots.push_back({d2, x, z, std::min(r, 1.f)});
        }
    }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.d2 < b.d2; });
    for (const Spot& s : spots) {
        const float thick = Smooth(0.15f, 0.95f, s.rain);
        const int layers = 1 + std::min(2, static_cast<int>(thick * 3.f));
        const int64_t wi = std::llround(s.x / cell), wj = std::llround(s.z / cell);
        for (int layer = 0; layer < layers && out.size() < kMostRainPuffs; ++layer) {
            const uint32_t seed = PuffSeed(wi, wj, layer);
            const float up = layers > 1 ? static_cast<float>(layer) / static_cast<float>(layers - 1) : 0.f;  // (0: 底 .. 1: 上)
            CloudPuff p;
            p.center = {s.x + (Hash01(seed) - 0.5f) * 0.7f * cell,
                        kRainCloudBase + static_cast<float>(layer) * (10.f + 14.f * s.rain) + Hash01(seed + 2u) * 6.f,
                        s.z + (Hash01(seed + 1u) - 0.5f) * 0.7f * cell};
            p.radius = cell * (0.85f + 0.55f * s.rain) * (0.85f + 0.3f * Hash01(seed + 3u)) * (1.15f - 0.25f * up);
            p.shade = std::clamp(thick * (1.f - 0.35f * up), 0.f, 1.f);  // (下の段ほど暗い)
            p.alpha = std::clamp(0.18f + 0.72f * s.rain, 0.f, 1.f);
            p.seed = seed;
            out.push_back(p);
        }
        if (out.size() >= kMostRainPuffs) break;
    }
    // 遠いものから (後ろから描く)。
    std::sort(out.begin(), out.end(), [&](const CloudPuff& a, const CloudPuff& b) {
        const auto d = [&](const CloudPuff& p) {
            const float dx = p.center.x - eye.x, dy = p.center.y - eye.y, dz = p.center.z - eye.z;
            return dx * dx + dy * dy + dz * dz;
        };
        return d(a) > d(b);
    });
}

void PuffQuads(std::span<const CloudPuff> puffs, Vec3 right, Vec3 up, std::vector<PuffVertex>& out) {
    out.clear();
    out.reserve(puffs.size() * 6);
    for (const CloudPuff& p : puffs) {
        const auto corner = [&](float u, float v) {
            PuffVertex vtx;
            vtx.position = {p.center.x + (right.x * u + up.x * v) * p.radius, p.center.y + (right.y * u + up.y * v) * p.radius,
                            p.center.z + (right.z * u + up.z * v) * p.radius};
            vtx.u = u, vtx.v = v;
            vtx.shade = p.shade, vtx.alpha = p.alpha;
            vtx.seed = static_cast<float>(p.seed % 1024u);
            return vtx;
        };
        const PuffVertex a = corner(-1, -1), b = corner(1, -1), c = corner(1, 1), d = corner(-1, 1);
        for (const PuffVertex& v : {a, b, c, a, c, d}) out.push_back(v);
    }
}

}  // namespace rm::weather
