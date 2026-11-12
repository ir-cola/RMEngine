#include "rm_engine/space3d/runtime/Renderer3D.h"

#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/TextureFile.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <unordered_map>

namespace rm::space3d {

namespace {

// Matrices are stored column by column (m[column][row]), which is HLSL's default packing, so they
// go to the GPU as they are and mul(matrix, vector) means matrix * vector.
constexpr char kShaders[] = R"(
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4x4 lightViewProj;  // ShadowVS: world -> the shadow map being drawn (x, y -1..1, depth 0..1)
    float4 eye;              // xyz: where the camera is (world)
};
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

// The screen's numbers (sRGB) <-> linear light, per component.
float3 ToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4); }
float3 ToScreen(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055;
}

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
    n = Bend(n, i);
    float4 mr = metalRoughMap.Sample(baseSampler, i.uv);
    if (pbr.w > 1.5) mr = mr.rrrr;  // one channel (BC4): the grey value is both
    float metal = pbr.x * (pbr.w > 0.5 ? mr.b : 1);
    float rough = clamp(pbr.y * (pbr.w > 0.5 ? mr.g : 1), 0.04, 1);
    float3 glow = emissive.rgb * (emissive.w > 0.5 ? emissionMap.Sample(baseSampler, i.uv).rgb : float3(1, 1, 1));
    float3 rgb = ToScreen(Shade(base.rgb, metal, rough, n, toEye, i.wpos) + glow);
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

// The constant buffer `Frame`.
struct FrameConstants {
    float viewProj[16];
    float lightViewProj[16];
    float eye[4];
};
static_assert(sizeof(FrameConstants) % 16 == 0);

// The constant buffer `Shadows`.
struct ShadowConstants {
    float sunViewProj[kCascades][16];
    float localViewProj[kLocalShadowSlices][16];
    float sunInfo[kCascades][4];
    float shadowInfo[4];
    float viewForward[4];
    float atlas[4];
};
static_assert(sizeof(ShadowConstants) % 16 == 0);
static_assert(sizeof(MeshVertex) == 32, "MeshVertex goes to the GPU as it is (POSITION, NORMAL, TEXCOORD)");

void CopyMatrix(float out[16], const Mat4& m) { std::memcpy(out, &m.m[0][0], sizeof(float) * 16); }

// The inverse transpose (a scale of 0 cannot be inverted: then the matrix itself, the box is flat anyway).
Mat4 NormalMatrix(const Mat4& world) {
    const auto inv = Inverse(world);
    if (!inv) return world;
    Mat4 t;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) t.m[c][r] = inv->m[r][c];
    }
    return t;
}

bool Compile(const char* entry, const char* target, Microsoft::WRL::ComPtr<ID3DBlob>& out, std::string* error) {
    Microsoft::WRL::ComPtr<ID3DBlob> messages;
    const HRESULT hr = D3DCompile(kShaders, sizeof(kShaders) - 1, "space3d", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &messages);
    if (FAILED(hr)) {
        if (error) {
            *error = std::format("シェーダー {} をコンパイルできませんでした (0x{:08X})", entry, static_cast<uint32_t>(hr));
            if (messages) *error += std::string(": ") + static_cast<const char*>(messages->GetBufferPointer());
        }
        return false;
    }
    return true;
}

// Every triangle turned so its front (counter-clockwise) faces the way its corners' normals do.
void FaceOut(MeshData& m) {
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const MeshVertex& a = m.vertices[m.indices[i]];
        const MeshVertex& b = m.vertices[m.indices[i + 1]];
        const MeshVertex& c = m.vertices[m.indices[i + 2]];
        if (Dot(Cross(b.position - a.position, c.position - a.position), a.normal + b.normal + c.normal) < 0) {
            std::swap(m.indices[i + 1], m.indices[i + 2]);
        }
    }
}

// The unit cube: 6 faces x 4 corners (each face its own normal), 2 triangles per face.
MeshData UnitCube() {
    MeshData m;
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        // Two axes across the face.
        const int a = n[f][0] != 0 ? 1 : 0;
        const int b = n[f][2] != 0 ? 1 : 2;
        for (int k = 0; k < 4; ++k) {
            float p[3] = {n[f][0] * 0.5f, n[f][1] * 0.5f, n[f][2] * 0.5f};
            p[a] = (k == 1 || k == 2) ? 0.5f : -0.5f;
            p[b] = (k >= 2) ? 0.5f : -0.5f;
            m.vertices.push_back({{p[0], p[1], p[2]}, {n[f][0], n[f][1], n[f][2]}, {(k == 1 || k == 2) ? 1.f : 0.f, k >= 2 ? 1.f : 0.f}});
        }
        const uint32_t base = static_cast<uint32_t>(f * 4);
        for (const uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + k);
    }
    FaceOut(m);
    ComputeBounds(m);
    return m;
}

// The floor's plane (y = 0), far wider than any scene (it only shows where shadows fall).
MeshData FloorPlane() {
    constexpr float s = 5000.f;
    MeshData m;
    for (const auto& [x, z] : {std::pair{-s, -s}, {s, -s}, {s, s}, {-s, s}}) m.vertices.push_back({{x, 0, z}, {0, 1, 0}, {}});
    m.indices = {0, 1, 2, 0, 2, 3};
    FaceOut(m);
    ComputeBounds(m);
    return m;
}

// Shadow maps of one kind: tiles of `size` pixels side by side in one texture (up to 4 a row), and
// what each tile holds now (a fingerprint of the light's matrix and of what was drawn into it). A tile
// whose fingerprint has not changed is not drawn again - in any renderer of the device.
struct ShadowMaps {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    uint32_t tiles = 0, cols = 0, rows = 0, size = 0;
    std::vector<uint64_t> holds;  // per tile (0: nothing known)
    // The kept layer: the same tiles holding only what stays put (Box / MeshDraw::moving false), made
    // when a map first has moving things in it.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> layerTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> layerSrv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> layerDsv;
    std::vector<uint64_t> layerHolds;
};

}  // namespace

struct Renderer::Shared {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11VertexShader> lineVs, meshVs, shadowVs, clearVs;
    Ptr<ID3D11PixelShader> linePs, meshPs, floorPs, copyPs;
    Ptr<ID3D11InputLayout> lineLayout, meshLayout;
    Ptr<ID3D11Buffer> frame, lights, shadows;  // the constant buffers (b0, b1, b2)
    std::shared_ptr<const GpuMesh> cube, floor;
    Ptr<ID3D11RasterizerState> raster, shadowRaster;
    Ptr<ID3D11DepthStencilState> depth, depthNoWrite, depthClear;
    Ptr<ID3D11BlendState> blend;
    Ptr<ID3D11SamplerState> textureSampler;  // pictures: repeat, smooth, mipmapped
    Ptr<ID3D11SamplerState> sharpSampler;    // pictures set to くっきり (import setting): the nearest pixel
    Ptr<ID3D11SamplerState> shadowSampler;
    // The shadow maps (depth seen from the lights): the sun's (1, or kCascades) and the point / spot
    // lights' (kLocalShadowSlices). Made when first needed.
    ShadowMaps sun, local;
    UINT samples = 1;

    bool Init(Renderer& owner, std::string* error);
    bool EnsureMaps(ShadowMaps& maps, uint32_t tiles, uint32_t size);
    bool EnsureLayer(ShadowMaps& maps);
};

namespace {

// One Shared per device: made by the first renderer on it, gone with the last. A device made again
// (the GPU was lost: platform::Gfx::Generation) is another device, so it gets its own.
std::mutex g_sharedMutex;
std::unordered_map<ID3D11Device*, std::weak_ptr<Renderer::Shared>>& SharedByDevice() {
    static std::unordered_map<ID3D11Device*, std::weak_ptr<Renderer::Shared>> map;
    return map;
}

// A fingerprint (64 bits) of some 4-byte words (matrices, serials), added to `h`: a word at a time
// (multiply and fold), not a byte - it runs for every thing in every shadow map.
uint64_t HashWords(uint64_t h, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t w;
        std::memcpy(&w, p + i, 4);
        h = (h ^ w) * 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
    }
    return h;
}
constexpr uint64_t kFingerprintStart = 14695981039346656037ull;

// Whether two materials are drawn with the same pictures (null: none) - then they can be one draw.
bool SamePictures(const DrawMaterial* a, const DrawMaterial* b) {
    if (a == b) return true;
    auto texture = [](const DrawMaterial* m, int k) -> const void* {
        if (!m) return nullptr;
        switch (k) {
            case 0: return m->texture.get();
            case 1: return m->metalRough.get();
            case 2: return m->normal.get();
            default: return m->emission.get();
        }
    };
    for (int k = 0; k < 4; ++k) {
        if (texture(a, k) != texture(b, k)) return false;
    }
    return true;  // (the sampler follows the colour picture, the same one)
}

const char kSharp = 0;  // PictureSet's mark for a くっきり colour picture

std::atomic<uint64_t> g_meshSerial{0};  // GpuMesh::serial: never the same twice

}  // namespace

bool Renderer::Shared::Init(Renderer& owner, std::string* error) {
    Ptr<ID3DBlob> lvs, lps, mvs, mps, fps, svs, cvs, cps;
    if (!Compile("LineVS", "vs_4_0", lvs, error) || !Compile("LinePS", "ps_4_0", lps, error) || !Compile("MeshVS", "vs_4_0", mvs, error) ||
        !Compile("MeshPS", "ps_4_0", mps, error) || !Compile("FloorPS", "ps_4_0", fps, error) || !Compile("ShadowVS", "vs_4_0", svs, error) ||
        !Compile("ClearVS", "vs_4_0", cvs, error) || !Compile("CopyDepthPS", "ps_4_0", cps, error)) {
        return false;
    }
    auto fail = [&](const char* what) {
        if (error) *error = std::format("3D の描画の準備に失敗しました: {}", what);
        return false;
    };
    if (FAILED(device->CreateVertexShader(lvs->GetBufferPointer(), lvs->GetBufferSize(), nullptr, &lineVs)) ||
        FAILED(device->CreatePixelShader(lps->GetBufferPointer(), lps->GetBufferSize(), nullptr, &linePs)) ||
        FAILED(device->CreatePixelShader(mps->GetBufferPointer(), mps->GetBufferSize(), nullptr, &meshPs)) ||
        FAILED(device->CreatePixelShader(fps->GetBufferPointer(), fps->GetBufferSize(), nullptr, &floorPs)) ||
        FAILED(device->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(), nullptr, &shadowVs)) ||
        FAILED(device->CreateVertexShader(cvs->GetBufferPointer(), cvs->GetBufferSize(), nullptr, &clearVs)) ||
        FAILED(device->CreatePixelShader(cps->GetBufferPointer(), cps->GetBufferSize(), nullptr, &copyPs)) ||
        FAILED(device->CreateVertexShader(mvs->GetBufferPointer(), mvs->GetBufferSize(), nullptr, &meshVs))) {
        return fail("シェーダー");
    }
    const D3D11_INPUT_ELEMENT_DESC lineIn[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    // The vertices (slot 0) and, per draw, where its thing and material are (slot 1: one "instance"
    // whose data is picked by the draw's start instance).
    const D3D11_INPUT_ELEMENT_DESC meshIn[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"DRAW", 0, DXGI_FORMAT_R32G32_UINT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    if (FAILED(device->CreateInputLayout(lineIn, 2, lvs->GetBufferPointer(), lvs->GetBufferSize(), &lineLayout)) ||
        FAILED(device->CreateInputLayout(meshIn, 4, mvs->GetBufferPointer(), mvs->GetBufferSize(), &meshLayout))) {
        return fail("入力レイアウト");
    }

    std::string why;
    cube = owner.Upload(UnitCube(), &why);
    floor = owner.Upload(FloorPlane(), &why);
    if (!cube || !floor) return fail("箱");
    D3D11_BUFFER_DESC cd{};
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.ByteWidth = sizeof(FrameConstants);
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cd, nullptr, &frame))) return fail("定数");
    cd.ByteWidth = sizeof(LightBlock);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &lights))) return fail("光");
    cd.ByteWidth = sizeof(ShadowConstants);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &shadows))) return fail("影");

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // open models show their inside too (lit as its own side: MeshPS asks the eye)
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    D3D11_RASTERIZER_DESC sd = rd;
    sd.MultisampleEnable = FALSE;
    sd.SlopeScaledDepthBias = 1.5f;  // surfaces seen from the light at a slant are pushed back more
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    D3D11_DEPTH_STENCIL_DESC dn = dd;
    dn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    D3D11_DEPTH_STENCIL_DESC dc = dd;  // a tile of a shadow map cleared (ClearVS: depth 1 everywhere)
    dc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_SAMPLER_DESC ts{};
    ts.Filter = D3D11_FILTER_ANISOTROPIC;
    ts.MaxAnisotropy = 8;
    ts.AddressU = ts.AddressV = ts.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;  // UVs past 0..1 repeat the picture
    ts.ComparisonFunc = D3D11_COMPARISON_NEVER;
    ts.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC tp = ts;  // くっきり: the nearest pixel (the smaller copies still far away)
    tp.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    tp.MaxAnisotropy = 1;
    // Compares the pixel's depth with the map's, blending the answers of the 2 x 2 texels around it.
    // Outside the map the depth is 1 (the farthest): nothing there casts a shadow.
    D3D11_SAMPLER_DESC ss{};
    ss.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    ss.BorderColor[0] = ss.BorderColor[1] = ss.BorderColor[2] = ss.BorderColor[3] = 1.f;
    ss.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    ss.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateRasterizerState(&rd, &raster)) || FAILED(device->CreateRasterizerState(&sd, &shadowRaster)) ||
        FAILED(device->CreateDepthStencilState(&dd, &depth)) || FAILED(device->CreateDepthStencilState(&dn, &depthNoWrite)) ||
        FAILED(device->CreateDepthStencilState(&dc, &depthClear)) || FAILED(device->CreateBlendState(&bl, &blend)) ||
        FAILED(device->CreateSamplerState(&ts, &textureSampler)) || FAILED(device->CreateSamplerState(&tp, &sharpSampler))) {
        return fail("描画の設定");
    }
    if (FAILED(device->CreateSamplerState(&ss, &shadowSampler))) return fail("影");

    UINT quality = 0;
    samples = 1;
    if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0) {
        UINT dq = 0;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &dq)) && dq > 0) samples = 4;
    }
    return true;
}

bool Renderer::Shared::EnsureMaps(ShadowMaps& maps, uint32_t tiles, uint32_t size) {
    if (maps.tiles >= tiles && maps.size == size && maps.srv) return true;
    maps = {};
    // Up to 4 tiles a row. One texture seen two ways: written as depth (D32), read as numbers (R32)
    // by the comparing sampler.
    const uint32_t cols = std::min(tiles, 4u), rows = (tiles + cols - 1) / cols;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = size * cols;
    td.Height = size * rows;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &maps.texture)) ||
        FAILED(device->CreateDepthStencilView(maps.texture.Get(), &dv, &maps.dsv)) ||
        FAILED(device->CreateShaderResourceView(maps.texture.Get(), &sv, &maps.srv))) {
        maps = {};
        return false;
    }
    maps.tiles = tiles;
    maps.cols = cols;
    maps.rows = rows;
    maps.size = size;
    maps.holds.assign(tiles, 0);
    return true;
}

bool Renderer::Shared::EnsureLayer(ShadowMaps& maps) {
    if (maps.layerSrv) return true;
    if (!maps.texture) return false;
    D3D11_TEXTURE2D_DESC td{};
    maps.texture->GetDesc(&td);  // the same tiles, the same way (written as D32, read as R32)
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &maps.layerTexture)) ||
        FAILED(device->CreateDepthStencilView(maps.layerTexture.Get(), &dv, &maps.layerDsv)) ||
        FAILED(device->CreateShaderResourceView(maps.layerTexture.Get(), &sv, &maps.layerSrv))) {
        maps.layerTexture.Reset();
        maps.layerDsv.Reset();
        maps.layerSrv.Reset();
        return false;
    }
    maps.layerHolds.assign(maps.tiles, 0);
    return true;
}

bool Renderer::Init(ID3D11Device* device, std::string* error) {
    device_ = device;
    static std::atomic<uint64_t> next{0};
    id_ = ++next;
    shared_.reset();
    std::lock_guard lock(g_sharedMutex);
    auto& map = SharedByDevice();
    std::erase_if(map, [](const auto& kv) { return kv.second.expired(); });
    if (auto found = map[device].lock()) {
        shared_ = std::move(found);
        return true;
    }
    auto made = std::make_shared<Shared>();
    made->device = device;
    if (!made->Init(*this, error)) {
        map.erase(device);
        return false;
    }
    map[device] = made;
    shared_ = std::move(made);
    return true;
}

std::shared_ptr<const GpuMesh> Renderer::Upload(const MeshData& mesh, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return nullptr;
    };
    if (!device_) return fail("3D の描画が始まっていません");
    if (mesh.vertices.empty() || mesh.indices.size() < 3) return fail("形がありません");
    if (mesh.vertices.size() > UINT32_MAX / sizeof(MeshVertex) || mesh.indices.size() > UINT32_MAX / sizeof(uint32_t)) {
        return fail("頂点が多すぎて GPU に載せられません");
    }
    // A mesh made by a plugin directly (not through the readers) may point past its vertices: the
    // part boxes below read vertices[indices[k]] on the CPU.
    if (std::string why; !IndicesInRange(mesh, &why)) {
        if (error) *error = why;
        return nullptr;
    }
    auto gpu = std::make_shared<GpuMesh>();
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;  // written once, never changed
    bd.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(MeshVertex));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{mesh.vertices.data(), 0, 0};
    if (FAILED(device_->CreateBuffer(&bd, &init, &gpu->vertices))) return fail("頂点を GPU に載せられませんでした (メモリーが足りない?)");
    bd.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = mesh.indices.data();
    if (FAILED(device_->CreateBuffer(&bd, &init, &gpu->indices))) return fail("インデックスを GPU に載せられませんでした (メモリーが足りない?)");
    gpu->serial = ++g_meshSerial;
    gpu->min = mesh.min;
    gpu->max = mesh.max;
    gpu->indexCount = static_cast<uint32_t>(mesh.indices.size() / 3 * 3);
    // The parts as they are (only whole triangles inside the indices); none: one part, no material.
    for (const MeshPart& part : mesh.parts) {
        const uint64_t end = static_cast<uint64_t>(part.firstIndex) + part.indexCount;
        if (part.indexCount >= 3 && end <= gpu->indexCount) gpu->parts.push_back({part.firstIndex, part.indexCount / 3 * 3, part.material});
    }
    if (gpu->parts.empty()) gpu->parts.push_back({0, gpu->indexCount, -1});
    for (const MeshPart& part : gpu->parts) {
        Vec3 lo = mesh.vertices[mesh.indices[part.firstIndex]].position, hi = lo;
        for (uint32_t k = part.firstIndex; k < part.firstIndex + part.indexCount; ++k) {
            const Vec3 p = mesh.vertices[mesh.indices[k]].position;
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        gpu->partCenters.push_back((lo + hi) * 0.5f);
    }
    return gpu;
}

std::shared_ptr<const GpuTexture> Renderer::UploadTexture(const platform::Image& rgba, bool srgb, std::string* error, bool sharp) {
    return UploadTexture(MakeTexturePixels(rgba, srgb), error, sharp);
}

std::shared_ptr<const GpuTexture> Renderer::UploadTexture(const TexturePixels& pixels, std::string* error, bool sharp) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return nullptr;
    };
    if (!device_) return fail("3D の描画が始まっていません");
    if (pixels.levels.empty()) return fail("画像が空です");
    const platform::Image& top = pixels.levels[0];
    if (top.width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || top.height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        return fail("画像が大きすぎて GPU に載せられません");
    }
    if (pixels.format == TextureFormat::BC7 && !SupportsBC7()) return fail("この GPU は BC7 を読めません");
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const platform::Image& l : pixels.levels) {
        if (l.pixels.size() < LevelBytes(pixels.format, l.width, l.height)) return fail("画像のデータが足りません");
        init.push_back({l.pixels.data(), static_cast<UINT>(RowPitch(pixels.format, l.width)), 0});
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = top.width;
    td.Height = top.height;
    td.MipLevels = static_cast<UINT>(pixels.levels.size());
    td.ArraySize = 1;
    // sRGB forms: the GPU reads linear light. The numbers are DXGI_FORMAT's (TextureFile.h).
    td.Format = static_cast<DXGI_FORMAT>(DxgiFormatOf(pixels.format, pixels.srgb));
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Ptr<ID3D11Texture2D> tex;
    auto gpu = std::make_shared<GpuTexture>();
    gpu->sharp = sharp;
    if (FAILED(device_->CreateTexture2D(&td, init.data(), &tex)) || FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &gpu->view))) {
        return fail("画像を GPU に載せられませんでした (メモリーが足りない?)");
    }
    gpu->width = top.width;
    gpu->height = top.height;
    gpu->srgb = pixels.srgb;
    gpu->format = pixels.format;
    gpu->madeBy = id_;
    for (const platform::Image& l : pixels.levels) gpu->bytes += l.pixels.size();
    return gpu;
}

bool Renderer::SupportsBC7() const { return device_ && device_->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0; }

bool Renderer::Resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_ && picture_) return true;
    picture_.Reset();
    multisample_.Reset();
    rtv_.Reset();
    depthTex_.Reset();
    dsv_.Reset();
    width_ = height_ = 0;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;  // shader resource: Direct2D draws it
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &picture_))) return false;
    ID3D11Texture2D* target = picture_.Get();
    samples_ = shared_->samples;
    if (samples_ > 1) {
        td.SampleDesc.Count = samples_;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &multisample_))) return false;
        target = multisample_.Get();
    }
    if (FAILED(device_->CreateRenderTargetView(target, nullptr, &rtv_))) return false;
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    td.SampleDesc.Count = samples_;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &depthTex_)) || FAILED(device_->CreateDepthStencilView(depthTex_.Get(), nullptr, &dsv_))) {
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool Renderer::LineCapacity(size_t vertices) {
    if (vertices <= lineCapacity_ && lineVb_) return true;
    const size_t cap = std::max<size_t>(vertices, lineCapacity_ * 2) + 64;
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = static_cast<UINT>(cap * sizeof(LineVertex));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    lineVb_.Reset();
    if (FAILED(device_->CreateBuffer(&bd, nullptr, &lineVb_))) {
        lineCapacity_ = 0;
        return false;
    }
    lineCapacity_ = cap;
    return true;
}

namespace {

void BindMesh(ID3D11DeviceContext* ctx, ID3D11Buffer* vb, ID3D11Buffer* ib) {
    const UINT stride = sizeof(MeshVertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
}

void Put(std::array<float, 4>& out, Color c) { out = {c.r, c.g, c.b, c.a}; }

// A dynamic buffer at least `bytes` big (made again, larger, when it is not).
bool EnsureDynamic(ID3D11Device* device, Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer, size_t& capacity, size_t bytes, UINT bind) {
    if (buffer && capacity >= bytes) return true;
    const size_t cap = std::max(bytes, capacity * 2) + 4096;
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = static_cast<UINT>(cap);
    bd.BindFlags = bind;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer.Reset();
    capacity = 0;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &buffer))) return false;
    capacity = cap;
    return true;
}

bool Fill(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer, const void* data, size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return false;
    std::memcpy(map.pData, data, bytes);
    ctx->Unmap(buffer, 0);
    return true;
}

}  // namespace

uint32_t Renderer::AddThing(const Mat4& world, Color color, Color highlight) {
    const auto at = static_cast<uint32_t>(things_.size());
    const Mat4 n = NormalMatrix(world);
    for (int c = 0; c < 4; ++c) things_.push_back({world.m[c][0], world.m[c][1], world.m[c][2], world.m[c][3]});
    for (int c = 0; c < 3; ++c) things_.push_back({n.m[c][0], n.m[c][1], n.m[c][2], 0});
    things_.emplace_back();
    Put(things_.back(), color);
    things_.emplace_back();
    Put(things_.back(), highlight);
    return at;
}

uint32_t Renderer::AddMaterial(const DrawMaterial& m) {
    const auto at = static_cast<uint32_t>(things_.size());
    const float mode = m.alpha == AlphaMode::Mask ? 1.f : m.alpha == AlphaMode::Blend ? 2.f : 0.f;
    things_.emplace_back();
    Put(things_.back(), m.color);
    // A picture's flag: 0 none, 1 a picture, 2 a picture with fewer channels (BC5 normals, BC4 numbers).
    auto flag = [](const TextureRef& t, int fewer) { return !t ? 0.f : ChannelsOf(t->format) == fewer ? 2.f : 1.f; };
    things_.push_back({m.texture ? 1.f : 0.f, mode, m.alphaCutoff, flag(m.normal, 2)});
    things_.push_back({m.metallic, m.roughness, m.normalScale, flag(m.metalRough, 1)});
    things_.push_back({m.emissive.r, m.emissive.g, m.emissive.b, m.emission ? 1.f : 0.f});
    return at;
}

uint32_t Renderer::AddDraw(uint32_t thing, uint32_t material) {
    const auto slot = static_cast<uint32_t>(draws_.size() / 2);
    draws_.push_back(thing);
    draws_.push_back(material);
    return slot;
}

bool Renderer::UploadFrame(ID3D11DeviceContext* ctx) {
    const size_t thingBytes = things_.size() * sizeof(things_[0]), drawBytes = draws_.size() * sizeof(uint32_t);
    const bool grew = !thingsBuffer_ || thingsCapacity_ < thingBytes;
    if (!EnsureDynamic(device_.Get(), thingsBuffer_, thingsCapacity_, thingBytes, D3D11_BIND_SHADER_RESOURCE) ||
        !EnsureDynamic(device_.Get(), drawsBuffer_, drawsCapacity_, std::max<size_t>(drawBytes, 8), D3D11_BIND_VERTEX_BUFFER)) {
        return false;
    }
    if (grew || !thingsView_) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.FirstElement = 0;
        sv.Buffer.NumElements = static_cast<UINT>(thingsCapacity_ / sizeof(things_[0]));
        thingsView_.Reset();
        if (FAILED(device_->CreateShaderResourceView(thingsBuffer_.Get(), &sv, &thingsView_))) return false;
    }
    return Fill(ctx, thingsBuffer_.Get(), things_.data(), thingBytes) && (draws_.empty() || Fill(ctx, drawsBuffer_.Get(), draws_.data(), drawBytes));
}

void Renderer::Emit(const GpuMesh* mesh, uint32_t firstIndex, uint32_t indexCount, uint32_t slot, const DrawMaterial* material) {
    if (cmds_.size() > mergeFloor_) {
        Cmd& last = cmds_.back();
        if (last.mesh == mesh && last.firstIndex == firstIndex && last.indexCount == indexCount && last.slot + last.instances == slot &&
            SamePictures(last.material, material)) {
            ++last.instances;  // the next instance of the same draw
            return;
        }
    }
    cmds_.push_back({mesh, firstIndex, indexCount, slot, material, 1});
}

uint32_t Renderer::PictureSet(const DrawMaterial* m) {
    if (!m || (!m->texture && !m->metalRough && !m->normal && !m->emission)) return 0;
    const std::array<const void*, 5> key{m->texture.get(), m->metalRough.get(), m->normal.get(), m->emission.get(),
                                         m->texture && m->texture->sharp ? static_cast<const void*>(&kSharp) : nullptr};
    const auto [it, added] = pictureSets_.try_emplace(key, static_cast<uint32_t>(pictureSets_.size() + 1));
    return it->second;
}

ID3D11Texture2D* Renderer::Render(ID3D11DeviceContext* ctx, const RenderFrame& f) {
    if (!Ready() || f.width == 0 || f.height == 0 || !Resize(f.width, f.height)) return nullptr;
    Shared& s = *shared_;

    // Everything filled: the boxes and the models (each with its own place and colour).
    items_.clear();
    for (const Box& b : f.boxes) {
        items_.push_back({s.cube.get(), &b.world, b.color, b.castShadow, {}, {0, 0, 0, 0}});
        items_.back().moving = b.moving;
    }
    for (const MeshDraw& m : f.meshes) {
        if (!m.mesh || m.mesh->indexCount == 0) continue;
        items_.push_back({m.mesh, &m.world, m.color, m.castShadow, m.materials, m.highlight});
        items_.back().moving = m.moving;
    }
    // Culling (always): what lies wholly outside the camera's view is left out of the picture.
    const Mat4 viewProj = f.projection * f.view;
    stats_ = {items_.size(), 0, 0, 0, 0, 0};
    const Frustum camera = FrustumOf(viewProj);
    for (Item& it : items_) {
        it.bounds = WorldBounds(*it.world, it.mesh->min, it.mesh->max);
        it.seen = BoxInFrustum(camera, it.bounds);
        if (it.seen) ++stats_.drawn;
    }
    Vec3 eye{0, 0, 0}, forward{0, 0, -1};
    if (const auto toWorld = Inverse(f.view)) {
        eye = TransformPoint(*toWorld, {0, 0, 0});
        forward = Normalize(TransformDirection(*toWorld, {0, 0, -1}));
    }
    // The lights (none: the one default directional light; more than kMaxLights: the nearest and
    // brightest) and the ambient light.
    LightSetup lights = MakeLights(f.lights, f.lightDirection, f.ambient, eye);

    // Every thing's and material's numbers, once for the frame (the draws only say which).
    things_.clear();
    draws_.clear();
    cmds_.clear();
    mergeFloor_ = 0;
    static const DrawMaterial kWhite;
    const uint32_t white = AddMaterial(kWhite);
    for (Item& it : items_) {
        it.thing = AddThing(*it.world, it.color, it.highlight);
        it.firstMaterial = static_cast<uint32_t>(things_.size());
        for (const MeshPart& part : it.mesh->parts) {
            AddMaterial(part.material >= 0 && static_cast<size_t>(part.material) < it.materials.size() ? it.materials[static_cast<size_t>(part.material)]
                                                                                                         : kWhite);
        }
    }
    auto materialOf = [](const Item& it, const MeshPart& part) -> const DrawMaterial& {
        return part.material >= 0 && static_cast<size_t>(part.material) < it.materials.size() ? it.materials[static_cast<size_t>(part.material)]
                                                                                               : kWhite;
    };
    auto isSeeThrough = [&](const Item& it, const MeshPart& part) {
        return it.color.a < 1.f || materialOf(it, part).alpha == AlphaMode::Blend;
    };
    auto partDraw = [&](const Item& it, const MeshPart& part) {
        const auto k = static_cast<uint32_t>(&part - it.mesh->parts.data());
        Emit(it.mesh, part.firstIndex, part.indexCount, AddDraw(it.thing, it.firstMaterial + k * 4), &materialOf(it, part));
    };

    // What casts, grouped by mesh (stable: DrawOrder.h) once for all the maps - a map's things of one
    // mesh become one instanced draw - each with a fingerprint of its mesh and place.
    keys_.clear();
    still_.clear();
    for (uint32_t i = 0; i < items_.size(); ++i) {
        Item& it = items_[i];
        if (!it.castShadow) continue;
        it.hash = HashWords(HashWords(kFingerprintStart, &it.mesh->serial, sizeof(it.mesh->serial)), it.world, sizeof(Mat4));
        keys_.push_back({0, 0, it.mesh->serial, 0});
        still_.push_back(i);
    }
    SortDraws(keys_, order_);
    casterOrder_.clear();
    for (const uint32_t k : order_) casterOrder_.push_back(still_[k]);

    // 1. The shadow maps to draw: what casts, seen from each light. Only what lies in a map is drawn
    // into it (each map's own view: a light's, a cascade's), and a map that would hold the same as it
    // does (the same matrix, the same things where they were) is not drawn again. A map with things
    // that move keeps a layer of what stays put (drawn again only when that changes) and lays it back
    // under the moving ones.
    ShadowConstants sc{};
    sc.viewForward[0] = forward.x, sc.viewForward[1] = forward.y, sc.viewForward[2] = forward.z;
    // The box the sun's maps are fitted to: what stays put, the moving things widening it only in
    // steps (Lighting.h's SunFitBounds: dragging one does not change the maps every frame).
    std::optional<Bounds> stillBox, movingBox;
    for (const uint32_t i : casterOrder_) {
        const Bounds& b = items_[i].bounds;
        std::optional<Bounds>& box = items_[i].moving ? movingBox : stillBox;
        box = box ? Bounds{{std::min(box->min.x, b.min.x), std::min(box->min.y, b.min.y), std::min(box->min.z, b.min.z)},
                           {std::max(box->max.x, b.max.x), std::max(box->max.y, b.max.y), std::max(box->max.z, b.max.z)}}
                  : b;
    }
    const std::optional<Bounds> fitted = SunFitBounds(stillBox, movingBox);
    const bool anyCaster = fitted.has_value();
    const Bounds casters = fitted.value_or(Bounds{});
    enum class MapMode { Whole, Layer, OverLayer };  // all of it / the kept layer / laid back + the moving ones
    struct MapWork {
        ShadowMaps* maps;
        uint32_t tile;
        Mat4 viewProj;
        size_t first, end;  // its draws in cmds_
        MapMode mode;
    };
    std::vector<MapWork> layers, work;
    // What the tiles will hold once drawn - written to them after the drawing (step 1 below), not
    // here: a frame that stops before (the frame's numbers could not go to the GPU) leaves the tiles
    // saying what they really hold, and they are drawn again next time.
    struct Holds {
        std::vector<uint64_t>* of;  // ShadowMaps::holds or layerHolds
        uint32_t tile;
        uint64_t value;
    };
    std::vector<Holds> holds;
    auto emitCasters = [&](const std::vector<uint32_t>& list) {
        for (const uint32_t i : list) Emit(items_[i].mesh, 0, items_[i].mesh->indexCount, AddDraw(items_[i].thing, white), nullptr);
    };
    auto planMap = [&](ShadowMaps& maps, uint32_t tile, const Mat4& lightViewProj) {
        const Frustum view = FrustumOf(lightViewProj);
        uint64_t still = HashWords(kFingerprintStart, &lightViewProj, sizeof(Mat4)), moving = kFingerprintStart;
        still_.clear();
        moving_.clear();
        for (const uint32_t i : casterOrder_) {
            const Item& it = items_[i];
            if (!BoxInFrustum(view, it.bounds)) continue;
            if (it.moving) {
                moving = HashWords(moving, &it.hash, sizeof(it.hash));
                moving_.push_back(i);
            } else {
                still = HashWords(still, &it.hash, sizeof(it.hash));
                still_.push_back(i);
            }
        }
        const uint64_t whole = moving_.empty() ? still : HashWords(still, &moving, sizeof(moving));
        if (maps.holds[tile] == whole) {  // drawn already (this frame's, the last's, or another renderer's)
            ++stats_.mapsKept;
            return;
        }
        holds.push_back({&maps.holds, tile, whole});
        ++stats_.mapsDrawn;
        mergeFloor_ = cmds_.size();
        const size_t first = cmds_.size();
        if (moving_.empty() || !s.EnsureLayer(maps)) {
            emitCasters(still_);
            emitCasters(moving_);
            work.push_back({&maps, tile, lightViewProj, first, cmds_.size(), MapMode::Whole});
            return;
        }
        if (maps.layerHolds[tile] != still) {
            holds.push_back({&maps.layerHolds, tile, still});
            emitCasters(still_);
            layers.push_back({&maps, tile, lightViewProj, first, cmds_.size(), MapMode::Layer});
            ++stats_.layersDrawn;
        }
        mergeFloor_ = cmds_.size();
        const size_t movingFirst = cmds_.size();
        emitCasters(moving_);
        work.push_back({&maps, tile, lightViewProj, movingFirst, cmds_.size(), MapMode::OverLayer});
    };
    // The sun: one map fitted to what casts, or the cascades along the view.
    int sunMaps = 0;
    if (f.shadows && anyCaster && lights.shadowLight >= 0) {
        const SunShadowPlan plan = PlanSunShadows(lights.shadowDirection, casters, f.view, f.projection, kShadowMapSize);
        if (plan.count > 0 && s.EnsureMaps(s.sun, static_cast<uint32_t>(plan.count), kShadowMapSize)) {
            sunMaps = plan.count;
            for (size_t m = 0; m < static_cast<size_t>(plan.count); ++m) {
                CopyMatrix(sc.sunViewProj[m], plan.viewProj[m]);
                // The biases are a texel or so: the normal offset in the world, the depth's in the map.
                sc.sunInfo[m][0] = plan.splitFar[m];
                sc.sunInfo[m][1] = plan.texel[m] * 1.5f;
                sc.sunInfo[m][2] = plan.texel[m] / std::max(plan.depthRange[m], 1e-6f);
                planMap(s.sun, static_cast<uint32_t>(m), plan.viewProj[m]);
            }
        }
    }
    sc.shadowInfo[0] = static_cast<float>(sunMaps);
    sc.shadowInfo[1] = 1.f / static_cast<float>(kShadowMapSize);
    // The point lights and spots.
    bool localMaps = false;
    if (f.shadows && anyCaster && lights.localSlices > 0 && s.EnsureMaps(s.local, static_cast<uint32_t>(kLocalShadowSlices), kLocalShadowSize)) {
        localMaps = true;
        for (size_t m = 0; m < lights.localSlices; ++m) {
            CopyMatrix(sc.localViewProj[m], lights.localViewProj[m]);
            planMap(s.local, static_cast<uint32_t>(m), lights.localViewProj[m]);
        }
    }
    sc.shadowInfo[2] = 1.f / static_cast<float>(kLocalShadowSize);
    sc.atlas[0] = static_cast<float>(std::max(s.sun.cols, 1u)), sc.atlas[1] = static_cast<float>(std::max(s.sun.rows, 1u));
    sc.atlas[2] = static_cast<float>(std::max(s.local.cols, 1u)), sc.atlas[3] = static_cast<float>(std::max(s.local.rows, 1u));
    // Lights whose maps were not drawn have no shadows.
    for (size_t i = 0; i < kMaxLights; ++i) {
        if (sunMaps == 0) lights.block.color[i][3] = 0;
        if (!localMaps) lights.block.shadow[i][0] = -1;
    }
    const bool shadows = sunMaps > 0 || localMaps;

    // 2. The picture's draws: what is not see-through (sorted: pictures, then mesh and part - the
    // depth test sorts out what is in front), the floor's shadows, then what is see-through, the
    // farthest first (by the middle of its part).
    const size_t opaqueFirst = cmds_.size();
    mergeFloor_ = opaqueFirst;
    keys_.clear();
    refs_.clear();
    pictureSets_.clear();
    for (uint32_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (!it.seen) continue;
        for (uint32_t k = 0; k < it.mesh->parts.size(); ++k) {
            const MeshPart& part = it.mesh->parts[k];
            if (isSeeThrough(it, part)) continue;
            keys_.push_back({0, PictureSet(&materialOf(it, part)), it.mesh->serial, k});
            refs_.push_back({i, k});
        }
    }
    SortDraws(keys_, order_);
    for (const uint32_t o : order_) partDraw(items_[refs_[o].first], items_[refs_[o].first].mesh->parts[refs_[o].second]);
    const size_t opaqueEnd = cmds_.size();
    mergeFloor_ = cmds_.size();
    if (shadows) {
        const uint32_t floorThing = AddThing(Mat4{}, f.shadowColor, {0, 0, 0, 0});
        Emit(s.floor.get(), 0, s.floor->indexCount, AddDraw(floorThing, white), nullptr);
    }
    const size_t floorEnd = cmds_.size();
    mergeFloor_ = cmds_.size();
    seeThrough_.clear();
    for (size_t i = 0; i < items_.size(); ++i) {
        if (!items_[i].seen) continue;
        for (const MeshPart& part : items_[i].mesh->parts) {
            if (isSeeThrough(items_[i], part)) seeThrough_.push_back({i, &part});
        }
    }
    if (!seeThrough_.empty()) {
        centers_.clear();
        for (const auto& [i, part] : seeThrough_) {
            const GpuMesh& m = *items_[i].mesh;
            centers_.push_back(TransformPoint(*items_[i].world, m.partCenters[static_cast<size_t>(part - m.parts.data())]));
        }
        // (the same mesh, part and pictures one after another in this order still become one draw)
        for (const size_t k : BackToFront(centers_, eye)) partDraw(items_[seeThrough_[k].first], *seeThrough_[k].second);
    }
    const size_t seeThroughEnd = cmds_.size();
    stats_.drawCalls = cmds_.size();

    // The frame's numbers go to the GPU at once.
    if (!UploadFrame(ctx)) return nullptr;
    FrameConstants fc{};
    CopyMatrix(fc.viewProj, viewProj);
    fc.eye[0] = eye.x, fc.eye[1] = eye.y, fc.eye[2] = eye.z;
    ctx->UpdateSubresource(s.lights.Get(), 0, nullptr, &lights.block, 0, 0);
    ctx->UpdateSubresource(s.shadows.Get(), 0, nullptr, &sc, 0, 0);
    ID3D11Buffer* frameCb = s.frame.Get();
    ID3D11Buffer* cbs[3] = {frameCb, s.lights.Get(), s.shadows.Get()};
    ctx->VSSetConstantBuffers(0, 1, &frameCb);
    ctx->PSSetConstantBuffers(0, 3, cbs);
    ID3D11ShaderResourceView* noSrvs[8] = {};
    ctx->PSSetShaderResources(0, 8, noSrvs);  // the maps may still be bound for reading: they are written first
    ID3D11ShaderResourceView* thingsView = thingsView_.Get();
    ctx->VSSetShaderResources(6, 1, &thingsView);
    ctx->PSSetShaderResources(6, 1, &thingsView);
    ctx->IASetInputLayout(s.meshLayout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    {
        const UINT stride = sizeof(uint32_t) * 2, offset = 0;
        ID3D11Buffer* db = drawsBuffer_.Get();
        ctx->IASetVertexBuffers(1, 1, &db, &stride, &offset);
    }
    const float factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(s.blend.Get(), factor, 0xFFFFFFFF);

    // Runs draws: the mesh bound when it changes, the pictures when they change; each draw is all
    // its instances at once.
    const GpuMesh* bound = nullptr;
    auto run = [&](size_t first, size_t end, bool painted) {
        const DrawMaterial* pictures = nullptr;
        bool any = false;
        for (size_t k = first; k < end; ++k) {
            const Cmd& cmd = cmds_[k];
            if (cmd.mesh != bound) {
                BindMesh(ctx, cmd.mesh->vertices.Get(), cmd.mesh->indices.Get());
                bound = cmd.mesh;
            }
            if (painted && (!any || !SamePictures(pictures, cmd.material))) {
                const DrawMaterial& mat = cmd.material ? *cmd.material : kWhite;
                auto view = [](const GpuTexture* t) -> ID3D11ShaderResourceView* { return t ? t->view.Get() : nullptr; };
                ID3D11ShaderResourceView* views[4] = {view(mat.texture.get()), view(mat.metalRough.get()), view(mat.normal.get()),
                                                      view(mat.emission.get())};
                ctx->PSSetShaderResources(1, 4, views);
                // (the colour picture decides: くっきり is the image's import setting)
                ID3D11SamplerState* sampler = mat.texture && mat.texture->sharp ? s.sharpSampler.Get() : s.textureSampler.Get();
                ctx->PSSetSamplers(1, 1, &sampler);
                pictures = cmd.material;
                any = true;
            }
            ctx->DrawIndexedInstanced(cmd.indexCount, cmd.instances, cmd.firstIndex, 0, cmd.slot);
        }
    };

    // 1. The shadow maps that changed: the kept layers first (what stays put), then each map's tile -
    // cleared (depth 1) and drawn whole, or laid back from its layer with the moving things on top.
    if (!layers.empty() || !work.empty()) {
        ctx->RSSetState(s.shadowRaster.Get());
        ctx->PSSetShader(nullptr, nullptr, 0);  // depth only
        ID3D11DepthStencilView* target = nullptr;
        auto begin = [&](ID3D11DepthStencilView* dsv, const MapWork& w) {
            if (dsv != target) {
                ctx->OMSetRenderTargets(0, nullptr, dsv);
                target = dsv;
            }
            const float size = static_cast<float>(w.maps->size);
            const D3D11_VIEWPORT svp{static_cast<float>(w.tile % w.maps->cols) * size, static_cast<float>(w.tile / w.maps->cols) * size, size, size, 0, 1};
            ctx->RSSetViewports(1, &svp);
        };
        auto clearTile = [&] {
            ctx->OMSetDepthStencilState(s.depthClear.Get(), 0);
            ctx->VSSetShader(s.clearVs.Get(), nullptr, 0);
            ctx->Draw(3, 0);
            ctx->OMSetDepthStencilState(s.depth.Get(), 0);
        };
        auto drawCasters = [&](const MapWork& w) {
            CopyMatrix(fc.lightViewProj, w.viewProj);
            ctx->UpdateSubresource(frameCb, 0, nullptr, &fc, 0, 0);
            ctx->VSSetShader(s.shadowVs.Get(), nullptr, 0);
            run(w.first, w.end, false);
        };
        for (const MapWork& w : layers) {
            begin(w.maps->layerDsv.Get(), w);
            clearTile();
            drawCasters(w);
        }
        for (const MapWork& w : work) {
            begin(w.maps->dsv.Get(), w);
            if (w.mode == MapMode::OverLayer) {
                // The layer's depths, exactly (CopyDepthPS), then the moving things over them. (The
                // whole texture cannot be copied: the tiles of other maps lie in it too.)
                ID3D11ShaderResourceView* layer = w.maps->layerSrv.Get();
                ctx->RSSetState(s.raster.Get());  // no depth bias on the copied depths
                ctx->OMSetDepthStencilState(s.depthClear.Get(), 0);
                ctx->VSSetShader(s.clearVs.Get(), nullptr, 0);
                ctx->PSSetShader(s.copyPs.Get(), nullptr, 0);
                ctx->PSSetShaderResources(7, 1, &layer);
                ctx->Draw(3, 0);
                ctx->PSSetShaderResources(7, 1, noSrvs);
                ctx->PSSetShader(nullptr, nullptr, 0);
                ctx->RSSetState(s.shadowRaster.Get());
                ctx->OMSetDepthStencilState(s.depth.Get(), 0);
            } else {
                clearTile();
            }
            drawCasters(w);
        }
    }
    // The tiles drawn now hold what was planned.
    for (const Holds& h : holds) (*h.of)[h.tile] = h.value;
    ctx->UpdateSubresource(frameCb, 0, nullptr, &fc, 0, 0);

    // 2. The picture.
    ID3D11RenderTargetView* rtv = rtv_.Get();
    ctx->OMSetRenderTargets(1, &rtv, dsv_.Get());
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(f.width), static_cast<float>(f.height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    const float clear[4] = {f.clear.r, f.clear.g, f.clear.b, f.clear.a};  // a < 1: a picture to put over something (a thumbnail)
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    ctx->RSSetState(s.raster.Get());
    ctx->OMSetDepthStencilState(s.depth.Get(), 0);
    if (shadows) {
        ID3D11ShaderResourceView* sunSrv = s.sun.srv.Get();
        ID3D11ShaderResourceView* localSrv = s.local.srv.Get();
        ID3D11SamplerState* sampler = s.shadowSampler.Get();
        ctx->PSSetShaderResources(0, 1, &sunSrv);
        ctx->PSSetShaderResources(5, 1, &localSrv);
        ctx->PSSetSamplers(0, 1, &sampler);
    }
    ctx->VSSetShader(s.meshVs.Get(), nullptr, 0);
    ctx->PSSetShader(s.meshPs.Get(), nullptr, 0);
    run(opaqueFirst, opaqueEnd, true);
    // The shadows on the floor: see-through, over what is already drawn (the depth test still hides
    // the floor behind things), and writing no depth (the grid lies on it).
    if (floorEnd > opaqueEnd) {
        ctx->OMSetDepthStencilState(s.depthNoWrite.Get(), 0);
        ctx->PSSetShader(s.floorPs.Get(), nullptr, 0);
        run(opaqueEnd, floorEnd, false);
        ctx->OMSetDepthStencilState(s.depth.Get(), 0);
    }

    // Lines (after the filled things: the grid is see-through, and the depth test still hides it behind them).
    if (!f.lines.empty() && LineCapacity(f.lines.size())) {
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(lineVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            std::memcpy(map.pData, f.lines.data(), f.lines.size() * sizeof(LineVertex));
            ctx->Unmap(lineVb_.Get(), 0);
            ctx->IASetInputLayout(s.lineLayout.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            const UINT stride = sizeof(LineVertex), offset = 0;
            ID3D11Buffer* vb = lineVb_.Get();
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            bound = nullptr;
            ctx->VSSetShader(s.lineVs.Get(), nullptr, 0);
            ctx->PSSetShader(s.linePs.Get(), nullptr, 0);
            ctx->Draw(static_cast<UINT>(f.lines.size() & ~size_t{1}), 0);
        }
    }

    // 3. What is see-through, over everything else and writing no depth (what is behind another
    // see-through part still shows through it).
    if (seeThroughEnd > floorEnd) {
        ctx->IASetInputLayout(s.meshLayout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetDepthStencilState(s.depthNoWrite.Get(), 0);
        ctx->VSSetShader(s.meshVs.Get(), nullptr, 0);
        ctx->PSSetShader(s.meshPs.Get(), nullptr, 0);
        run(floorEnd, seeThroughEnd, true);
        ctx->OMSetDepthStencilState(s.depth.Get(), 0);
    }

    if (samples_ > 1) ctx->ResolveSubresource(picture_.Get(), 0, multisample_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    // Nothing of ours stays bound (Direct2D draws the picture next).
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->PSSetShaderResources(0, 8, noSrvs);
    ctx->VSSetShaderResources(6, 1, noSrvs);
    ID3D11Buffer* noCbs[3] = {};
    ctx->PSSetConstantBuffers(0, 3, noCbs);
    ID3D11Buffer* noVb = nullptr;
    const UINT zero = 0;
    ctx->IASetVertexBuffers(1, 1, &noVb, &zero, &zero);
    return picture_.Get();
}

}  // namespace rm::space3d

