#include "rm_engine/weather/runtime/WeatherDraw.h"

#include "base/Log.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/RendererShaders.h"
#include "rm_engine/space3d/runtime/SkyShader.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/weather/runtime/Falling.h"
#include "rm_engine/weather/runtime/WeatherBlocks.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace rm::weather {

namespace {

template <class T>
using Ptr = Microsoft::WRL::ComPtr<T>;

bool Compile(const std::string& text, const char* entry, const char* target, Ptr<ID3DBlob>& out, std::string& error) {
    Ptr<ID3DBlob> messages;
    const HRESULT hr = D3DCompile(text.data(), text.size(), "weather", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &messages);
    if (SUCCEEDED(hr)) return true;
    error = std::string(entry) + (messages ? std::string(": ") + static_cast<const char*>(messages->GetBufferPointer()) : std::string());
    return false;
}

class WeatherDraw final : public game_x_3d::WorldDrawer {
public:
    void PrepareFrame(const game_x_3d::WorldDrawFrame& f, game_x_3d::FrameAdditions& out) override {
        directions_.clear();
        falls_.clear();
        solids_.clear();
        snowCover_ = 0;
        snowMap_ = -1;
        const Weather* w = f.scene.Setting<Weather>();
        if (!w || !f.atmosphere) return;
        const WeatherNow now = NowOf(*w);
        const Vec3 wind{now.windX, 0, now.windZ};
        // What falls (each with the 向きの地図 of its way) and what got wet (a map for each way).
        const Color light = LightOf(f.sky);
        for (const auto kind : {Precipitation::Kind::Rain, Precipitation::Kind::Snow}) {
            Precipitation p;
            p.kind = kind;
            p.amount = kind == Precipitation::Kind::Rain ? RainNow(now) : SnowNow(now);
            if (!(p.amount > 0)) continue;
            p.seconds = static_cast<float>(f.seconds);
            p.light = light;
            p.windX = now.windX;
            p.windZ = now.windZ;
            p.driftX = w->driftX;
            p.driftZ = w->driftZ;
            p.map = static_cast<int>(directions_.size());
            directions_.push_back(FallDirection(kind, wind));
            falls_.push_back(p);
        }
        wet_ = WetNow(*w);
        if (wet_.size() > kWetWays) wet_.resize(kWetWays);
        wetMaps_.clear();
        for (const WetWay& way : wet_) {
            wetMaps_.push_back(static_cast<int>(directions_.size()));
            directions_.push_back(way.way);
        }
        // 積雪: what the snow reaches straight down (a map of its own).
        snowCover_ = SnowCoverNow(*w);
        if (snowCover_ > 0) {
            snowMap_ = static_cast<int>(directions_.size());
            directions_.push_back({0, -1, 0});
        }
        if (directions_.empty()) return;
        // What they hit: the colliders (the connector 天候 × 物理), never drawn.
        if (f.registry) {
            MakeWeatherBlocks(f.scene, *f.registry, f.world, f.shape, f.model, shapes_, blockers_);
        } else {
            shapes_.clear();  // (never the last frame's: their models may be gone)
        }
        if (shapes_.empty()) {
            for (Precipitation& p : falls_) p.map = -1;  // (nothing to hit: no maps - all open)
            std::fill(wetMaps_.begin(), wetMaps_.end(), -1);
            snowMap_ = -1;
        } else {
            out.mapDirections = directions_;
            out.mapShapes = shapes_;
        }
        if (!wet_.empty() || snowCover_ > 0) {
            surface_.code = kWetSurfaceHlsl;
            surface_.data = WetSurfaceData(wet_, wetMaps_, snowCover_, snowMap_);
            out.surface = &surface_;
        }
        // Deeper than a centimetre: the snow lying with its height (on the tops the map sees).
        if (snowCover_ >= 0.01f && snowMap_ >= 0 && LayerReady(f.gfx)) {
            solids_.push_back([this](const space3d::ExtraDrawContext& dc) { DrawSnowLayer(dc); });
            out.solids = solids_;
        }
    }

    void Prepare(const game_x_3d::WorldDrawFrame& f, std::vector<space3d::ExtraDraw>& out) override {
        animating_ = !falls_.empty() && f.playing;
        if (falls_.empty() || !Ready(f.gfx)) return;
        for (const Precipitation& p : falls_) {
            out.push_back([this, p](const space3d::ExtraDrawContext& dc) { DrawFall(dc, p); });
        }
    }

    bool Animating() const override { return animating_; }

private:
    // What lights the drops: the sky and the sun / moon (and a lightning flash) - about as much as the
    // ground under them. No sky: a dull grey.
    static Color LightOf(const game_x_3d::SkyLook* sky) {
        if (!sky) return {0.6f, 0.6f, 0.65f, 1};
        const float s = sky->lightStrength * 0.9f, f = sky->flash;
        return {sky->ambientSky.r * 2 + sky->lightColor.r * s + f, sky->ambientSky.g * 2 + sky->lightColor.g * s + f,
                sky->ambientSky.b * 2 + sky->lightColor.b * s + f, 1};
    }

    // The shaders and the constant buffer, on the picture's device (made again with new devices).
    bool Ready(platform::Gfx& gfx) {
        if (generation_ == gfx.Generation() && (vs_ || failed_)) return vs_ != nullptr;
        generation_ = gfx.Generation();
        vs_.Reset();
        ps_.Reset();
        cb_.Reset();
        blend_.Reset();
        raster_.Reset();
        failed_ = false;
        ID3D11Device* device = gfx.D3D();
        if (!device) return false;
        const std::string text = std::string(space3d::kColorHlsl) + space3d::kDirectionMapHlsl + kFallHlsl;
        Ptr<ID3DBlob> vs, ps;
        std::string error;
        D3D11_BUFFER_DESC cd{};
        cd.Usage = D3D11_USAGE_DEFAULT;
        cd.ByteWidth = sizeof(FallConstants);
        cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        // Its own states (an ExtraDraw gets what the last one left: a glowing particle's adding up).
        D3D11_BLEND_DESC bl{};
        bl.RenderTarget[0].BlendEnable = TRUE;
        bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        rd.MultisampleEnable = TRUE;
        if (!Compile(text, "FallVS", "vs_4_0", vs, error) || !Compile(text, "FallPS", "ps_4_0", ps, error) ||
            FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_)) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_)) ||
            FAILED(device->CreateBuffer(&cd, nullptr, &cb_)) || FAILED(device->CreateBlendState(&bl, &blend_)) ||
            FAILED(device->CreateRasterizerState(&rd, &raster_))) {
            log::Error("雨・雪を描く準備ができません: {}", error);
            vs_.Reset();
            failed_ = true;
            return false;
        }
        return true;
    }

    // The snow lying's shaders (made from the renderer's text: lit as the models are), once per device.
    bool LayerReady(platform::Gfx& gfx) {
        if (layerGeneration_ == gfx.Generation() && (layerVs_ || layerFailed_)) return layerVs_ != nullptr;
        layerGeneration_ = gfx.Generation();
        layerVs_.Reset();
        layerPs_.Reset();
        layerCb_.Reset();
        layerFailed_ = false;
        ID3D11Device* device = gfx.D3D();
        if (!device) return false;
        const std::string text = space3d::RendererShaderText(kWetSurfaceHlsl) + kSnowLayerHlsl;
        Ptr<ID3DBlob> vs, ps;
        std::string error;
        D3D11_BUFFER_DESC cd{};
        cd.Usage = D3D11_USAGE_DEFAULT;
        cd.ByteWidth = sizeof(SnowLayerConstants);
        cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (!Compile(text, "SnowLayerVS", "vs_4_0", vs, error) || !Compile(text, "SnowLayerPS", "ps_4_0", ps, error) ||
            FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &layerVs_)) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &layerPs_)) ||
            FAILED(device->CreateBuffer(&cd, nullptr, &layerCb_))) {
            log::Error("積もった雪を描く準備ができません: {}", error);
            layerVs_.Reset();
            layerFailed_ = true;
            return false;
        }
        return true;
    }

    // The snow lying (RenderFrame::solids: after the models, writing depth, lit as they are). Its corners
    // read the map straight down in the vertex shader; the pixel shader uses the models' lighting, still
    // bound.
    void DrawSnowLayer(const space3d::ExtraDrawContext& dc) {
        if (!layerVs_ || !dc.directionMaps) return;
        const SnowLayerConstants k = SnowLayerOf(dc.eye, snowCover_, snowMap_);
        ID3D11DeviceContext* ctx = dc.ctx;
        ctx->UpdateSubresource(layerCb_.Get(), 0, nullptr, &k, 0, 0);
        ID3D11Buffer* cb = layerCb_.Get();
        ID3D11Buffer* maps = dc.directionMapsCb;
        ID3D11ShaderResourceView* mapTexture = dc.directionMaps;
        ctx->VSSetConstantBuffers(4, 1, &cb);
        ctx->VSSetConstantBuffers(6, 1, &maps);
        ctx->VSSetShaderResources(8, 1, &mapTexture);
        ctx->IASetInputLayout(nullptr);  // (each corner made from its number)
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(layerVs_.Get(), nullptr, 0);
        ctx->PSSetShader(layerPs_.Get(), nullptr, 0);
        ctx->Draw(kSnowCells * kSnowCells * 6, 0);
        ID3D11Buffer* none = nullptr;
        ID3D11ShaderResourceView* noTexture = nullptr;
        ctx->VSSetConstantBuffers(4, 1, &none);
        ctx->VSSetConstantBuffers(6, 1, &none);
        ctx->VSSetShaderResources(8, 1, &noTexture);
    }

    // Rain or snow around the eye: see-through, depth-tested, writing no depth (as an ExtraDraw is
    // drawn). They read the 向きの地図 of their way in the vertex shader: where they hit.
    void DrawFall(const space3d::ExtraDrawContext& dc, const Precipitation& p) {
        const uint32_t count = PrecipitationCount(p);
        if (count == 0 || !vs_) return;
        const bool snow = p.kind == Precipitation::Kind::Snow;
        const FallSpeed speed = FallOf(p.kind);
        FallConstants k{};
        const space3d::Mat4 viewProj = dc.projection * dc.view;
        std::memcpy(k.viewProj, &viewProj.m[0][0], sizeof(k.viewProj));
        k.eye[0] = dc.eye.x, k.eye[1] = dc.eye.y, k.eye[2] = dc.eye.z;
        k.info[0] = snow ? 1.f : 0.f, k.info[1] = p.seconds, k.info[2] = snow ? kSnowBox : kRainBox;
        k.info[3] = dc.directionMaps ? static_cast<float>(p.map) : -1.f;
        k.light[0] = p.light.r, k.light[1] = p.light.g, k.light[2] = p.light.b;
        // The camera's right and up: the view's first two rows (m[column][row]).
        k.right[0] = dc.view.m[0][0], k.right[1] = dc.view.m[1][0], k.right[2] = dc.view.m[2][0];
        k.up[0] = dc.view.m[0][1], k.up[1] = dc.view.m[1][1], k.up[2] = dc.view.m[2][1];
        k.wind[0] = p.windX, k.wind[1] = p.windZ, k.wind[2] = p.driftX, k.wind[3] = p.driftZ;
        k.fall[0] = speed.least, k.fall[1] = speed.spread, k.fall[2] = speed.carried;
        ID3D11DeviceContext* ctx = dc.ctx;
        ctx->UpdateSubresource(cb_.Get(), 0, nullptr, &k, 0, 0);
        ID3D11Buffer* cb = cb_.Get();
        ID3D11Buffer* maps = dc.directionMapsCb;
        ID3D11ShaderResourceView* mapTexture = dc.directionMaps;
        ctx->VSSetConstantBuffers(4, 1, &cb);
        ctx->PSSetConstantBuffers(4, 1, &cb);
        ctx->VSSetConstantBuffers(6, 1, &maps);
        ctx->VSSetShaderResources(8, 1, &mapTexture);
        // Its own blend and rasterizer, set back after (the depth: as an ExtraDraw gets it - tested, not written).
        Ptr<ID3D11BlendState> blendBefore;
        Ptr<ID3D11RasterizerState> rasterBefore;
        float factorBefore[4] = {};
        UINT maskBefore = 0xFFFFFFFF;
        ctx->OMGetBlendState(&blendBefore, factorBefore, &maskBefore);
        ctx->RSGetState(&rasterBefore);
        const float factor[4] = {};
        ctx->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFF);
        ctx->RSSetState(raster_.Get());
        ctx->IASetInputLayout(nullptr);  // (each corner made from its number)
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs_.Get(), nullptr, 0);
        ctx->PSSetShader(ps_.Get(), nullptr, 0);
        ctx->Draw(count * 6, 0);
        ID3D11Buffer* none = nullptr;
        ID3D11ShaderResourceView* noTexture = nullptr;
        ctx->VSSetConstantBuffers(4, 1, &none);
        ctx->PSSetConstantBuffers(4, 1, &none);
        ctx->VSSetConstantBuffers(6, 1, &none);
        ctx->VSSetShaderResources(8, 1, &noTexture);
        ctx->OMSetBlendState(blendBefore.Get(), factorBefore, maskBefore);
        ctx->RSSetState(rasterBefore.Get());
    }

    std::vector<Vec3> directions_;
    std::vector<Precipitation> falls_;
    std::vector<WetWay> wet_;
    std::vector<int> wetMaps_;
    std::vector<space3d::MapShape> shapes_;
    std::vector<WeatherBlocker> blockers_;
    space3d::SurfaceTouch surface_;
    std::vector<space3d::ExtraDraw> solids_;
    float snowCover_ = 0;
    int snowMap_ = -1;
    Ptr<ID3D11VertexShader> layerVs_;
    Ptr<ID3D11PixelShader> layerPs_;
    Ptr<ID3D11Buffer> layerCb_;
    uint64_t layerGeneration_ = ~uint64_t{0};
    bool layerFailed_ = false;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11Buffer> cb_;
    Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11RasterizerState> raster_;
    uint64_t generation_ = ~uint64_t{0};
    bool failed_ = false;
    bool animating_ = false;
};

}  // namespace

std::unique_ptr<game_x_3d::WorldDrawer> CreateWeatherDraw() { return std::make_unique<WeatherDraw>(); }

}  // namespace rm::weather
