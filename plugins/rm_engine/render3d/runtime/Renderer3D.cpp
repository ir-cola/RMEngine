#include "rm_engine/render3d/runtime/Renderer3D.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace rm::render3d {

namespace {

// Matrices are stored column by column (m[column][row]), which is HLSL's default packing, so they
// go to the GPU as they are and mul(matrix, vector) means matrix * vector.
constexpr char kShaders[] = R"(
cbuffer PerDraw : register(b0) {
    float4x4 viewProj;
    float4x4 world;
    float4 color;
    float4 light;
};

struct LineIn  { float3 pos : POSITION; float4 col : COLOR; };
struct LineOut { float4 pos : SV_Position; float4 col : COLOR; };
LineOut LineVS(LineIn i) {
    LineOut o;
    o.pos = mul(viewProj, float4(i.pos, 1));
    o.col = i.col;
    return o;
}
float4 LinePS(LineOut i) : SV_Target { return i.col; }

struct BoxIn  { float3 pos : POSITION; float3 nrm : NORMAL; };
struct BoxOut { float4 pos : SV_Position; float3 nrm : NORMAL; };
BoxOut BoxVS(BoxIn i) {
    BoxOut o;
    o.pos = mul(viewProj, mul(world, float4(i.pos, 1)));
    o.nrm = mul((float3x3)world, i.nrm);
    return o;
}
float4 BoxPS(BoxOut i) : SV_Target {
    float3 n = normalize(i.nrm);
    float lit = saturate(dot(n, -normalize(light.xyz)));
    float sky = 0.5 + 0.5 * n.y;  // faces turned up are a little brighter even in shadow
    float k = 0.30 + 0.12 * sky + 0.58 * lit;
    return float4(color.rgb * k, color.a);
}
)";

struct Constants {
    float viewProj[16];
    float world[16];
    float color[4];
    float light[4];
};
static_assert(sizeof(Constants) % 16 == 0);

struct BoxVertex {
    float px, py, pz, nx, ny, nz;
};

void CopyMatrix(float out[16], const space3d::Mat4& m) { std::memcpy(out, &m.m[0][0], sizeof(float) * 16); }

bool Compile(const char* entry, const char* target, Microsoft::WRL::ComPtr<ID3DBlob>& out, std::string* error) {
    Microsoft::WRL::ComPtr<ID3DBlob> messages;
    const HRESULT hr = D3DCompile(kShaders, sizeof(kShaders) - 1, "render3d", nullptr, nullptr, entry, target,
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

}  // namespace

bool Renderer::Init(ID3D11Device* device, std::string* error) {
    device_ = device;
    Ptr<ID3DBlob> lvs, lps, bvs, bps;
    if (!Compile("LineVS", "vs_4_0", lvs, error) || !Compile("LinePS", "ps_4_0", lps, error) || !Compile("BoxVS", "vs_4_0", bvs, error) ||
        !Compile("BoxPS", "ps_4_0", bps, error)) {
        return false;
    }
    auto fail = [&](const char* what) {
        if (error) *error = std::format("3D 描画の準備に失敗しました: {}", what);
        boxVs_.Reset();
        return false;
    };
    if (FAILED(device->CreateVertexShader(lvs->GetBufferPointer(), lvs->GetBufferSize(), nullptr, &lineVs_)) ||
        FAILED(device->CreatePixelShader(lps->GetBufferPointer(), lps->GetBufferSize(), nullptr, &linePs_)) ||
        FAILED(device->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), nullptr, &boxVs_)) ||
        FAILED(device->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), nullptr, &boxPs_))) {
        return fail("シェーダー");
    }
    const D3D11_INPUT_ELEMENT_DESC lineIn[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const D3D11_INPUT_ELEMENT_DESC boxIn[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device->CreateInputLayout(lineIn, 2, lvs->GetBufferPointer(), lvs->GetBufferSize(), &lineLayout_)) ||
        FAILED(device->CreateInputLayout(boxIn, 2, bvs->GetBufferPointer(), bvs->GetBufferSize(), &boxLayout_))) {
        return fail("入力レイアウト");
    }

    // The unit cube: 6 faces x 4 corners (each face its own normal), 2 triangles per face.
    BoxVertex v[24];
    uint16_t idx[36];
    const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        // Two axes across the face.
        const int a = n[f][0] != 0 ? 1 : 0;
        const int b = n[f][2] != 0 ? 1 : 2;
        for (int k = 0; k < 4; ++k) {
            float p[3] = {n[f][0] * 0.5f, n[f][1] * 0.5f, n[f][2] * 0.5f};
            p[a] = (k == 1 || k == 2) ? 0.5f : -0.5f;
            p[b] = (k >= 2) ? 0.5f : -0.5f;
            v[f * 4 + k] = {p[0], p[1], p[2], n[f][0], n[f][1], n[f][2]};
        }
        const uint16_t base = static_cast<uint16_t>(f * 4);
        const uint16_t tri[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; ++k) idx[f * 6 + k] = static_cast<uint16_t>(base + tri[k]);
    }
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = sizeof(v);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{v, 0, 0};
    if (FAILED(device->CreateBuffer(&bd, &init, &cubeVb_))) return fail("箱");
    bd.ByteWidth = sizeof(idx);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = idx;
    if (FAILED(device->CreateBuffer(&bd, &init, &cubeIb_))) return fail("箱");
    D3D11_BUFFER_DESC cd{};
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.ByteWidth = sizeof(Constants);
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cd, nullptr, &constants_))) return fail("定数");

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // boxes are closed, the depth test hides the back
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateRasterizerState(&rd, &raster_)) || FAILED(device->CreateDepthStencilState(&dd, &depth_)) ||
        FAILED(device->CreateBlendState(&bl, &blend_))) {
        return fail("描画の設定");
    }

    UINT quality = 0;
    samples_ = 1;
    if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0) {
        UINT dq = 0;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &dq)) && dq > 0) samples_ = 4;
    }
    return true;
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

ID3D11Texture2D* Renderer::Render(ID3D11DeviceContext* ctx, const Frame& f) {
    if (!Ready() || f.width == 0 || f.height == 0 || !Resize(f.width, f.height)) return nullptr;

    ID3D11RenderTargetView* rtv = rtv_.Get();
    ctx->OMSetRenderTargets(1, &rtv, dsv_.Get());
    const D3D11_VIEWPORT vp{0, 0, static_cast<float>(f.width), static_cast<float>(f.height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    const float clear[4] = {f.clear.r, f.clear.g, f.clear.b, 1};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    ctx->RSSetState(raster_.Get());
    ctx->OMSetDepthStencilState(depth_.Get(), 0);
    const float factor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFF);
    ID3D11Buffer* cb = constants_.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);

    Constants c{};
    CopyMatrix(c.viewProj, f.projection * f.view);
    const Vec3 l = f.lightDirection;
    c.light[0] = l.x, c.light[1] = l.y, c.light[2] = l.z;

    // Boxes.
    if (!f.boxes.empty()) {
        ctx->IASetInputLayout(boxLayout_.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const UINT stride = sizeof(BoxVertex), offset = 0;
        ID3D11Buffer* vb = cubeVb_.Get();
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetIndexBuffer(cubeIb_.Get(), DXGI_FORMAT_R16_UINT, 0);
        ctx->VSSetShader(boxVs_.Get(), nullptr, 0);
        ctx->PSSetShader(boxPs_.Get(), nullptr, 0);
        for (const Box& b : f.boxes) {
            CopyMatrix(c.world, b.world);
            c.color[0] = b.color.r, c.color[1] = b.color.g, c.color[2] = b.color.b, c.color[3] = b.color.a;
            ctx->UpdateSubresource(cb, 0, nullptr, &c, 0, 0);
            ctx->DrawIndexed(36, 0, 0);
        }
    }

    // Lines (after the boxes: the grid is see-through, and the depth test still hides it behind boxes).
    if (!f.lines.empty() && LineCapacity(f.lines.size())) {
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(lineVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            std::memcpy(map.pData, f.lines.data(), f.lines.size() * sizeof(LineVertex));
            ctx->Unmap(lineVb_.Get(), 0);
            CopyMatrix(c.world, space3d::Mat4{});
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
    return picture_.Get();
}

}  // namespace rm::render3d
