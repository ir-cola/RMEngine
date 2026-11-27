#include "rm_engine/navigation_x_3d/runtime/NavFill.h"

#include "base/Log.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/navigation/runtime/NavFile.h"
#include "rm_engine/navigation/runtime/Query.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace rm::navigation_x_3d {

namespace {

template <class T>
using Ptr = Microsoft::WRL::ComPtr<T>;

constexpr float kLift = 0.04f;    // 床と重ならないように少し上
constexpr float kAlpha = 0.32f;   // 塗りの濃さ
constexpr double kRecheck = 1.0;  // ファイルを見直す間 (秒)

const char kFillHlsl[] = R"(
cbuffer NavFill : register(b4) {
    float4x4 fillViewProj;
};
struct FillIn {
    float3 pos : POSITION;
    float4 color : COLOR0;
};
struct FillOut {
    float4 pos : SV_Position;
    float4 color : COLOR0;
};
FillOut FillVS(FillIn i) {
    FillOut o;
    o.pos = mul(fillViewProj, float4(i.pos, 1));
    o.color = i.color;
    return o;
}
float4 FillPS(FillOut i) : SV_Target {
    return i.color;
}
)";

struct FillVertex {
    float pos[3];
    float color[4];
};
static_assert(sizeof(FillVertex) == 28, "FillIn と合わせる");

struct FillConstants {
    float viewProj[16];
};
static_assert(sizeof(FillConstants) % 16 == 0);

bool Compile(const char* entry, const char* target, Ptr<ID3DBlob>& out, std::string& error) {
    Ptr<ID3DBlob> messages;
    const HRESULT hr = D3DCompile(kFillHlsl, sizeof(kFillHlsl) - 1, "navfill", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &messages);
    if (SUCCEEDED(hr)) return true;
    error = std::string(entry) + (messages ? std::string(": ") + static_cast<const char*>(messages->GetBufferPointer()) : std::string());
    return false;
}

class NavFill final : public game_x_3d::WorldDrawer {
public:
    void Prepare(const game_x_3d::WorldDrawFrame& f, std::vector<space3d::ExtraDraw>& out) override {
        if (!f.picture || std::strcmp(f.picture, "scene") != 0 || !game_x_navigation::FillInSceneView() || !f.asset) return;
        const game_x_navigation::NavSettings* settings = f.scene.Setting<game_x_navigation::NavSettings>();
        if (!settings) return;
        const game_x_navigation::Worlds worlds = game_x_navigation::WorldsOf(f.scene);
        vertices_.clear();
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        for (const game_x_navigation::NavVolume& v : game_x_navigation::Volumes(f.scene, worlds)) {
            if (!v.active || v.volume.baked.Empty()) continue;
            const Mesh* m = MeshOf(v.volume.baked, f, now);
            if (!m) continue;
            for (const navigation::NavPolygon& p : m->polygons) AddPolygon(p, *settings);
        }
        if (vertices_.size() < 3 || !Ready(f.gfx)) return;
        out.push_back([this](const space3d::ExtraDrawContext& dc) { Draw(dc); });
    }

private:
    struct Mesh {
        std::string bytes;
        std::vector<navigation::NavPolygon> polygons;
        double lookedAt = -1e9;
        bool told = false;
    };

    // 焼いた物を読む (1 秒ごとに見直す: 焼き直したら塗りも変わる)。読めない: null (1 度だけ知らせる)。
    const Mesh* MeshOf(const AssetRef& ref, const game_x_3d::WorldDrawFrame& f, double now) {
        Mesh& m = meshes_[ref.guid];
        if (now - m.lookedAt >= kRecheck) {
            m.lookedAt = now;
            std::optional<std::string> bytes = f.asset(ref);
            if (!bytes) {
                m.bytes.clear();
                m.polygons.clear();
                return nullptr;
            }
            if (*bytes != m.bytes) {
                m.bytes = std::move(*bytes);
                m.polygons.clear();
                std::string why;
                navigation::NavFileHead head;
                const auto data = navigation::ReadNavFile(m.bytes, &head, &why);
                navigation::NavMesh mesh;
                if (data && mesh.Load(*data, &why)) {
                    mesh.PolygonsOf(m.polygons);
                    m.told = false;
                } else if (!m.told) {
                    m.told = true;
                    log::Warn("ナビ: 面を塗る焼いた物を読めません: {}", why);
                }
            }
        }
        return m.polygons.empty() ? nullptr : &m;
    }

    void AddPolygon(const navigation::NavPolygon& p, const game_x_navigation::NavSettings& settings) {
        if (p.points.size() < 3) return;
        const game_x_navigation::NavAreaKind* kind = game_x_navigation::FindAreaKind(settings, game_x_navigation::AreaIdOfCode(p.area));
        const Color c = kind ? kind->color : game_x_navigation::AreaKindColor(0);
        auto put = [&](Vec3 q) { vertices_.push_back({{q.x, q.y + kLift, q.z}, {c.r, c.g, c.b, kAlpha}}); };
        for (size_t i = 1; i + 1 < p.points.size(); ++i) {
            put(p.points[0]);
            put(p.points[i]);
            put(p.points[i + 1]);
        }
    }

    // シェーダーと状態 (新しいデバイスのたびに作り直す)。
    bool Ready(platform::Gfx& gfx) {
        if (generation_ != gfx.Generation()) {
            generation_ = gfx.Generation();
            vs_.Reset(), ps_.Reset(), layout_.Reset(), cb_.Reset(), vb_.Reset(), blend_.Reset(), raster_.Reset();
            capacity_ = 0;
            failed_ = false;
            ID3D11Device* device = gfx.D3D();
            if (!device) return false;
            Ptr<ID3DBlob> vs, ps;
            std::string error;
            D3D11_BUFFER_DESC cd{};
            cd.Usage = D3D11_USAGE_DEFAULT;
            cd.ByteWidth = sizeof(FillConstants);
            cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            // 半透明 (後ろが透ける)。絵のアルファはそのまま。
            D3D11_BLEND_DESC bl{};
            bl.RenderTarget[0].BlendEnable = TRUE;
            bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
            bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            rd.MultisampleEnable = TRUE;
            const D3D11_INPUT_ELEMENT_DESC layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            if (!Compile("FillVS", "vs_4_0", vs, error) || !Compile("FillPS", "ps_4_0", ps, error) ||
                FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_)) ||
                FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_)) ||
                FAILED(device->CreateInputLayout(layout, 2, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_)) ||
                FAILED(device->CreateBuffer(&cd, nullptr, &cb_)) || FAILED(device->CreateBlendState(&bl, &blend_)) ||
                FAILED(device->CreateRasterizerState(&rd, &raster_))) {
                log::Error("ナビの面を塗る準備ができません: {}", error);
                vs_.Reset();
                failed_ = true;
            }
            device_ = device;
        }
        if (failed_ || !vs_) return false;
        // 頂点の入れ物 (足りなければ大きく作り直す)。
        if (vertices_.size() > capacity_) {
            size_t want = 4096;
            while (want < vertices_.size()) want *= 2;
            D3D11_BUFFER_DESC vd{};
            vd.Usage = D3D11_USAGE_DYNAMIC;
            vd.ByteWidth = static_cast<UINT>(want * sizeof(FillVertex));
            vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            vb_.Reset();
            if (!device_ || FAILED(device_->CreateBuffer(&vd, nullptr, &vb_))) {
                capacity_ = 0;
                return false;
            }
            capacity_ = want;
        }
        return true;
    }

    void Draw(const space3d::ExtraDrawContext& dc) {
        if (!vs_ || !vb_ || vertices_.empty()) return;
        ID3D11DeviceContext* ctx = dc.ctx;
        const UINT count = static_cast<UINT>(std::min(vertices_.size(), capacity_));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
        std::memcpy(mapped.pData, vertices_.data(), count * sizeof(FillVertex));
        ctx->Unmap(vb_.Get(), 0);
        FillConstants k{};
        const space3d::Mat4 viewProj = dc.projection * dc.view;
        std::memcpy(k.viewProj, &viewProj.m[0][0], sizeof(k.viewProj));
        ctx->UpdateSubresource(cb_.Get(), 0, nullptr, &k, 0, 0);
        // 前のステートは描いた後に戻す (深度は ExtraDraw が受け取るとおり: テストする、書かない)。
        Ptr<ID3D11BlendState> blendBefore;
        Ptr<ID3D11RasterizerState> rasterBefore;
        Ptr<ID3D11InputLayout> layoutBefore;
        float factorBefore[4] = {};
        UINT maskBefore = 0xFFFFFFFF;
        ctx->OMGetBlendState(&blendBefore, factorBefore, &maskBefore);
        ctx->RSGetState(&rasterBefore);
        ctx->IAGetInputLayout(&layoutBefore);
        const float factor[4] = {};
        ctx->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFF);
        ctx->RSSetState(raster_.Get());
        ID3D11Buffer* cb = cb_.Get();
        ID3D11Buffer* vb = vb_.Get();
        const UINT stride = sizeof(FillVertex), offset = 0;
        ctx->IASetInputLayout(layout_.Get());
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetConstantBuffers(4, 1, &cb);
        ctx->VSSetShader(vs_.Get(), nullptr, 0);
        ctx->PSSetShader(ps_.Get(), nullptr, 0);
        ctx->Draw(count, 0);
        ID3D11Buffer* none = nullptr;
        const UINT zero = 0;
        ctx->VSSetConstantBuffers(4, 1, &none);
        ctx->IASetVertexBuffers(0, 1, &none, &zero, &zero);
        ctx->IASetInputLayout(layoutBefore.Get());
        ctx->OMSetBlendState(blendBefore.Get(), factorBefore, maskBefore);
        ctx->RSSetState(rasterBefore.Get());
    }

    std::map<std::string, Mesh> meshes_;  // 焼いた物の id ごと
    std::vector<FillVertex> vertices_;
    uint64_t generation_ = ~uint64_t{0};
    ID3D11Device* device_ = nullptr;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> cb_, vb_;
    Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11RasterizerState> raster_;
    size_t capacity_ = 0;
    bool failed_ = false;
};

}  // namespace

std::unique_ptr<game_x_3d::WorldDrawer> CreateNavFill() { return std::make_unique<NavFill>(); }

}  // namespace rm::navigation_x_3d
