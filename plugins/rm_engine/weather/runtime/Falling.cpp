#include "rm_engine/weather/runtime/Falling.h"

#include "rm_engine/space3d/runtime/Lighting.h"

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
        float size = 0.014 + seed.z * 0.017;  // (2026-11-20: a third smaller)
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
// 積雪 on a surface (0 .. 1): how white it is - on what faces up and the snow reaches straight down
// (surfaceData[0].z: its 向きの地図; under a roof: none). Thin snow: white patches growing together, all
// of it white at 2 cm (deeper, the snow lying - SnowLayerVS - covers it with its height too).
static const float3 kSnowWhite = float3(0.82, 0.84, 0.88);
// How the snow stands at (x, z) where `depth` lies (its height: depth x this; 0: no snow there). Thin:
// lumps that rise where a slow noise is high - taller than the depth, the snow gathered in them - and
// grow together; from about 8 cm a blanket with a gentle swell (2026-11-20: thin snow was flat specks,
// like the rain's).
float SnowShape(float2 xz, float depth) {
    if (depth <= 0) return 0;
    float n = 0.65 * WetNoise(xz * 2.2 + 1.7) + 0.35 * WetNoise(xz * 5.3 + 4.1);
    float cover = saturate(depth / 0.04);
    float lump = saturate((n - (1 - cover)) / 0.3);
    lump = lump * lump * (3 - 2 * lump);
    float thin = lump * lerp(2.5, 1, saturate(depth / 0.06));
    return lerp(thin, 0.85 + 0.3 * n, saturate((depth - 0.04) / 0.06));
}
// Is the point open the way map `m` looks (0 .. 1; no map: open)?
float OpenFor(int m, float3 p) {
    return m >= 0 && mapInfo[m].x > 0.5 ? saturate((0.06 - MapPastAt(m, mul(mapViewProj[m], float4(p, 1)))) / 0.06) : 1;
}
// How deep the snow lies at a point: what each way left (WetWay::snow: surfaceData[5 + k].x) where that
// way reaches (its 向きの地図) - added up. Slanted by the wind, a way reaches under a roof's edge too.
float SnowDepthAt(float3 wpos, float3 n) {
    int count = (int)surfaceData[0].x;
    float depth = 0;
    [loop] for (int k = 0; k < count; ++k) {
        float snow = surfaceData[5 + k].x;
        if (snow > 0) depth += snow * OpenFor((int)surfaceData[1 + k].z, wpos + n * 0.05);
    }
    return depth;
}
// 積雪 on a surface (0 .. 1): white where the snow stands (the foot of the snow lying with its height -
// SnowLayerVS, the same shape), on what faces up.
float SnowAt(float3 wpos, float3 n) {
    if (n.y < 0.3) return 0;
    float depth = SnowDepthAt(wpos, n);
    return depth > 0 ? smoothstep(0.01, 0.03, SnowShape(wpos.xz, depth)) * smoothstep(0.3, 0.5, n.y) : 0;
}

// 濡れ and 積雪 on every surface (3D 空間's RenderFrame::surface): water in the surface makes it darker
// - and only a touch smoother: wet ground, not a puddle. (2026-11-19: smoother, a wet speck in shade
// mirrored the sky and looked lighter than the dry ground - the shadow's edge became a line; now a
// speck is always darker.) Snow lies over it: white and dull.
void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n) {
    float wet = WetAt(wpos, n);
    float snow = SnowAt(wpos, n);
    wet *= 1 - snow;
    albedo *= 1 - wet * 0.3 * (1 - metal);
    rough = lerp(rough, min(rough, max(rough * 0.85, 0.5)), wet);
    albedo = lerp(albedo, kSnowWhite, snow);
    rough = lerp(rough, 0.75, snow);
}
)";

const char kSnowLayerHlsl[] = R"(
// 積雪 with its height (RenderFrame::solids), one layer for each way snow fell (a WetWay with snow): a
// grid across the way around the eye (SnowLayerOf). Each corner goes down the way's line to what the
// snow hit (its 向きの地図 - slanted by the wind, that is under a roof's edge too) and stands up there as
// high as the snow lies (all the ways' snow where each reaches, in SnowShape's lumps), but never above
// where something moving pressed it (the footprints, SnowTrailPS). At a step: on its top the snow thins
// to nothing (a rounded edge); at its foot and where nothing was hit the corner is left out (NaN: its
// triangles are not drawn). Where an earlier way's layer reaches too, that one draws it (SnowLayerPS).
// Lit as the models are (Shade). After RendererShaderText(kWetSurfaceHlsl).
cbuffer SnowLayer : register(b4) {
    float4 layerAcross;   // xyz: one axis across the way; w: a cell (m)
    float4 layerAlong;    // xyz: the other axis across it; w: cells a side
    float4 layerWay;      // xyz: the way it fell; w: this layer's way (0 ..)
    float4 layerMiddle;   // xyz: the grid's middle (the eye, in whole cells); w: -
    float4 layerSnow[4];  // each way: x: its snow (m), y: its 向きの地図 (-1: none)
    float4 trailInfo;     // xy: the footprints' first cell (world x z); z: a cell (m); w: cells a side (0: none)
    float4 trailMore;     // x: which count of footprints (each Play its own); y .. w: -
};
Texture2D<float4> trailMap : register(t9);

// The highest the snow may stand at (x, z): where something moving pressed it, or 1e30.
float TrailCeiling(float2 xz) {
    if (trailInfo.w < 1) return 1e30;
    float n = trailInfo.w;
    float2 cell = floor(xz / trailInfo.z);
    int2 t = int2(cell - n * floor(cell / n));  // (the cell's place in the map: it wraps around)
    float4 v = trailMap.Load(int3(t, 0));
    return all(v.yz == cell) && v.w == trailMore.x ? v.x : 1e30;
}

// One corner: what the way's line from it hits (`hit`; `past`: how far the corner is past it; none:
// nothing), how deep the snow lies there and the top of the snow (world y).
struct SnowCorner {
    float3 hit;
    float past;
    float depth;
    float top;
    bool none;
};
SnowCorner CornerAt(float2 g) {
    SnowCorner c;
    float cell = layerAcross.w;
    float3 q = layerMiddle.xyz + layerAcross.xyz * (g.x * cell) + layerAlong.xyz * (g.y * cell);
    int m = (int)layerSnow[(int)layerWay.w].y;
    c.past = MapPastAt(m, mul(mapViewProj[m], float4(q, 1)));
    c.none = c.past < -1e29;
    c.hit = q - layerWay.xyz * c.past;
    c.depth = 0;
    c.top = c.hit.y;
    if (c.none) return c;
    float3 above = c.hit + float3(0, 0.05, 0);
    [unroll] for (int j = 0; j < 4; ++j) {
        if (layerSnow[j].x > 0) c.depth += layerSnow[j].x * OpenFor((int)layerSnow[j].y, above);
    }
    // (fading out toward the grid's edge: no wall where it ends)
    float fade = saturate((layerAlong.w * 0.5 - max(abs(g.x), abs(g.y))) * cell / 4);
    float h = c.depth * SnowShape(c.hit.xz, c.depth) * fade;
    c.top = max(c.hit.y, min(c.hit.y + h, TrailCeiling(c.hit.xz)));
    return c;
}

struct SnowOut {
    float4 pos : SV_Position;
    float3 nrm : NORMAL;
    float3 wpos : TEXCOORD2;
    float3 ground : TEXCOORD3;  // what the snow lies on
    float depth : TEXCOORD4;    // how deep it lies there
};

SnowOut SnowLayerVS(uint id : SV_VertexID) {
    SnowOut o;
    uint side = (uint)layerAlong.w + 1;
    float2 g = float2(id % side, id / side) - (side - 1) * 0.5;
    SnowCorner c = CornerAt(g);
    SnowCorner l = CornerAt(g - float2(1, 0)), r = CornerAt(g + float2(1, 0));
    SnowCorner d = CornerAt(g - float2(0, 1)), u = CornerAt(g + float2(0, 1));
    float ledge = 0.15 + layerAcross.w * 1.5;
    // A neighbour much nearer up the line: this corner is at a step's foot. Much further (or none): at
    // its top - a rim.
    bool foot = (!l.none && l.past - c.past > ledge) || (!r.none && r.past - c.past > ledge) ||
                (!d.none && d.past - c.past > ledge) || (!u.none && u.past - c.past > ledge);
    bool rim = l.none || r.none || d.none || u.none || c.past - l.past > ledge || c.past - r.past > ledge ||
               c.past - d.past > ledge || c.past - u.past > ledge;
    // The ground's slope and the snow's (a neighbour across a step: as if this one).
    float3 gl = l.none || abs(l.past - c.past) > ledge ? c.hit : l.hit, gr = r.none || abs(r.past - c.past) > ledge ? c.hit : r.hit;
    float3 gd = d.none || abs(d.past - c.past) > ledge ? c.hit : d.hit, gu = u.none || abs(u.past - c.past) > ledge ? c.hit : u.hit;
    float3 across = gr - gl, along = gu - gd;
    float3 groundN = cross(along, across);
    groundN = dot(groundN, groundN) > 1e-12 ? normalize(groundN) : float3(0, 1, 0);
    if (groundN.y < 0) groundN = -groundN;
    if (c.none || foot || groundN.y < 0.3) {
        float nan = asfloat(0x7fc00000);
        o.pos = float4(nan, nan, nan, nan);
        o.nrm = float3(0, 1, 0);
        o.wpos = o.ground = float3(0, 0, 0);
        o.depth = 0;
        return o;
    }
    float tl = gl.y + (gl.x == c.hit.x && gl.z == c.hit.z ? c.top - c.hit.y : l.top - l.hit.y);
    float tr = gr.y + (gr.x == c.hit.x && gr.z == c.hit.z ? c.top - c.hit.y : r.top - r.hit.y);
    float td = gd.y + (gd.x == c.hit.x && gd.z == c.hit.z ? c.top - c.hit.y : d.top - d.hit.y);
    float tu = gu.y + (gu.x == c.hit.x && gu.z == c.hit.z ? c.top - c.hit.y : u.top - u.hit.y);
    float3 n = cross(float3(gu.x, tu, gu.z) - float3(gd.x, td, gd.z), float3(gr.x, tr, gr.z) - float3(gl.x, tl, gl.z));
    n = dot(n, n) > 1e-12 ? normalize(n) : groundN;
    if (n.y < 0) n = -n;
    o.nrm = n;
    o.ground = c.hit;
    o.depth = c.depth;
    o.wpos = float3(c.hit.x, rim ? c.hit.y + 0.002 : max(c.top, c.hit.y + 0.002), c.hit.z);
    o.pos = mul(viewProj, float4(o.wpos, 1));
    return o;
}

float4 SnowLayerPS(SnowOut i) : SV_Target {
    clip(SnowShape(i.ground.xz, i.depth) - 0.02);  // (where no lump stands: none)
    // An earlier way's layer reaches here too: that one draws it.
    int k = (int)layerWay.w;
    float3 above = i.ground + float3(0, 0.05, 0);
    [unroll] for (int j = 0; j < 4; ++j) {
        if (j < k && layerSnow[j].x > 0.0005 && OpenFor((int)layerSnow[j].y, above) > 0.5) discard;
    }
    float3 n = normalize(i.nrm);
    float3 v = normalize(eye.xyz - i.wpos);
    float grain = (WetNoise(i.wpos.xz * 40) - 0.5) * 0.05;  // (a little uneven: not plastic)
    return float4(ToScreen(Shade(kSnowWhite + grain, 0, 0.75, n, v, i.wpos)), 1);
}
)";

const char kSnowTrailHlsl[] = R"(
// 足跡: the highest the snow may stand at each cell around the eye (SnowTrailOf), kept from frame to frame
// (two textures, each frame drawn from the other). Where something moving is (its underside, seen from
// below: a 向きの地図 of the moving shapes only) the snow can stand no higher than it - it is pressed
// down, and stays so; while it snows it fills up again as fast as the snow grows. The map wraps around:
// each cell keeps which world cell it is (a cell come new into the square starts with no footprint).
// After RendererShaderText(kWetSurfaceHlsl).
cbuffer SnowTrail : register(b4) {
    float4 trailInfo;  // xy: the first cell (world x z); z: a cell (m); w: cells a side
    float4 trailMore;  // x: which count (each Play its own); y: how much it fills up now (m); z: where the lines up start (world y); w: the map from below
};
Texture2D<float4> trailBefore : register(t9);

float4 SnowTrailVS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 SnowTrailPS(float4 pos : SV_Position) : SV_Target {
    float n = trailInfo.w;
    float2 t = floor(pos.xy);
    float2 cell = trailInfo.xy + (t - trailInfo.xy - n * floor((t - trailInfo.xy) / n));
    float4 before = trailBefore.Load(int3(t, 0));
    float ceiling = all(before.yz == cell) && before.w == trailMore.x ? before.x : 1e30;
    if (ceiling < 1e29) ceiling += trailMore.y;
    int m = (int)trailMore.w;
    if (m >= 0 && mapInfo[m].x > 0.5) {
        float2 c = (cell + 0.5) * trailInfo.z;
        [unroll] for (int y = -1; y <= 1; ++y) {
            [unroll] for (int x = -1; x <= 1; ++x) {
                float past = MapPastAt(m, mul(mapViewProj[m], float4(c.x + x * trailInfo.z, trailMore.z, c.y + y * trailInfo.z, 1)));
                if (past > -1e29) ceiling = min(ceiling, trailMore.z - past + 0.03 * (abs(x) + abs(y)));  // (a softer rim)
            }
        }
    }
    return float4(ceiling, cell, trailMore.x);
}
)";

namespace {

// The two axes across a way (as 3D 空間's PlanCover has them).
void AcrossOf(Vec3 d, Vec3& a, Vec3& b) {
    auto cross = [](Vec3 x, Vec3 y) { return Vec3{x.y * y.z - x.z * y.y, x.z * y.x - x.x * y.z, x.x * y.y - x.y * y.x}; };
    a = std::fabs(d.z) < 0.99f ? cross(d, {0, 0, 1}) : cross(d, {1, 0, 0});
    const float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    a = len > 0 ? Vec3{a.x / len, a.y / len, a.z / len} : Vec3{1, 0, 0};
    b = cross(a, d);
}

void TrailFirstCell(Vec3 eye, float out[4]) {
    const float half = static_cast<float>(kTrailCells / 2);
    out[0] = (std::isfinite(eye.x) ? std::floor(eye.x / kTrailCell) : 0.f) - half;
    out[1] = (std::isfinite(eye.z) ? std::floor(eye.z / kTrailCell) : 0.f) - half;
    out[2] = kTrailCell;
    out[3] = static_cast<float>(kTrailCells);
}

}  // namespace

SnowLayerConstants SnowLayerOf(Vec3 eye, std::span<const WetWay> ways, std::span<const int> maps, size_t layer, uint32_t trailCount, bool trails) {
    SnowLayerConstants k{};
    const Vec3 d = layer < ways.size() ? ways[layer].way : Vec3{0, -1, 0};
    Vec3 a, b;
    AcrossOf(d, a, b);
    auto dot = [](Vec3 x, Vec3 y) { return x.x * y.x + x.y * y.y + x.z * y.z; };
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) eye = {0, 0, 0};
    // The middle in whole cells across the way (the grid does not swim as the eye moves).
    const float u = std::floor(dot(eye, a) / kSnowCell) * kSnowCell, v = std::floor(dot(eye, b) / kSnowCell) * kSnowCell, w = dot(eye, d);
    const Vec3 m{a.x * u + b.x * v + d.x * w, a.y * u + b.y * v + d.y * w, a.z * u + b.z * v + d.z * w};
    k.across[0] = a.x, k.across[1] = a.y, k.across[2] = a.z, k.across[3] = kSnowCell;
    k.along[0] = b.x, k.along[1] = b.y, k.along[2] = b.z, k.along[3] = static_cast<float>(kSnowCells);
    k.way[0] = d.x, k.way[1] = d.y, k.way[2] = d.z, k.way[3] = static_cast<float>(layer);
    k.middle[0] = m.x, k.middle[1] = m.y, k.middle[2] = m.z;
    for (size_t j = 0; j < 4; ++j) {
        const bool has = j < ways.size() && j < kWetWays;
        k.snow[j][0] = has && std::isfinite(ways[j].snow) ? std::clamp(ways[j].snow, 0.f, kMostSnowDepth) : 0.f;
        k.snow[j][1] = has && j < maps.size() ? static_cast<float>(maps[j]) : -1.f;
    }
    if (trails) TrailFirstCell(eye, k.trail);
    k.trailMore[0] = static_cast<float>(trailCount);
    return k;
}

SnowTrailConstants SnowTrailOf(Vec3 eye, uint32_t count, float fill, int upMap) {
    SnowTrailConstants k{};
    TrailFirstCell(eye, k.trail);
    k.more[0] = static_cast<float>(count);
    k.more[1] = std::isfinite(fill) ? std::max(fill, 0.f) : 0.f;
    k.more[2] = (std::isfinite(eye.y) ? eye.y : 0.f) - 70.f;  // (below what walks: the map from below starts 80 m under the eye)
    k.more[3] = static_cast<float>(upMap);
    return k;
}

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
    std::vector<float> data(4 * (1 + 2 * kWetWays), 0.f);
    data[0] = static_cast<float>(count);
    for (size_t k = 0; k < count; ++k) {
        float* way = &data[4 * (1 + k)];
        way[0] = std::clamp(ways[k].sun, 0.f, 1.f);
        way[1] = std::clamp(ways[k].shade, 0.f, 1.f);
        way[2] = static_cast<float>(k < maps.size() ? maps[k] : -1);
        way[3] = std::isfinite(ways[k].seep) ? std::clamp(ways[k].seep, 0.f, 4.f) : 0.f;
        data[4 * (1 + kWetWays + k)] = std::isfinite(ways[k].snow) ? std::clamp(ways[k].snow, 0.f, kMostSnowDepth) : 0.f;
    }
    return data;
}

}  // namespace rm::weather
