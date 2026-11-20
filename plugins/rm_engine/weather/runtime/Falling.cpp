#include "rm_engine/weather/runtime/Falling.h"

#include <algorithm>
#include <cmath>

namespace rm::weather {

const char kFallHlsl[] = R"(
// Rain and snow (Falling.h's Precipitation): 6 corners (2 triangles) per drop / flake, each made from
// its number - where it is in the box around the eye, how fast it falls - by a hash (no buffer).
cbuffer Fall : register(b4) {
    float4x4 viewProj;   // world -> the picture
    float4 eye;          // xyz: where the camera is
    float4 precipInfo;   // x: 0 rain / 1 snow, y: seconds, z: the box's side, w: its 向きの地図 (-1: none)
    float4 precipLight;  // rgb: what lights them (linear)
    float4 camRight;     // xyz: the camera's right (world)
    float4 camUp;        // xyz: the camera's up
    float4 precipWind;   // xy: the wind now (m/s, world x z); zw: how far it has carried them
    float4 precipFall;   // x: the least fall (m/s), y: how much faster each may be, z: how much of the wind takes it (Falling.h's FallOf)
};
uint PrecipHash(uint x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}
float PrecipRand(uint id, uint k) { return (PrecipHash(id * 4 + k) & 0xFFFFFF) / 16777216.0; }
// splash: 0 falling, 1 a raindrop's splash, 2 a flake lying where it landed (melt: 0 .. 1, seed: its own)
struct PrecipOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float fade : TEXCOORD1; float splash : TEXCOORD2; float melt : TEXCOORD3; float seed : TEXCOORD4; };
PrecipOut FallVS(uint vid : SV_VertexID) {
    static const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    uint id = vid / 6;
    float2 q = corners[vid % 6];
    bool snow = precipInfo.x > 0.5;
    float t = precipInfo.y, box = precipInfo.z;
    float3 seed = float3(PrecipRand(id, 0), PrecipRand(id, 1), PrecipRand(id, 2));
    float fall = precipFall.x + (snow ? seed.x : seed.y) * precipFall.y;  // m/s
    // Each one along the same line as the map of what it hits (Falling.h's FallDirection: the average
    // fall): one that falls faster is carried as much faster - so where it hit stays where it is
    // (2026-11-19: before, a flake slid on under the surface and its resting place moved).
    float carried = precipFall.z * fall / (precipFall.x + precipFall.y * 0.5);
    float3 v = float3(precipWind.x * carried, -fall, precipWind.y * carried);  // how it moves now
    float speed = length(v);
    float3 dir = v / speed;
    float3 p = seed * box * 7.31;  // where it is in the world (the box repeats)
    p.y -= t * fall;
    p.xz += precipWind.zw * carried;  // the wind so far
    float3 rel = (frac((p - eye.xyz) / box + 0.5) - 0.5) * box;  // the copy nearest the eye
    float3 w = eye.xyz + rel;
    // Flakes sway (drawn so; what they hit is by the line they fall along).
    float3 sway = snow ? float3(sin(t * 0.9 + seed.z * 40), 0, cos(t * 0.7 + seed.x * 40)) * 0.2 : float3(0, 0, 0);
    float dist = length(rel + sway);
    PrecipOut o;
    o.uv = q;
    o.splash = 0;
    o.melt = 0;
    o.seed = seed.z;
    o.fade = saturate((dist - 0.4) / 1.2) * saturate((box * 0.5 - dist) / (box * 0.25));
    // What is in its way (the map the way it falls, slanted by the wind): how far past it it is now.
    // Past it: it has hit (a roof, a wall, the ground) - from then on nothing falls there.
    float past = MapPast((int)precipInfo.w, w);
    float since = past / speed;  // seconds since it hit
    float3 hit = w - dir * past;  // where
    if (snow) {
        float size = 0.02 + seed.z * 0.025;
        if (past > 0) {
            // It stops where it touched and lies flat on it (a little above: never in it), stays a
            // moment, then melts like a fried egg: the wet edge spreads thin and uneven while the white
            // middle shrinks, all of it fading (FallPS).
            if (since > 3.4) {
                o.pos = float4(0, 0, -1, 1);  // (all six corners: nothing drawn)
                return o;
            }
            o.melt = saturate((since - 1.4) / 2.0);
            o.splash = 2;
            float3 n = -dir;  // (the side it came from: up, slanted by the wind)
            float3 a = normalize(abs(n.z) < 0.99 ? cross(n, float3(0, 0, 1)) : cross(n, float3(1, 0, 0)));
            float3 b = cross(n, a);
            float3 swayThen = float3(sin((t - since) * 0.9 + seed.z * 40), 0, cos((t - since) * 0.7 + seed.x * 40)) * 0.2;
            w = hit + n * 0.01 + swayThen + (a * q.x + b * q.y) * size * 2.6;
        } else {
            w += sway + (camRight.xyz * q.x + camUp.xyz * q.y) * size;
        }
    } else if (past > 0) {
        // It has hit: a splash where it did for a moment (a ring that grows and fades), then nothing.
        if (since > 0.14) {
            o.pos = float4(0, 0, -1, 1);
            return o;
        }
        float k = since / 0.14;
        float r = 0.02 + 0.06 * k;  // (small: a drop's splash, wind or not)
        w = hit - dir * 0.015 + (camRight.xyz * q.x + float3(0, 0.45, 0) * (q.y + 1)) * r;
        o.fade *= 1 - k;
        o.splash = 1;
    } else {
        // A thin streak behind it along the way it falls (the wind slants it), turned to face the eye.
        float3 side = normalize(cross(dir, rel + float3(1e-4, 0, 1e-4)));
        float len = 0.35 + seed.x * 0.25;
        w += side * q.x * (0.004 + dist * 0.0006) - dir * (q.y * 0.5 + 0.5) * len;
    }
    o.pos = mul(viewProj, float4(w, 1));
    return o;
}
float4 FallPS(PrecipOut i) : SV_Target {
    bool snow = precipInfo.x > 0.5;
    float a;
    if (i.splash > 1.5) {
        // A flake lying where it landed (the quad is 2.6 times its size): first round as it fell; as it
        // melts, the edge spreads thin and uneven (the white of an egg) while the middle shrinks (the
        // yolk), all fading out.
        float r = length(i.uv);
        float ang = atan2(i.uv.y, i.uv.x);
        float m = i.melt;
        const float core = 1.0 / 2.6;
        float yolk = core * (1 - 0.7 * m);
        float white = core * (1 + 1.4 * m) * (1 + (0.12 * sin(ang * 5 + i.seed * 40) + 0.08 * sin(ang * 3 + i.seed * 17)) * m);
        float aYolk = (1 - smoothstep(yolk * 0.55, yolk, r)) * 0.9;
        float aWhite = (1 - smoothstep(white * 0.7, white, r)) * lerp(0.9, 0.3, m);
        a = max(aYolk, aWhite) * (1 - m * m);
    } else if (i.splash > 0.5) {
        float r = length(i.uv * float2(1, 1.6) - float2(0, 0.6));  // a flat ring, seen from the side
        a = saturate(1 - abs(r - 0.65) * 4) * 0.45 + saturate(1 - r * 3) * 0.2;
    } else {
        a = snow ? saturate((1 - length(i.uv)) * 2.5) * 0.9 : (1 - abs(i.uv.x)) * (0.55 - 0.25 * i.uv.y * i.uv.y) * 0.45;
    }
    return float4(ToScreen(precipLight.rgb * (snow ? 1.0 : 0.85)), saturate(a) * i.fade);
}
)";

const char kWetSurfaceHlsl[] = R"(
float WetHash(float2 c) { return frac(sin(dot(c, float2(127.1, 311.7))) * 43758.5453); }
// Smooth noise (0 .. 1) over the plane.
float WetNoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3 - 2 * f);
    float a = WetHash(i), b = WetHash(i + float2(1, 0)), c = WetHash(i + float2(0, 1)), d = WetHash(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Small wet specks where drops landed (a few centimetres, uneven), as many as `density` says (0: none
// .. 1: all of it wet): the parts of a fine noise under a threshold. Far away (a speck smaller than a
// pixel or two) the specks melt into their average, so they do not shimmer. `s`: the place on the
// surface (metres); `footprint`: how far it goes across a pixel (fwidth, taken where every pixel runs).
float WetSpecks(float2 s, float density, float footprint) {
    float n = 0.55 * WetNoise(s * 22) + 0.3 * WetNoise(s * 55 + 7.1) + 0.15 * WetNoise(s * 120 + 3.3);
    float threshold = lerp(0.18, 0.86, density);
    float specks = 1 - smoothstep(threshold - 0.04, threshold + 0.04, n);
    specks = lerp(specks, density, saturate(footprint * 22 * 1.2 - 0.2));
    return max(specks, smoothstep(0.85, 1.0, density));  // (wet through: no dry gaps left)
}

// How much the rain reached a point the way `k` (0 .. 1): the place a little off the surface is not past
// anything that way - read at 32 places on a spiral 75 cm around it (weighted to the middle), so toward a
// roof's edge the specks thin out over a metre or so, as on real ground. Each place reads "reached or
// not", so the spiral is turned and moved a little from place to place (slowly, and in speck-sized
// wobbles): the steps of each reading fall at different places and overlap - no band, no line where the
// specks stop (2026-11-20: a 5 x 5 grid made 5 straight bands, the specks cut off along each).
float WetOpen(int k, float3 p, float2 s) {
    const float2 wobble = float2(WetNoise(s * 7 + 11.3), WetNoise(s * 7 + 27.9)) * 2 - 1;
    const float turn = WetNoise(s * 0.8 + 5.1) * 6.2831853;
    const float4 c = mul(mapViewProj[k], float4(p + float3(wobble.x, 0, wobble.y) * 0.18, 1));
    const float4 ax = mul(mapViewProj[k], float4(1, 0, 0, 0)), az = mul(mapViewProj[k], float4(0, 0, 1, 0));
    float open = 0, total = 0;
    [unroll] for (int i = 0; i < 32; ++i) {
        const float r = 0.75 * sqrt((i + 0.5) / 32.0);
        const float a = i * 2.3999632 + turn;  // (the golden angle: evenly over the disc)
        const float w = 1.05 - r / 0.75;
        const float2 o = float2(cos(a), sin(a)) * r;
        open += saturate((0.06 - MapPastAt(k, c + ax * o.x + az * o.y)) / 0.06) * w;
        total += w;
    }
    return open / total;
}

// しみこみ: how much of the wet reaches a covered point from around it - water spreading over the ground
// (WetWay::seep). Read at 16 places on a spiral `reach` around it: wet when enough of them are open,
// fewer and fewer specks toward `reach` in. Under a thing smaller than `reach` all of it is open nearby;
// under a roof only its edge (2026-11-20: the ground right under a ball or a barrel stayed dry).
float WetSeep(int k, float3 p, float2 s, float reach) {
    const float turn = WetNoise(s * 0.8 + 9.7) * 6.2831853;
    const float4 c = mul(mapViewProj[k], float4(p, 1));
    const float4 ax = mul(mapViewProj[k], float4(1, 0, 0, 0)), az = mul(mapViewProj[k], float4(0, 0, 1, 0));
    float open = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        const float r = reach * sqrt((i + 0.5) / 16.0);
        const float a = i * 2.3999632 + turn;
        const float2 o = float2(cos(a), sin(a)) * r;
        open += saturate((0.06 - MapPastAt(k, c + ax * o.x + az * o.y)) / 0.06);
    }
    return saturate(open / 16 * 4);
}

// How wet a surface looks (0 .. 1): the wettest of the ways the rain fell that reached it (a place it
// reached stays wet while it rains, whatever the wind does after), in the sun or in its shadow (shade
// dries slower) - small specks, more and more of them until all of it is wet; toward a roof's edge
// fewer and fewer (no line, no wave: as many specks as it is wet there); most on what faces up, none
// facing down.
float WetAt(float3 wpos, float3 n) {
    // The surface's own two axes (the plane it mostly lies in): the specks lie flat on it.
    float3 a = abs(n);
    float2 s = a.y >= a.x && a.y >= a.z ? wpos.xz : a.x >= a.z ? wpos.zy : wpos.xy;
    float footprint = max(fwidth(s.x), fwidth(s.y));  // (before any return: derivatives want every pixel)
    int count = (int)surfaceData[0].x;
    if (count <= 0 || n.y < -0.3) return 0;
    float lit = shadowInfo.x > 0 ? SunShadow(wpos, n) : 1;
    float3 p = wpos + n * 0.12;
    // How wet it has got (by time: `level`) and how much of that reaches here (`wet`: the level times how
    // open the place is). How many specks: `wet`; how dark they are: `level`.
    float wet = 0, level = 0;
    [loop] for (int k = 0; k < count; ++k) {
        float4 way = surfaceData[1 + k];  // x: how wet in the sun, y: in shade, z: its 向きの地図 (-1: none), w: しみこみ (m)
        float l = lerp(way.y, way.x, lit);
        level = max(level, l);
        if (l <= wet) continue;
        int m = (int)way.z;
        if (m < 0 || mapInfo[m].x < 0.5) {
            wet = l;  // (no map: nothing to hit, all open)
            continue;
        }
        float open = WetOpen(m, p, s);
        if (way.w > 0.02 && open < 0.999) open = max(open, WetSeep(m, p, s, way.w));
        wet = max(wet, l * open);
    }
    if (wet <= 0) return 0;
    // Between dry and wet (an edge, the start of the rain) more specks here, fewer there - patches a
    // metre or two across - so an edge is no even band (nothing changes where it is dry or wet through).
    const float patch = WetNoise(s * 0.6) * 2 - 1;
    const float density = saturate(wet + patch * wet * (1 - wet) * 0.9);
    return saturate(level * 1.5) * WetSpecks(s, density, footprint) * lerp(0.45, 1, saturate(n.y));
}


// 濡れ on every surface (3D 空間's RenderFrame::surface): water in the surface makes it darker - and only a
// touch smoother: wet ground, not a puddle. (2026-11-19: smoother, a wet speck in shade mirrored the sky
// and looked lighter than the dry ground - the shadow's edge became a line; now a speck is always darker.)
void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n) {
    float wet = WetAt(wpos, n);
    albedo *= 1 - wet * 0.3 * (1 - metal);
    rough = lerp(rough, min(rough, max(rough * 0.85, 0.5)), wet);
}
)";

uint32_t PrecipitationCount(const Precipitation& p) {
    const float amount = p.amount > 1.f ? 1.f : p.amount < 0.f ? 0.f : p.amount;
    const uint32_t most = p.kind == Precipitation::Kind::Rain ? kMaxRaindrops : kMaxSnowflakes;
    return static_cast<uint32_t>(amount * static_cast<float>(most) + 0.5f);
}

FallSpeed FallOf(Precipitation::Kind kind) {
    // (2026-11-19: half the wind they took before - they slanted too much)
    return kind == Precipitation::Kind::Rain ? FallSpeed{9.f, 3.f, 0.45f} : FallSpeed{0.9f, 0.6f, 0.5f};
}

Vec3 FallDirection(Precipitation::Kind kind, Vec3 wind) {
    const FallSpeed f = FallOf(kind);
    const float x = wind.x * f.carried, y = -(f.least + f.spread * 0.5f), z = wind.z * f.carried;
    const float len = std::sqrt(x * x + y * y + z * z);
    return {x / len, y / len, z / len};
}

std::vector<float> WetSurfaceData(std::span<const WetWay> ways, std::span<const int> maps) {
    const size_t count = std::min(ways.size(), kWetWays);
    std::vector<float> data(4 * (1 + kWetWays), 0.f);
    data[0] = static_cast<float>(count);
    for (size_t k = 0; k < count; ++k) {
        float* way = &data[4 * (1 + k)];
        way[0] = std::clamp(ways[k].sun, 0.f, 1.f);
        way[1] = std::clamp(ways[k].shade, 0.f, 1.f);
        way[2] = static_cast<float>(k < maps.size() ? maps[k] : -1);
        way[3] = std::isfinite(ways[k].seep) ? std::clamp(ways[k].seep, 0.f, 4.f) : 0.f;
    }
    return data;
}

}  // namespace rm::weather
