#include "rm_engine/weather/runtime/RainShafts.h"

#include <algorithm>
#include <cmath>

namespace rm::weather {

const char kRainShaftHlsl[] = R"(
cbuffer Shafts : register(b4) {
    float4x4 shaftViewProj;
    float4 shaftLight;  // rgb: 照らす光
    float4 shaftLook;   // x: 秒、y: 消えきる近さ、z: 見え始める距離、w: 薄れきる遠さ
};
struct ShaftIn {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;    // u: 横 (-1 .. 1)、v: 0 下 .. 1 上
    float3 look : TEXCOORD1;  // x: 降る量、y: 模様の種、z: カメラからの距離 (m)
};
struct ShaftOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 look : TEXCOORD1;
    float height : TEXCOORD2;
};
ShaftOut ShaftVS(ShaftIn i) {
    ShaftOut o;
    o.pos = mul(shaftViewProj, float4(i.pos, 1));
    o.uv = i.uv;
    o.look = i.look;
    o.height = i.pos.y;
    return o;
}
float ShaftHash(float x) { return frac(sin(x * 127.1 + 31.7) * 43758.5453); }
float4 ShaftPS(ShaftOut i) : SV_Target {
    float rain = saturate(i.look.x);
    // 縦の筋: 細い列ごとに明るさと速さが違い、時間で下へ流れる。
    float column = floor((i.uv.x * 0.5 + 0.5) * 48 + i.look.y * 7.0);
    float speed = 0.6 + 0.8 * ShaftHash(column);
    float flow = frac(i.height * 0.05 + shaftLook.x * speed + ShaftHash(column + 11.0));
    float streak = lerp(0.55, 1.0, ShaftHash(column + 3.0)) * (0.6 + 0.4 * smoothstep(0.0, 0.6, flow) * (1 - smoothstep(0.6, 1.0, flow)));
    float a = rain * (0.25 + 0.35 * rain) * streak;
    a *= 1 - smoothstep(0.5, 1.0, abs(i.uv.x));                         // 横の端
    a *= smoothstep(0.0, 0.08, i.uv.y) * (1 - smoothstep(0.85, 1.0, i.uv.y));  // 下と上 (雲の中) の端
    a *= smoothstep(shaftLook.y, shaftLook.z, i.look.z) * (1 - smoothstep(shaftLook.w * 0.8, shaftLook.w, i.look.z));  // 近く (粒) と遠く
    if (a <= 0.002) discard;
    float3 c = shaftLight.rgb * lerp(0.75, 0.45, rain);  // (強い雨ほど暗い灰色)
    return float4(c, a);
}
)";

void RainShaftsOf(const PlaceClouds& map, int cells, float cell, Vec3 eye, std::vector<RainShaft>& out) {
    out.clear();
    if (cells <= 0 || !(cell > 0) || !std::isfinite(cell) || !std::isfinite(map.firstX) || !std::isfinite(map.firstZ) || !std::isfinite(eye.x) ||
        !std::isfinite(eye.z))
        return;
    const size_t count = static_cast<size_t>(cells) * static_cast<size_t>(cells);
    if (map.rains.size() != count) return;
    struct Spot {
        float d2;
        RainShaft shaft;
    };
    std::vector<Spot> spots;
    const float reach2 = kShaftReach * kShaftReach, near2 = kShaftNear * kShaftNear;
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float r = map.rains[static_cast<size_t>(j) * static_cast<size_t>(cells) + static_cast<size_t>(i)];
            if (!std::isfinite(r) || r < kLeastRainForShaft) continue;
            RainShaft s;
            s.x = map.firstX + static_cast<float>(i) * cell;
            s.z = map.firstZ + static_cast<float>(j) * cell;
            const float dx = s.x - eye.x, dz = s.z - eye.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 > reach2 || d2 < near2) continue;
            s.width = cell * 1.4f;
            s.rain = std::min(r, 1.f);
            const auto wi = static_cast<uint32_t>(static_cast<uint64_t>(std::llround(s.x / cell)));
            const auto wj = static_cast<uint32_t>(static_cast<uint64_t>(std::llround(s.z / cell)));
            s.seed = (wi * 73856093u) ^ (wj * 19349663u);
            spots.push_back({d2, s});
        }
    }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.d2 < b.d2; });
    if (spots.size() > kMostShafts) spots.resize(kMostShafts);
    // 遠い順 (後ろから描く)。
    for (auto it = spots.rbegin(); it != spots.rend(); ++it) out.push_back(it->shaft);
}

void ShaftQuads(std::span<const RainShaft> shafts, Vec3 eye, float bottom, float top, Vec2 wind, std::vector<ShaftVertex>& out) {
    out.clear();
    out.reserve(shafts.size() * 6);
    const float height = top - bottom;
    const Vec2 lean{std::isfinite(wind.x) ? -wind.x * height / kShaftFall : 0.f, std::isfinite(wind.y) ? -wind.y * height / kShaftFall : 0.f};
    for (const RainShaft& s : shafts) {
        // カメラの方へ縦の軸で回す: 横の向き = (カメラへの向き) を 90 度回したもの。
        float dx = eye.x - s.x, dz = eye.z - s.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > 1e-4f) dx /= d, dz /= d;
        else dx = 0, dz = 1;
        const float rx = -dz * s.width * 0.5f, rz = dx * s.width * 0.5f;
        const auto corner = [&](float u, float v) {
            ShaftVertex vtx;
            vtx.position = {s.x + rx * u + lean.x * v, bottom + height * v, s.z + rz * u + lean.y * v};
            vtx.u = u, vtx.v = v;
            vtx.rain = s.rain;
            vtx.seed = static_cast<float>(s.seed % 997u);
            vtx.away = d;
            return vtx;
        };
        const ShaftVertex a = corner(-1, 0), b = corner(1, 0), c = corner(1, 1), e = corner(-1, 1);
        for (const ShaftVertex& v : {a, b, c, a, c, e}) out.push_back(v);
    }
}

}  // namespace rm::weather
