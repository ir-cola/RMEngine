#include "rm_engine/space3d/runtime/Renderer3D.h"

#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>

namespace rm::space3d {

namespace {

// Matrices are stored column by column (m[column][row]), which is HLSL's default packing, so they
// go to the GPU as they are and mul(matrix, vector) means matrix * vector.
constexpr char kShaders[] = R"(
cbuffer PerDraw : register(b0) {
    float4x4 viewProj;
    float4x4 world;
    float4x4 normalWorld;    // the inverse transpose of world: normals stay at right angles
    float4x4 lightViewProj;  // ShadowVS: world -> the shadow map being drawn (x, y -1..1, depth 0..1)
    float4 color;
    float4 eye;              // xyz: where the camera is (world)
    float4 material;         // the part's material colour, linear (times `color`, which is the screen's)
    float4 paint;            // x: 1 = a colour picture; y: alpha 0 opaque / 1 mask / 2 blend; z: mask's cutoff; w: 1 = a normal map
    float4 highlight;        // a glow at the edges (selected): rgb (the screen's), a = how strong
    float4 pbr;              // x: metallic, y: roughness, z: the normal map's strength; w: 1 = a metal / rough picture
    float4 emissive;         // rgb: the light it gives (linear); w: 1 = an emission picture
};
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

struct MeshIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct MeshOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float3 wpos : TEXCOORD2; };
MeshOut MeshVS(MeshIn i) {
    MeshOut o;
    float4 w = mul(world, float4(i.pos, 1));
    o.pos = mul(viewProj, w);
    o.nrm = mul((float3x3)normalWorld, i.nrm);
    o.wpos = w.xyz;
    o.uv = i.uv;
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

float4 ShadowVS(MeshIn i) : SV_Position { return mul(lightViewProj, mul(world, float4(i.pos, 1))); }
)";

struct Constants {
    float viewProj[16];
    float world[16];
    float normalWorld[16];
    float lightViewProj[16];
    float color[4];
    float eye[4];
    float material[4];
    float paint[4];
    float highlight[4];
    float pbr[4];
    float emissive[4];
};
static_assert(sizeof(Constants) % 16 == 0);

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

}  // namespace

bool Renderer::Init(ID3D11Device* device, std::string* error) {
    device_ = device;
    static std::atomic<uint64_t> next{0};
    id_ = ++next;
    Ptr<ID3DBlob> lvs, lps, mvs, mps, fps, svs;
    if (!Compile("LineVS", "vs_4_0", lvs, error) || !Compile("LinePS", "ps_4_0", lps, error) || !Compile("MeshVS", "vs_4_0", mvs, error) ||
        !Compile("MeshPS", "ps_4_0", mps, error) || !Compile("FloorPS", "ps_4_0", fps, error) || !Compile("ShadowVS", "vs_4_0", svs, error)) {
        return false;
    }
    auto fail = [&](const char* what) {
        if (error) *error = std::format("3D の描画の準備に失敗しました: {}", what);
        meshVs_.Reset();
        return false;
    };
    if (FAILED(device->CreateVertexShader(lvs->GetBufferPointer(), lvs->GetBufferSize(), nullptr, &lineVs_)) ||
        FAILED(device->CreatePixelShader(lps->GetBufferPointer(), lps->GetBufferSize(), nullptr, &linePs_)) ||
        FAILED(device->CreatePixelShader(mps->GetBufferPointer(), mps->GetBufferSize(), nullptr, &meshPs_)) ||
        FAILED(device->CreatePixelShader(fps->GetBufferPointer(), fps->GetBufferSize(), nullptr, &floorPs_)) ||
        FAILED(device->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(), nullptr, &shadowVs_)) ||
        FAILED(device->CreateVertexShader(mvs->GetBufferPointer(), mvs->GetBufferSize(), nullptr, &meshVs_))) {
        return fail("シェーダー");
    }
    const D3D11_INPUT_ELEMENT_DESC lineIn[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const D3D11_INPUT_ELEMENT_DESC meshIn[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device->CreateInputLayout(lineIn, 2, lvs->GetBufferPointer(), lvs->GetBufferSize(), &lineLayout_)) ||
        FAILED(device->CreateInputLayout(meshIn, 3, mvs->GetBufferPointer(), mvs->GetBufferSize(), &meshLayout_))) {
        return fail("入力レイアウト");
    }

    std::string why;
    cube_ = Upload(UnitCube(), &why);
    floor_ = Upload(FloorPlane(), &why);
    if (!cube_ || !floor_) return fail("箱");
    D3D11_BUFFER_DESC cd{};
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.ByteWidth = sizeof(Constants);
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cd, nullptr, &constants_))) return fail("定数");
    cd.ByteWidth = sizeof(LightBlock);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &lights_))) return fail("光");
    cd.ByteWidth = sizeof(ShadowConstants);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &shadows_))) return fail("影");

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
    if (FAILED(device->CreateRasterizerState(&rd, &raster_)) || FAILED(device->CreateRasterizerState(&sd, &shadowRaster_)) ||
        FAILED(device->CreateDepthStencilState(&dd, &depth_)) || FAILED(device->CreateDepthStencilState(&dn, &depthNoWrite_)) ||
        FAILED(device->CreateBlendState(&bl, &blend_)) || FAILED(device->CreateSamplerState(&ts, &textureSampler_))) {
        return fail("描画の設定");
    }
    D3D11_SAMPLER_DESC tp = ts;  // くっきり: the nearest pixel (the smaller copies still far away)
    tp.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    tp.MaxAnisotropy = 1;
    if (FAILED(device->CreateSamplerState(&tp, &sharpSampler_))) {
        return fail("描画の設定");
    }
    if (!MakeShadowSampler()) return fail("影");

    UINT quality = 0;
    samples_ = 1;
    if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0) {
        UINT dq = 0;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &dq)) && dq > 0) samples_ = 4;
    }
    return true;
}

bool Renderer::MakeShadowSampler() {
    // Compares the pixel's depth with the map's, blending the answers of the 2 x 2 texels around it.
    // Outside the map the depth is 1 (the farthest): nothing there casts a shadow.
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.f;
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(device_->CreateSamplerState(&sd, &shadowSampler_));
}

bool Renderer::EnsureMaps(ShadowMaps& maps, uint32_t tiles, uint32_t size) {
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
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &maps.texture)) ||
        FAILED(device_->CreateDepthStencilView(maps.texture.Get(), &dv, &maps.dsv)) ||
        FAILED(device_->CreateShaderResourceView(maps.texture.Get(), &sv, &maps.srv))) {
        maps = {};
        return false;
    }
    maps.tiles = tiles;
    maps.cols = cols;
    maps.rows = rows;
    maps.size = size;
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
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const platform::Image& l : pixels.levels) init.push_back({l.pixels.data(), l.width * 4, 0});
    D3D11_TEXTURE2D_DESC td{};
    td.Width = top.width;
    td.Height = top.height;
    td.MipLevels = static_cast<UINT>(pixels.levels.size());
    td.ArraySize = 1;
    td.Format = pixels.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;  // sRGB: the GPU reads linear light
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
    gpu->madeBy = id_;
    for (const platform::Image& l : pixels.levels) gpu->bytes += l.pixels.size();
    return gpu;
}

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

}  // namespace

ID3D11Texture2D* Renderer::Render(ID3D11DeviceContext* ctx, const RenderFrame& f) {
    if (!Ready() || f.width == 0 || f.height == 0 || !Resize(f.width, f.height)) return nullptr;

    // Everything filled: the boxes and the models (each with its own place and colour).
    items_.clear();
    for (const Box& b : f.boxes) items_.push_back({cube_.get(), &b.world, b.color, b.castShadow, {}, {0, 0, 0, 0}});
    for (const MeshDraw& m : f.meshes) {
        if (m.mesh && m.mesh->indexCount > 0) items_.push_back({m.mesh, &m.world, m.color, m.castShadow, m.materials, m.highlight});
    }
    // Culling (always): what lies wholly outside the camera's view is left out of the picture.
    const Mat4 viewProj = f.projection * f.view;
    stats_ = {items_.size(), 0};
    for (Item& it : items_) {
        it.bounds = WorldBounds(*it.world, it.mesh->min, it.mesh->max);
        it.seen = BoxInView(viewProj, it.bounds);
        if (it.seen) ++stats_.drawn;
    }

    ID3D11Buffer* cb = constants_.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    // The lights (none: the one default directional light) and the ambient light.
    LightSetup lights = MakeLights(f.lights, f.lightDirection, f.ambient);
    ID3D11Buffer* lb = lights_.Get();
    ID3D11Buffer* sb = shadows_.Get();
    ID3D11Buffer* both[2] = {lb, sb};
    ctx->PSSetConstantBuffers(1, 2, both);
    ctx->IASetInputLayout(meshLayout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFF);
    ID3D11ShaderResourceView* noSrvs[6] = {};
    ctx->PSSetShaderResources(0, 6, noSrvs);  // the maps may still be bound for reading: they are written first

    Constants c{};
    CopyMatrix(c.viewProj, f.projection * f.view);
    if (const auto toWorld = Inverse(f.view)) {
        const Vec3 eye = TransformPoint(*toWorld, {0, 0, 0});
        c.eye[0] = eye.x, c.eye[1] = eye.y, c.eye[2] = eye.z;
    }
    auto put = [](float out[4], Color col) { out[0] = col.r, out[1] = col.g, out[2] = col.b, out[3] = col.a; };
    // The whole shape at once (the shadow map, the floor): no materials.
    auto draw = [&](const GpuMesh& mesh, const Mat4& world, Color col) {
        CopyMatrix(c.world, world);
        CopyMatrix(c.normalWorld, NormalMatrix(world));
        put(c.color, col);
        put(c.material, {1, 1, 1, 1});
        put(c.paint, {0, 0, 0, 0});
        put(c.highlight, {0, 0, 0, 0});
        put(c.pbr, {0, 0.8f, 1, 0});
        put(c.emissive, {0, 0, 0, 0});
        ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
        BindMesh(ctx, mesh.vertices.Get(), mesh.indices.Get());
        ctx->DrawIndexed(mesh.indexCount, 0, 0);
    };
    // Part by part, each with its material (colour, metal / rough, normals, emission, alpha, pictures).
    // `seeThrough`: only the parts that are see-through (BLEND, or the whole thing faded) - or only the others.
    static const DrawMaterial kWhite;
    auto materialOf = [](const Item& it, const MeshPart& part) -> const DrawMaterial& {
        return part.material >= 0 && static_cast<size_t>(part.material) < it.materials.size() ? it.materials[static_cast<size_t>(part.material)]
                                                                                               : kWhite;
    };
    auto isSeeThrough = [&](const Item& it, const MeshPart& part) {
        return it.color.a < 1.f || materialOf(it, part).alpha == AlphaMode::Blend;
    };
    auto drawPainted = [&](const Item& it, bool seeThrough, const MeshPart* only) {
        CopyMatrix(c.world, *it.world);
        CopyMatrix(c.normalWorld, NormalMatrix(*it.world));
        put(c.color, it.color);
        put(c.highlight, it.highlight);
        BindMesh(ctx, it.mesh->vertices.Get(), it.mesh->indices.Get());
        for (const MeshPart& part : it.mesh->parts) {
            if (only ? &part != only : isSeeThrough(it, part) != seeThrough) continue;
            const DrawMaterial& mat = materialOf(it, part);
            put(c.material, mat.color);
            const float mode = mat.alpha == AlphaMode::Mask ? 1.f : mat.alpha == AlphaMode::Blend ? 2.f : 0.f;
            put(c.paint, {mat.texture ? 1.f : 0.f, mode, mat.alphaCutoff, mat.normal ? 1.f : 0.f});
            put(c.pbr, {mat.metallic, mat.roughness, mat.normalScale, mat.metalRough ? 1.f : 0.f});
            put(c.emissive, {mat.emissive.r, mat.emissive.g, mat.emissive.b, mat.emission ? 1.f : 0.f});
            auto view = [](const GpuTexture* t) -> ID3D11ShaderResourceView* { return t ? t->view.Get() : nullptr; };
            ID3D11ShaderResourceView* views[4] = {view(mat.texture.get()), view(mat.metalRough.get()), view(mat.normal.get()),
                                                  view(mat.emission.get())};
            ctx->PSSetShaderResources(1, 4, views);
            // (the colour picture decides: くっきり is the image's import setting)
            ID3D11SamplerState* sampler = mat.texture && mat.texture->sharp ? sharpSampler_.Get() : textureSampler_.Get();
            ctx->PSSetSamplers(1, 1, &sampler);
            ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
            ctx->DrawIndexed(part.indexCount, part.firstIndex, 0);
        }
    };

    // 1. The shadow maps: what casts, seen from the lights (an item outside a map is not drawn into it).
    ShadowConstants sc{};
    if (const auto toWorld = Inverse(f.view)) {
        const Vec3 forward = Normalize(TransformDirection(*toWorld, {0, 0, -1}));
        sc.viewForward[0] = forward.x, sc.viewForward[1] = forward.y, sc.viewForward[2] = forward.z;
    }
    Bounds casters;
    bool anyCaster = false;
    for (const Item& it : items_) {
        if (!it.castShadow) continue;
        const Bounds& b = it.bounds;
        casters.min = anyCaster ? Vec3{std::min(casters.min.x, b.min.x), std::min(casters.min.y, b.min.y), std::min(casters.min.z, b.min.z)} : b.min;
        casters.max = anyCaster ? Vec3{std::max(casters.max.x, b.max.x), std::max(casters.max.y, b.max.y), std::max(casters.max.z, b.max.z)} : b.max;
        anyCaster = true;
    }
    // Each kind of map: cleared once, then each map drawn into its tile.
    auto clearMaps = [&](const ShadowMaps& maps) {
        ctx->OMSetRenderTargets(0, nullptr, maps.dsv.Get());
        ctx->ClearDepthStencilView(maps.dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
    };
    auto drawMap = [&](const ShadowMaps& maps, uint32_t tile, const Mat4& lightViewProj) {
        const float size = static_cast<float>(maps.size);
        const D3D11_VIEWPORT svp{static_cast<float>(tile % maps.cols) * size, static_cast<float>(tile / maps.cols) * size, size, size, 0, 1};
        ctx->RSSetViewports(1, &svp);
        ctx->RSSetState(shadowRaster_.Get());
        ctx->OMSetDepthStencilState(depth_.Get(), 0);
        ctx->VSSetShader(shadowVs_.Get(), nullptr, 0);
        ctx->PSSetShader(nullptr, nullptr, 0);  // depth only
        CopyMatrix(c.lightViewProj, lightViewProj);
        for (const Item& it : items_) {
            if (it.castShadow && BoxInView(lightViewProj, it.bounds)) draw(*it.mesh, *it.world, it.color);
        }
    };
    // The sun: one map fitted to what casts, or the cascades along the view.
    int sunMaps = 0;
    if (f.shadows && anyCaster && lights.shadowLight >= 0) {
        const SunShadowPlan plan = PlanSunShadows(lights.shadowDirection, casters, f.view, f.projection, kShadowMapSize);
        if (plan.count > 0 && EnsureMaps(sun_, static_cast<uint32_t>(plan.count), kShadowMapSize)) {
            sunMaps = plan.count;
            clearMaps(sun_);
            for (size_t m = 0; m < static_cast<size_t>(plan.count); ++m) {
                CopyMatrix(sc.sunViewProj[m], plan.viewProj[m]);
                // The biases are a texel or so: the normal offset in the world, the depth's in the map.
                sc.sunInfo[m][0] = plan.splitFar[m];
                sc.sunInfo[m][1] = plan.texel[m] * 1.5f;
                sc.sunInfo[m][2] = plan.texel[m] / std::max(plan.depthRange[m], 1e-6f);
                drawMap(sun_, static_cast<uint32_t>(m), plan.viewProj[m]);
            }
        }
    }
    sc.shadowInfo[0] = static_cast<float>(sunMaps);
    sc.shadowInfo[1] = 1.f / static_cast<float>(kShadowMapSize);
    // The point lights and spots.
    bool localMaps = false;
    if (f.shadows && anyCaster && lights.localSlices > 0 && EnsureMaps(local_, static_cast<uint32_t>(kLocalShadowSlices), kLocalShadowSize)) {
        localMaps = true;
        clearMaps(local_);
        for (size_t m = 0; m < lights.localSlices; ++m) {
            CopyMatrix(sc.localViewProj[m], lights.localViewProj[m]);
            drawMap(local_, static_cast<uint32_t>(m), lights.localViewProj[m]);
        }
    }
    sc.shadowInfo[2] = 1.f / static_cast<float>(kLocalShadowSize);
    sc.atlas[0] = static_cast<float>(std::max(sun_.cols, 1u)), sc.atlas[1] = static_cast<float>(std::max(sun_.rows, 1u));
    sc.atlas[2] = static_cast<float>(std::max(local_.cols, 1u)), sc.atlas[3] = static_cast<float>(std::max(local_.rows, 1u));
    // Lights whose maps were not drawn have no shadows.
    for (size_t i = 0; i < kMaxLights; ++i) {
        if (sunMaps == 0) lights.block.color[i][3] = 0;
        if (!localMaps) lights.block.shadow[i][0] = -1;
    }
    const bool shadows = sunMaps > 0 || localMaps;
    ctx->UpdateSubresource(lb, 0, nullptr, &lights.block, 0, 0);
    ctx->UpdateSubresource(sb, 0, nullptr, &sc, 0, 0);

    // 2. The picture.
    ID3D11RenderTargetView* rtv = rtv_.Get();
    ctx->OMSetRenderTargets(1, &rtv, dsv_.Get());
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(f.width), static_cast<float>(f.height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    const float clear[4] = {f.clear.r, f.clear.g, f.clear.b, f.clear.a};  // a < 1: a picture to put over something (a thumbnail)
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    ctx->RSSetState(raster_.Get());
    ctx->OMSetDepthStencilState(depth_.Get(), 0);
    if (shadows) {
        ID3D11ShaderResourceView* sunSrv = sun_.srv.Get();
        ID3D11ShaderResourceView* localSrv = local_.srv.Get();
        ID3D11SamplerState* sampler = shadowSampler_.Get();
        ctx->PSSetShaderResources(0, 1, &sunSrv);
        ctx->PSSetShaderResources(5, 1, &localSrv);
        ctx->PSSetSamplers(0, 1, &sampler);
    }

    if (!items_.empty()) {
        ctx->VSSetShader(meshVs_.Get(), nullptr, 0);
        ctx->PSSetShader(meshPs_.Get(), nullptr, 0);
        ID3D11SamplerState* sampler = textureSampler_.Get();
        ctx->PSSetSamplers(1, 1, &sampler);
        // What is not see-through first (any order: the depth test sorts it out).
        for (const Item& it : items_) {
            if (it.seen) drawPainted(it, false, nullptr);
        }
    }
    // The shadows on the floor: see-through, over what is already drawn (the depth test still hides
    // the floor behind things), and writing no depth (the grid lies on it).
    if (shadows) {
        ctx->OMSetDepthStencilState(depthNoWrite_.Get(), 0);
        ctx->VSSetShader(meshVs_.Get(), nullptr, 0);
        ctx->PSSetShader(floorPs_.Get(), nullptr, 0);
        draw(*floor_, Mat4{}, f.shadowColor);
        ctx->OMSetDepthStencilState(depth_.Get(), 0);
    }

    // Lines (after the filled things: the grid is see-through, and the depth test still hides it behind them).
    if (!f.lines.empty() && LineCapacity(f.lines.size())) {
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(lineVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            std::memcpy(map.pData, f.lines.data(), f.lines.size() * sizeof(LineVertex));
            ctx->Unmap(lineVb_.Get(), 0);
            CopyMatrix(c.world, Mat4{});
            ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
            ctx->IASetInputLayout(lineLayout_.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            const UINT stride = sizeof(LineVertex), offset = 0;
            ID3D11Buffer* vb = lineVb_.Get();
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->VSSetShader(lineVs_.Get(), nullptr, 0);
            ctx->PSSetShader(linePs_.Get(), nullptr, 0);
            ctx->Draw(static_cast<UINT>(f.lines.size() & ~size_t{1}), 0);
        }
    }

    // 3. What is see-through, the farthest first (by the middle of its part), over everything else and
    // writing no depth (what is behind another see-through part still shows through it).
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
        Vec3 eye{0, 0, 0};
        if (const auto toWorld = Inverse(f.view)) eye = TransformPoint(*toWorld, {0, 0, 0});
        ctx->IASetInputLayout(meshLayout_.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetDepthStencilState(depthNoWrite_.Get(), 0);
        ctx->VSSetShader(meshVs_.Get(), nullptr, 0);
        ctx->PSSetShader(meshPs_.Get(), nullptr, 0);
        for (const size_t k : BackToFront(centers_, eye)) drawPainted(items_[seeThrough_[k].first], true, seeThrough_[k].second);
        ctx->OMSetDepthStencilState(depth_.Get(), 0);
    }
    ID3D11ShaderResourceView* none[4] = {};
    ctx->PSSetShaderResources(1, 4, none);

    if (samples_ > 1) ctx->ResolveSubresource(picture_.Get(), 0, multisample_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    // Nothing of ours stays bound (Direct2D draws the picture next).
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->PSSetShaderResources(0, 6, noSrvs);
    ID3D11Buffer* noCbs[2] = {nullptr, nullptr};
    ctx->PSSetConstantBuffers(1, 2, noCbs);
    return picture_.Get();
}

}  // namespace rm::space3d
