// The renderer's shaders (HLSL text): see RendererShaders.h.

#include "rm_engine/space3d/runtime/RendererShaders.h"

#include "rm_engine/space3d/runtime/SkyShader.h"

namespace rm::space3d {

namespace {

// Matrices are stored column by column (m[column][row]), which is HLSL's default packing, so they
// go to the GPU as they are and mul(matrix, vector) means matrix * vector.
// (each a literal of its own: one may hold at most 16 KB)
constexpr char kShadersFirst[] = R"(
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4x4 lightViewProj;  // ShadowVS: world -> the shadow map being drawn (x, y -1..1, depth 0..1)
    float4 eye;              // xyz: where the camera is (world)
    float4 fog;              // rgb: its colour (linear); a: how thick (per metre; 0: none) - RenderFrame::fog
};
// The fog (RenderFrame::fog): the farther, the more of its colour (linear, before ToScreen).
float3 Fogged(float3 c, float3 wpos) {
    if (fog.a <= 0) return c;
    return lerp(fog.rgb, c, exp(-fog.a * distance(wpos, eye.xyz)));
}
// Every thing's and material's numbers of the frame, written once a frame (Renderer3D.cpp's things_).
// A draw says where its thing and its material are (DRAW: x, y, in float4s):
//   a thing:    world (4 columns), normalWorld (3 columns: the inverse transpose of world - normals
//               stay at right angles), color (the screen's), highlight (a glow at the edges: rgb, a = how strong)
//   a material: material (its colour, linear: times `color`), paint (x: 1 = a colour picture; y: alpha
//               0 opaque / 1 mask / 2 blend; z: mask's cutoff; w: 1 = a normal map), pbr (x: metallic,
//               y: roughness, z: the normal map's strength; w: 1 = a metal / rough picture), emissive
//               (rgb: the light it gives, linear; w: 1 = an emission picture)
Buffer<float4> things : register(t6);
static float4x4 world;
static float3x3 normalWorld;
static float4 color, highlight, material, paint, pbr, emissive;
void LoadPlace(uint at) {
    world = transpose(float4x4(things.Load(at), things.Load(at + 1), things.Load(at + 2), things.Load(at + 3)));
    normalWorld = transpose(float3x3(things.Load(at + 4).xyz, things.Load(at + 5).xyz, things.Load(at + 6).xyz));
}
void LoadLook(uint2 draw) {
    color = things.Load(draw.x + 7);
    highlight = things.Load(draw.x + 8);
    material = things.Load(draw.y);
    paint = things.Load(draw.y + 1);
    pbr = things.Load(draw.y + 2);
    emissive = things.Load(draw.y + 3);
}
// The lights of the frame (Lighting.h's LightBlock: set once per frame).
cbuffer Lights : register(b1) {
    float4 lightInfo;         // x: how many
    float4 ambientSky;        // rgb: the ambient light on faces turned up
    float4 ambientGround;     // rgb: ... turned down
    float4 lightPos[8];       // xyz: where (point, spot); w: 0 directional, 1 point, 2 spot
    float4 lightDir[8];       // xyz: where the light goes; w: the range
    float4 lightColor[8];     // rgb: colour x strength; w: 1 = its shadows are the sun's maps
    float4 lightSpot[8];      // x: cos of the cone's edge, y: cos where it starts to fade
    float4 lightShadow[8];    // point / spot: x: its first map (-1: none); y: normal offset per distance; z: depth bias at distance 1
};
// The shadow maps of the frame (Renderer3D.cpp's ShadowConstants).
cbuffer Shadows : register(b2) {
    float4x4 sunViewProj[3];    // world -> the sun's maps (one, or the cascades)
    float4x4 localViewProj[8];  // world -> the point / spot lights' maps
    float4 sunInfo[3];          // x: where the map ends in front of the eye; y: normal offset (world); z: depth bias
    float4 shadowInfo;          // x: how many sun maps (0: none); y: a texel of a sun's map (its uv); z: ... of the others
    float4 viewForward;         // xyz: where the camera looks
    float4 atlas;               // how the maps lie side by side in their texture: xy: the sun's columns, rows; zw: the others'
};
// Each kind of shadow map lies side by side (tiles) in one texture (an array of maps would need
// ps_4_1 to be compared by the sampler).
Texture2D sunMaps : register(t0);
Texture2D localMaps : register(t5);
SamplerComparisonState shadowSampler : register(s0);
Texture2D baseMap : register(t1);        // sRGB: read as linear
Texture2D metalRoughMap : register(t2);  // b: metal, g: rough
Texture2D normalMap : register(t3);      // tangent space (x right, y up, z out)
Texture2D emissionMap : register(t4);    // sRGB
SamplerState baseSampler : register(s1);

static const float PI = 3.14159265;

struct LineIn  { float3 pos : POSITION; float4 col : COLOR; };
struct LineOut { float4 pos : SV_Position; float4 col : COLOR; };
LineOut LineVS(LineIn i) {
    LineOut o;
    o.pos = mul(viewProj, float4(i.pos, 1));
    o.col = i.col;
    return o;
}
float4 LinePS(LineOut i) : SV_Target { return i.col; }

struct MeshIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint2 draw : DRAW; };
struct MeshOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float3 wpos : TEXCOORD2; nointerpolation uint2 draw : DRAW; };
MeshOut MeshVS(MeshIn i) {
    LoadPlace(i.draw.x);
    MeshOut o;
    float4 w = mul(world, float4(i.pos, 1));
    o.pos = mul(viewProj, w);
    o.nrm = mul(normalWorld, i.nrm);
    o.wpos = w.xyz;
    o.uv = i.uv;
    o.draw = i.draw;
    return o;
}

// 1 = in the light, 0 = in shadow: the map `tile` of `maps` (tiles lie `grid` columns x rows) at p
// (after the divide: x, y -1..1, depth 0..1). 3 x 3 looks, each already a blend of 2 x 2 texels
// (PCF), kept inside the tile. Outside the map: lit.
float ShadowLook(Texture2D maps, float2 grid, float3 p, float tile, float texel, float bias) {
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return 1;
    float d = min(p.z, 1) - bias;  // beyond the map's depth: behind everything
    float2 corner = float2(tile - grid.x * floor(tile / grid.x), floor(tile / grid.x));
    float sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            float2 t = clamp(uv + float2(x, y) * texel, texel, 1 - texel);
            sum += maps.SampleCmpLevelZero(shadowSampler, (corner + t) / grid, d);
        }
    }
    return sum / 9;
}

// The sun: the first of its maps that reaches as far as the point is in front of the eye (one map,
// or the cascades: near ones sharp). Looked up a little off the surface (normal offset): a surface
// does not shadow itself. Beyond the last: lit.
float SunShadow(float3 wpos, float3 n) {
    int count = (int)shadowInfo.x;
    float ahead = dot(wpos - eye.xyz, viewForward.xyz);
    [loop] for (int c = 0; c < count; ++c) {
        if (ahead <= sunInfo[c].x) {
            float4 p = mul(sunViewProj[c], float4(wpos + n * sunInfo[c].y, 1));
            return ShadowLook(sunMaps, atlas.xy, p.xyz / p.w, c, shadowInfo.y, sunInfo[c].z);
        }
    }
    return 1;
}

// A point light (the side of its cube the point is on: the largest of |x| |y| |z|, in the order
// +X -X +Y -Y +Z -Z) or a spot (its one map). The depth of a perspective map is not even: the bias
// shrinks with the distance.
float LocalShadow(int l, float3 wpos, float3 n) {
    float slice = lightShadow[l].x;
    if (slice < 0) return 1;
    float3 from = wpos - lightPos[l].xyz;
    if (lightPos[l].w < 1.5) {
        float3 a = abs(from);
        if (a.x >= a.y && a.x >= a.z) slice += from.x > 0 ? 0 : 1;
        else if (a.y >= a.z) slice += from.y > 0 ? 2 : 3;
        else slice += from.z > 0 ? 4 : 5;
    }
    float4 p = mul(localViewProj[(int)slice], float4(wpos + n * (lightShadow[l].y * length(from)), 1));
    if (p.w <= 1e-4) return 1;
    return ShadowLook(localMaps, atlas.zw, p.xyz / p.w, slice, shadowInfo.z, lightShadow[l].z / p.w);
}

float ShadowOf(int l, float3 wpos, float3 n) { return lightColor[l].w > 0.5 ? SunShadow(wpos, n) : LocalShadow(l, wpos, n); }
)";

constexpr char kShadersLast[] = R"(
// How much of a light is left at a point (its fade with distance / out of the cone), and from where
// (toLight).
float FadeAt(int l, float3 wpos, out float3 toLight) {
    toLight = -lightDir[l].xyz;
    float fade = 1;
    if (lightPos[l].w > 0.5) {
        float3 d = lightPos[l].xyz - wpos;
        float dist = length(d);
        toLight = d / max(dist, 1e-4);
        float x = saturate(1 - dist / lightDir[l].w);
        fade = x * x;
        if (lightPos[l].w > 1.5) fade *= smoothstep(lightSpot[l].x, lightSpot[l].y, dot(-toLight, lightDir[l].xyz));
    }
    return fade;
}

// One light at a point: what comes (colour x strength x its fade) and from where (toLight). The
// light's numbers are made by Lighting.h.
float3 LightAt(int l, float3 wpos, out float3 toLight) { return lightColor[l].rgb * FadeAt(l, wpos, toLight); }

// The light leaving a surface towards the eye (linear): the ambient light (from everywhere, the sky's
// from above - even in shadow) and each light (diffuse + GGX highlight; its shadow if it has one).
float3 Shade(float3 albedo, float metal, float rough, float3 n, float3 v, float3 wpos) {
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);  // how much a surface mirrors, head on
    float3 diffuse = albedo * (1 - metal);                       // metal has no diffuse colour
    float a = rough * rough;
    float a2 = a * a;
    float nv = max(dot(n, v), 1e-4);
    float k = (rough + 1) * (rough + 1) / 8;
    // The sky: diffuse from the normal's side, and a dim reflection of it (Fresnel by the angle).
    float sky = 0.5 + 0.5 * n.y;
    float skyMirror = 0.5 + 0.5 * reflect(-v, n).y;
    float3 fSky = f0 + (max(float3(1 - rough, 1 - rough, 1 - rough), f0) - f0) * pow(1 - nv, 5);
    // The ambient light (環境光, linear): the ground's from below .. the sky's from above.
    float3 c = diffuse * lerp(ambientGround.rgb, ambientSky.rgb, sky) + fSky * lerp(ambientGround.rgb, ambientSky.rgb, skyMirror) * (1 - 0.6 * rough);
    int count = (int)lightInfo.x;
    [loop] for (int l = 0; l < count; ++l) {
        float3 toLight;
        float3 e = LightAt(l, wpos, toLight);
        float nl = saturate(dot(n, toLight));
        if (nl <= 0 || all(e <= 0)) continue;
        e *= ShadowOf(l, wpos, n);  // 1 for a light without shadows
        float3 h = normalize(toLight + v);
        float nh = saturate(dot(n, h));
        float vh = saturate(dot(v, h));
        float dd = nh * nh * (a2 - 1) + 1;
        float D = a2 / (PI * dd * dd);                                     // GGX: how many tiny faces turn to h
        float G = nl / (nl * (1 - k) + k) * (nv / (nv * (1 - k) + k));     // Smith: the ones not hidden
        float3 F = f0 + (1 - f0) * pow(1 - vh, 5);                         // Schlick: more mirror at a slant
        float3 spec = D * G * F / max(4 * nl * nv, 1e-4);
        // Strength 1 on a white face head on = 0.85 (the look the scene had before lights).
        c += ((1 - F) * diffuse + PI * spec) * e * (nl * 0.85);
    }
    return c;
}

// The normal bent by the normal map. The tangents come from how the position and the UV change
// from pixel to pixel (a "cotangent frame"): no tangents are needed in the vertices.
float3 Bend(float3 n, MeshOut i) {
    float3 t = normalMap.Sample(baseSampler, i.uv).xyz * 2 - 1;  // (sampled before any branch)
    if (paint.w > 1.5) t.z = sqrt(saturate(1 - dot(t.xy, t.xy)));  // two channels (BC5): z from x and y
    float3 dp1 = ddx(i.wpos), dp2 = ddy(i.wpos);
    float2 du1 = ddx(i.uv), du2 = ddy(i.uv);
    if (paint.w < 0.5) return n;
    float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    float3 T = dp2perp * du1.x + dp1perp * du2.x;
    float3 B = dp2perp * du1.y + dp1perp * du2.y;  // where V grows: down the picture
    float m = max(dot(T, T), dot(B, B));
    if (m < 1e-20) return n;  // no UVs to follow
    float s = rsqrt(m);
    t.xy *= pbr.z;
    return normalize(T * (s * t.x) - B * (s * t.y) + n * t.z);  // the map's +Y is up the picture
}

// The surface's own colour: the draw's x the material's x the picture. Alpha by the material's mode
// (opaque: a picture's alpha is no hole; mask: a hole below the cutoff; blend: see-through).
float4 Paint(MeshOut i) {
    float4 c = float4(ToLinear(color.rgb), color.a) * material;
    if (paint.x > 0.5) c *= baseMap.Sample(baseSampler, i.uv);
    if (paint.y > 0.5 && paint.y < 1.5) clip(c.a - paint.z);  // mask: a hole, or not
    if (paint.y < 1.5) c.a = color.a;                          // only blend sees through by its alpha
    return c;
}

float4 MeshPS(MeshOut i) : SV_Target {
    LoadLook(i.draw);
    float4 base = Paint(i);
    // The side seen is the side facing the eye (the back of an open model is lit as its own side).
    // Told from the eye, not from the triangle's winding on the screen, so a mirrored model or a
    // flipped projection never lights the wrong side.
    float3 n = normalize(i.nrm);
    float3 toEye = normalize(eye.xyz - i.wpos);
    if (dot(n, toEye) < 0) n = -n;
    float rim = pow(1 - saturate(dot(n, toEye)), 2.5) * highlight.a;  // (by the shape, not the normal map)
    float3 shape = n;  // (a plugin's touch goes by the shape too)
    n = Bend(n, i);
    float4 mr = metalRoughMap.Sample(baseSampler, i.uv);
    if (pbr.w > 1.5) mr = mr.rrrr;  // one channel (BC4): the grey value is both
    float metal = pbr.x * (pbr.w > 0.5 ? mr.b : 1);
    float rough = clamp(pbr.y * (pbr.w > 0.5 ? mr.g : 1), 0.04, 1);
    Surface(base.rgb, rough, metal, i.wpos, shape);  // a plugin's touch (RenderFrame::surface: 天候's 濡れ)
    rough = clamp(rough, 0.04, 1);
    float3 glow = emissive.rgb * (emissive.w > 0.5 ? emissionMap.Sample(baseSampler, i.uv).rgb : float3(1, 1, 1));
    float3 rgb = ToScreen(Fogged(Shade(base.rgb, metal, rough, n, toEye, i.wpos) + glow, i.wpos));
    // Selected: the edges (where the surface turns away from the eye) glow, the colours stay.
    return float4(lerp(rgb, highlight.rgb, rim), base.a);
}

// The floor: nothing of its own, only the shadows (seen from above) - the darkest one of a light
// that reaches it.
float4 FloorPS(MeshOut i) : SV_Target {
    LoadLook(i.draw);
    if (eye.y < 0) discard;  // seen from below the floor
    float3 up = float3(0, 1, 0);
    float dark = 0;
    int count = (int)lightInfo.x;
    [loop] for (int l = 0; l < count; ++l) {
        if (lightColor[l].w < 0.5 && lightShadow[l].x < 0) continue;  // no shadows
        float3 toLight;
        float fade = FadeAt(l, i.wpos, toLight);
        float r = saturate(saturate(dot(up, toLight)) * fade * 4);
        if (r > 0) dark = max(dark, (1 - ShadowOf(l, i.wpos, up)) * r);
    }
    return float4(color.rgb, color.a * dark);
}

float4 ShadowVS(MeshIn i) : SV_Position {
    LoadPlace(i.draw.x);
    return mul(lightViewProj, mul(world, float4(i.pos, 1)));
}

// A tile of a shadow map laid back from its kept layer (what stays put: the same tile of another
// texture), exactly, before the moving things are drawn over it. Drawn with ClearVS's triangle.
Texture2D layerMap : register(t7);
float CopyDepthPS(float4 pos : SV_Position) : SV_Depth { return layerMap.Load(int3(pos.xy, 0)).r; }

// A tile of a shadow map cleared: one triangle over the whole viewport at depth 1 (drawn with the
// depth test off). ClearDepthStencilView would clear every tile.
float4 ClearVS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 1, 1);
}
)";

// No plugin's touch: the surface as it is.
constexpr char kNoSurface[] = R"(
void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n) {}
)";

}  // namespace

const char kDirectionMapHlsl[] = R"(
// 向きの地図 (RenderFrame::mapDirections): the depth of the hidden shapes (RenderFrame::mapShapes) seen
// along each direction, tiles side by side in one texture (Lighting.h's PlanCover; RendererShaders.h's
// DirectionMapConstants).
cbuffer DirectionMaps : register(b6) {
    float4x4 mapViewProj[8];  // world -> a map (x, y -1..1 across the way, depth 0..1 along it)
    float4 mapInfo[8];        // x: 1 = there is one; y, z: its tile's left, top (pixels); w: its depth range (world)
    float4 mapSize;           // x: a map's size (pixels); y: how many
};
Texture2D directionMaps : register(t8);

// How far a point is past the first hidden shape on its line along map `k` (world units; < 0: before
// it). `p`: the map's matrix times the point. Nothing on the line, or out of the map: far before.
float MapPastAt(int k, float4 p) {
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0 || uv.x >= 1 || uv.y < 0 || uv.y >= 1) return -1e30;
    float d = directionMaps.Load(int3(uv * mapSize.x + mapInfo[k].yz, 0)).r;
    return d >= 1 ? -1e30 : (p.z - d) * mapInfo[k].w;
}
float MapPast(int k, float3 w) {
    if (k < 0 || mapInfo[k].x < 0.5) return -1e30;
    return MapPastAt(k, mul(mapViewProj[k], float4(w, 1)));
}
)";

const char kSurfaceHlsl[] = R"(
// A plugin's touch on every surface (RenderFrame::surface), before it is lit: its code defines
//   void Surface(inout float3 albedo, inout float rough, float metal, float3 wpos, float3 n)
// (albedo: linear; rough 0.04 .. 1; wpos: where in the world; n: the surface's normal, by its shape)
// and reads its numbers here. It may use SunShadow(wpos, n), MapPast(k, w) and shadowInfo.
cbuffer SurfaceData : register(b5) {
    float4 surfaceData[16];
};
)";

std::string RendererShaderText(std::string_view surface) {
    std::string text = std::string(kColorHlsl) + kShadersFirst + kDirectionMapHlsl + kSurfaceHlsl;
    text += surface.empty() ? std::string_view(kNoSurface) : surface;
    text += "\n";
    text += kShadersLast;
    return text;
}

}  // namespace rm::space3d
