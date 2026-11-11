#include "rm_engine/space3d/runtime/Renderer3D.h"

#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3dcompiler.h>

#include <algorithm>
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
    float4x4 lightViewProj;  // world -> the shadow map (x, y -1..1, depth 0..1)
    float4 color;
    float4 light;            // xyz: where the light goes; w: 1 = shadows
    float4 shadow;           // x: one texel of the shadow map (uv); y: depth bias; z: normal offset (world)
    float4 eye;              // xyz: where the camera is (world)
};
// The lights of the frame (Lighting.h's LightBlock: set once per frame).
cbuffer Lights : register(b1) {
    float4 lightInfo;         // x: how many
    float4 lightPos[8];       // xyz: where (point, spot); w: 0 directional, 1 point, 2 spot
    float4 lightDir[8];       // xyz: where the light goes; w: the range
    float4 lightColor[8];     // rgb: colour x strength; w: 1 = its shadows are in the shadow map
    float4 lightSpot[8];      // x: cos of the cone's edge, y: cos where it starts to fade
};
Texture2D shadowMap : register(t0);
SamplerComparisonState shadowSampler : register(s0);

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
struct MeshOut { float4 pos : SV_Position; float3 nrm : NORMAL; float4 lpos : TEXCOORD1; float3 wpos : TEXCOORD2; };
MeshOut MeshVS(MeshIn i) {
    MeshOut o;
    float4 w = mul(world, float4(i.pos, 1));
    o.pos = mul(viewProj, w);
    o.nrm = mul((float3x3)normalWorld, i.nrm);
    // Looked up a little off the surface (normal offset): a surface does not shadow itself.
    float3 n = o.nrm * rsqrt(max(dot(o.nrm, o.nrm), 1e-12));
    o.lpos = mul(lightViewProj, float4(w.xyz + n * shadow.z, 1));
    o.wpos = w.xyz;
    return o;
}

// 1 = in the light, 0 = in shadow. 3 x 3 looks, each already a blend of 2 x 2 texels (PCF).
float Lit(float4 lpos) {
    if (light.w < 0.5) return 1;
    float2 uv = lpos.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return 1;  // outside: nothing casts there
    float d = min(lpos.z, 1) - shadow.y;                            // beyond the map's depth: behind everything
    float sum = 0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            sum += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) * shadow.x, d);
        }
    }
    return sum / 9;
}

// How much light reaches a point facing n: the sky (every face, faces turned up a little more - even
// in shadow) and each light (its angle, its fade with distance / out of the cone, its shadow).
float3 LightAmount(float3 n, float3 wpos, float shadowLit) {
    float sky = 0.5 + 0.5 * n.y;
    float3 k = 0.30 + 0.12 * sky;
    int count = (int)lightInfo.x;
    [loop] for (int l = 0; l < count; ++l) {
        float3 toLight = -lightDir[l].xyz;
        float fade = 1;
        if (lightPos[l].w > 0.5) {
            float3 d = lightPos[l].xyz - wpos;
            float dist = length(d);
            toLight = d / max(dist, 1e-4);
            float x = saturate(1 - dist / lightDir[l].w);
            fade = x * x;
            if (lightPos[l].w > 1.5) fade *= smoothstep(lightSpot[l].x, lightSpot[l].y, dot(-toLight, lightDir[l].xyz));
        }
        float s = lightColor[l].w > 0.5 ? shadowLit : 1;
        k += 0.58 * lightColor[l].rgb * (saturate(dot(n, toLight)) * fade * s);
    }
    return k;
}

float4 MeshPS(MeshOut i) : SV_Target {
    // The side seen is the side facing the eye (the back of an open model is lit as its own side).
    // Told from the eye, not from the triangle's winding on the screen, so a mirrored model or a
    // flipped projection never lights the wrong side.
    float3 n = normalize(i.nrm);
    if (dot(n, eye.xyz - i.wpos) < 0) n = -n;
    float3 k = LightAmount(n, i.wpos, Lit(i.lpos));
    return float4(color.rgb * k, color.a);
}

// The floor: nothing of its own, only the shadow (seen from above).
float4 FloorPS(MeshOut i) : SV_Target {
    if (eye.y < 0) discard;  // seen from below the floor
    return float4(color.rgb, color.a * (1 - Lit(i.lpos)));
}

float4 ShadowVS(MeshIn i) : SV_Position { return mul(lightViewProj, mul(world, float4(i.pos, 1))); }
)";

struct Constants {
    float viewProj[16];
    float world[16];
    float normalWorld[16];
    float lightViewProj[16];
    float color[4];
    float light[4];
    float shadow[4];
    float eye[4];
};
static_assert(sizeof(Constants) % 16 == 0);
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
    if (FAILED(device->CreateRasterizerState(&rd, &raster_)) || FAILED(device->CreateRasterizerState(&sd, &shadowRaster_)) ||
        FAILED(device->CreateDepthStencilState(&dd, &depth_)) || FAILED(device->CreateDepthStencilState(&dn, &depthNoWrite_)) ||
        FAILED(device->CreateBlendState(&bl, &blend_))) {
        return fail("描画の設定");
    }
    if (!MakeShadowMap()) return fail("影");

    UINT quality = 0;
    samples_ = 1;
    if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0) {
        UINT dq = 0;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &dq)) && dq > 0) samples_ = 4;
    }
    return true;
}

bool Renderer::MakeShadowMap() {
    // One texture seen two ways: written as depth (D32), read as numbers (R32) by the comparing sampler.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kShadowMapSize;
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
    // Compares the pixel's depth with the map's, blending the answers of the 2 x 2 texels around it.
    // Outside the map the depth is 1 (the farthest): nothing there casts a shadow.
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.f;
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(device_->CreateTexture2D(&td, nullptr, &shadowTex_)) &&
           SUCCEEDED(device_->CreateDepthStencilView(shadowTex_.Get(), &dv, &shadowDsv_)) &&
           SUCCEEDED(device_->CreateShaderResourceView(shadowTex_.Get(), &sv, &shadowSrv_)) &&
           SUCCEEDED(device_->CreateSamplerState(&sd, &shadowSampler_));
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
    for (const Box& b : f.boxes) items_.push_back({cube_.get(), &b.world, b.color, b.castShadow});
    for (const MeshDraw& m : f.meshes) {
        if (m.mesh && m.mesh->indexCount > 0) items_.push_back({m.mesh, &m.world, m.color, m.castShadow});
    }

    ID3D11Buffer* cb = constants_.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    // The lights (none: the one default directional light); the shadow map is the first directional
    // light's that casts shadows.
    const LightSetup lights = MakeLights(f.lights, f.lightDirection);
    ctx->UpdateSubresource(lights_.Get(), 0, nullptr, &lights.block, 0, 0);
    ID3D11Buffer* lb = lights_.Get();
    ctx->PSSetConstantBuffers(1, 1, &lb);
    ctx->IASetInputLayout(meshLayout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFF);
    ID3D11ShaderResourceView* noSrv = nullptr;
    ctx->PSSetShaderResources(0, 1, &noSrv);  // the map may still be bound for reading: it is written first

    Constants c{};
    CopyMatrix(c.viewProj, f.projection * f.view);
    if (const auto toWorld = Inverse(f.view)) {
        const Vec3 eye = TransformPoint(*toWorld, {0, 0, 0});
        c.eye[0] = eye.x, c.eye[1] = eye.y, c.eye[2] = eye.z;
    }
    const Vec3 l = lights.shadowDirection;
    c.light[0] = l.x, c.light[1] = l.y, c.light[2] = l.z;
    auto draw = [&](const GpuMesh& mesh, const Mat4& world, Color col) {
        CopyMatrix(c.world, world);
        CopyMatrix(c.normalWorld, NormalMatrix(world));
        c.color[0] = col.r, c.color[1] = col.g, c.color[2] = col.b, c.color[3] = col.a;
        ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
        BindMesh(ctx, mesh.vertices.Get(), mesh.indices.Get());
        ctx->DrawIndexed(mesh.indexCount, 0, 0);
    };

    // 1. The shadow map: what casts, seen from the light, fitted to the box that holds all of it.
    bool shadows = false;
    if (f.shadows && lights.shadowLight >= 0 && shadowDsv_) {
        Bounds casters;
        for (const Item& it : items_) {
            if (!it.castShadow) continue;
            const Bounds b = WorldBounds(*it.world, it.mesh->min, it.mesh->max);
            casters.min = shadows ? Vec3{std::min(casters.min.x, b.min.x), std::min(casters.min.y, b.min.y), std::min(casters.min.z, b.min.z)} : b.min;
            casters.max = shadows ? Vec3{std::max(casters.max.x, b.max.x), std::max(casters.max.y, b.max.y), std::max(casters.max.z, b.max.z)} : b.max;
            shadows = true;
        }
        if (shadows) {
            const Mat4 lightViewProj = DirectionalShadowMatrix(lights.shadowDirection, casters);
            CopyMatrix(c.lightViewProj, lightViewProj);
            // How big one texel is in the world, and how deep the map is (the light's view only turns
            // and moves, so each row of the matrix is the orthographic scale of that axis): the
            // biases are a texel or so.
            auto rowLength = [&](int r) {
                return Length({lightViewProj.m[0][r], lightViewProj.m[1][r], lightViewProj.m[2][r]});
            };
            const float width = 2.f / std::max(rowLength(0), 1e-12f), height = 2.f / std::max(rowLength(1), 1e-12f);
            const float depthRange = 1.f / std::max(rowLength(2), 1e-12f);
            const float texel = std::max(width, height) / static_cast<float>(kShadowMapSize);
            c.shadow[0] = 1.f / static_cast<float>(kShadowMapSize);
            c.shadow[1] = texel / depthRange;
            c.shadow[2] = texel * 1.5f;
            ctx->OMSetRenderTargets(0, nullptr, shadowDsv_.Get());
            const D3D11_VIEWPORT svp{0, 0, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0, 1};
            ctx->RSSetViewports(1, &svp);
            ctx->ClearDepthStencilView(shadowDsv_.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            ctx->RSSetState(shadowRaster_.Get());
            ctx->OMSetDepthStencilState(depth_.Get(), 0);
            ctx->VSSetShader(shadowVs_.Get(), nullptr, 0);
            ctx->PSSetShader(nullptr, nullptr, 0);  // depth only
            for (const Item& it : items_) {
                if (it.castShadow) draw(*it.mesh, *it.world, it.color);
            }
        }
    }

    // 2. The picture.
    ID3D11RenderTargetView* rtv = rtv_.Get();
    ctx->OMSetRenderTargets(1, &rtv, dsv_.Get());
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(f.width), static_cast<float>(f.height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    const float clear[4] = {f.clear.r, f.clear.g, f.clear.b, 1};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    ctx->RSSetState(raster_.Get());
    ctx->OMSetDepthStencilState(depth_.Get(), 0);
    c.light[3] = shadows ? 1.f : 0.f;
    if (shadows) {
        ID3D11ShaderResourceView* srv = shadowSrv_.Get();
        ID3D11SamplerState* sampler = shadowSampler_.Get();
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, &sampler);
    }

    if (!items_.empty()) {
        ctx->VSSetShader(meshVs_.Get(), nullptr, 0);
        ctx->PSSetShader(meshPs_.Get(), nullptr, 0);
        for (const Item& it : items_) draw(*it.mesh, *it.world, it.color);
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

    if (samples_ > 1) ctx->ResolveSubresource(picture_.Get(), 0, multisample_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    // Nothing of ours stays bound (Direct2D draws the picture next).
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->PSSetShaderResources(0, 1, &noSrv);
    ID3D11Buffer* noCb = nullptr;
    ctx->PSSetConstantBuffers(1, 1, &noCb);
    return picture_.Get();
}

}  // namespace rm::space3d
