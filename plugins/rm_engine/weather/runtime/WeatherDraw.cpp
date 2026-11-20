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
        movingOnly_.clear();
        upMap_ = -1;
        downMap_ = -1;
        anySnow_ = false;
        const Weather* w = f.scene.Setting<Weather>();
        if (!w || !f.atmosphere) return;
        const WeatherNow now = NowOf(*w);
        const Vec3 wind{now.windX, 0, now.windZ};
        // What falls (each with the 向きの地図 of its way) and what got wet (a map for each way).
        const Color light = LightOf(f.sky);
        // 霧 (and a downpour's, a snowstorm's): its colour by the weather, from the light in the air.
        if (const float fog = FogOf(now); fog > 0) {
            const Color c = FogColorOf(now, light);
            out.fog = {c.r, c.g, c.b, fog};
        }
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
        // 積雪 lies on what each way's map sees. Footprints (only while the game plays): the moving
        // shapes seen from below (a map of their own, of them only).
        for (const WetWay& way : wet_) anySnow_ = anySnow_ || way.snow > 0.0005f;
        if (!f.playing) {
            trailsOn_ = false;
        } else if (!trailsOn_) {
            trailsOn_ = true;
            ++trailCount_;  // (a new Play: no footprints yet)
        }
        trailFill_ = trailsOn_ ? SnowFallRate(now, SnowMostOf(*w)) * std::max(f.dt, 0.f) : 0.f;
        if (anySnow_) {  // (what the snow lies on: the tops, straight down)
            downMap_ = static_cast<int>(directions_.size());
            directions_.push_back({0, -1, 0});
        }
        if (anySnow_ && trailsOn_) {
            upMap_ = static_cast<int>(directions_.size());
            directions_.push_back({0, 1, 0});
        }
        movingOnly_.assign(directions_.size(), 0);
        if (upMap_ >= 0) movingOnly_[static_cast<size_t>(upMap_)] = 1;
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
            upMap_ = -1;
            downMap_ = -1;
        } else {
            out.mapDirections = directions_;
            out.mapShapes = shapes_;
            out.mapMovingOnly = movingOnly_;
        }
        if (!wet_.empty()) {
            surface_.code = kWetSurfaceHlsl;
            surface_.data = WetSurfaceData(wet_, wetMaps_);
            out.surface = &surface_;
        }
        // The snow lying with its height (on what the ways' maps see; thin: in lumps), and its footprints.
        if (anySnow_ && downMap_ >= 0 && LayerReady(f.gfx)) {
            solids_.push_back([this](const space3d::ExtraDrawContext& dc) { DrawSnow(dc); });
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

    // The snow lying's shaders (made from the renderer's text: lit as the models are), its grid's
    // corners and the footprints' two pictures - once per device.
    bool LayerReady(platform::Gfx& gfx) {
        if (layerGeneration_ == gfx.Generation() && (layerVs_ || layerFailed_)) return layerVs_ != nullptr;
        layerGeneration_ = gfx.Generation();
        layerVs_.Reset();
        layerPs_.Reset();
        layerCb_.Reset();
        layerIndices_.Reset();
        trailVs_.Reset();
        trailPs_.Reset();
        trailCb_.Reset();
        for (TrailPicture& t : trails_) t = {};
        layerFailed_ = false;
        ID3D11Device* device = gfx.D3D();
        if (!device) return false;
        const std::string base = space3d::RendererShaderText(kWetSurfaceHlsl);
        const std::string layerText = base + kSnowLayerHlsl, trailText = base + kSnowTrailHlsl;
        Ptr<ID3DBlob> vs, ps, tvs, tps;
        std::string error;
        auto cbuffer = [&](UINT bytes, Ptr<ID3D11Buffer>& out) {
            D3D11_BUFFER_DESC cd{};
            cd.Usage = D3D11_USAGE_DEFAULT;
            cd.ByteWidth = bytes;
            cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            return SUCCEEDED(device->CreateBuffer(&cd, nullptr, &out));
        };
        // The grid's two triangles a cell (its corners are numbered row by row: SnowLayerVS).
        std::vector<uint32_t> indices;
        indices.reserve(size_t{kSnowCells} * kSnowCells * 6);
        const uint32_t side = kSnowCells + 1;
        for (uint32_t y = 0; y < kSnowCells; ++y) {
            for (uint32_t x = 0; x < kSnowCells; ++x) {
                const uint32_t c = y * side + x;
                for (const uint32_t i : {c, c + 1, c + side, c + side, c + 1, c + side + 1}) indices.push_back(i);
            }
        }
        D3D11_BUFFER_DESC id{};
        id.Usage = D3D11_USAGE_IMMUTABLE;
        id.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
        id.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA idata{indices.data(), 0, 0};
        bool ok = Compile(layerText, "SnowLayerVS", "vs_4_0", vs, error) && Compile(layerText, "SnowLayerPS", "ps_4_0", ps, error) &&
                  Compile(trailText, "SnowTrailVS", "vs_4_0", tvs, error) && Compile(trailText, "SnowTrailPS", "ps_4_0", tps, error);
        ok = ok && SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &layerVs_)) &&
             SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &layerPs_)) &&
             SUCCEEDED(device->CreateVertexShader(tvs->GetBufferPointer(), tvs->GetBufferSize(), nullptr, &trailVs_)) &&
             SUCCEEDED(device->CreatePixelShader(tps->GetBufferPointer(), tps->GetBufferSize(), nullptr, &trailPs_)) &&
             cbuffer(sizeof(SnowLayerConstants), layerCb_) && cbuffer(sizeof(SnowTrailConstants), trailCb_) &&
             SUCCEEDED(device->CreateBuffer(&id, &idata, &layerIndices_));
        // The footprints: two pictures, each frame drawn from the other.
        for (TrailPicture& t : trails_) {
            if (!ok) break;
            D3D11_TEXTURE2D_DESC td{};
            td.Width = td.Height = kTrailCells;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            ok = SUCCEEDED(device->CreateTexture2D(&td, nullptr, &t.texture)) && SUCCEEDED(device->CreateShaderResourceView(t.texture.Get(), nullptr, &t.srv)) &&
                 SUCCEEDED(device->CreateRenderTargetView(t.texture.Get(), nullptr, &t.rtv));
        }
        trailsCleared_ = false;
        if (!ok) {
            log::Error("積もった雪を描く準備ができません: {}", error);
            layerVs_.Reset();
            layerFailed_ = true;
            return false;
        }
        return true;
    }

    // 積雪 (RenderFrame::solids: after the models, writing depth, lit as they are): the footprints move
    // on a frame (while the game plays), then a layer for each way with snow.
    void DrawSnow(const space3d::ExtraDrawContext& dc) {
        if (!layerVs_ || !dc.directionMaps) return;
        ID3D11DeviceContext* ctx = dc.ctx;
        ID3D11Buffer* none = nullptr;
        ID3D11ShaderResourceView* noTexture = nullptr;
        ID3D11Buffer* maps = dc.directionMapsCb;
        ID3D11ShaderResourceView* mapTexture = dc.directionMaps;
        if (!trailsCleared_) {  // (a new picture holds anything: no world cell matches what it says)
            const float nothing[4] = {1e30f, 1e30f, 1e30f, -1};
            for (TrailPicture& t : trails_) ctx->ClearRenderTargetView(t.rtv.Get(), nothing);
            trailsCleared_ = true;
        }
        if (trailsOn_) {
            TrailPicture& from = trails_[trailNow_];
            TrailPicture& to = trails_[1 - trailNow_];
            const SnowTrailConstants tk = SnowTrailOf(dc.eye, trailCount_, trailFill_, upMap_);
            ctx->UpdateSubresource(trailCb_.Get(), 0, nullptr, &tk, 0, 0);
            // Its own target and size; the picture's are set back after.
            Ptr<ID3D11RenderTargetView> rtvBefore;
            Ptr<ID3D11DepthStencilView> dsvBefore;
            ctx->OMGetRenderTargets(1, &rtvBefore, &dsvBefore);
            UINT viewports = 1;
            D3D11_VIEWPORT viewportBefore{};
            ctx->RSGetViewports(&viewports, &viewportBefore);
            Ptr<ID3D11BlendState> blendBefore;
            float factorBefore[4] = {};
            UINT maskBefore = 0xFFFFFFFF;
            ctx->OMGetBlendState(&blendBefore, factorBefore, &maskBefore);
            ctx->VSSetShaderResources(9, 1, &noTexture);
            ctx->OMSetRenderTargets(1, to.rtv.GetAddressOf(), nullptr);
            const D3D11_VIEWPORT vp{0, 0, static_cast<float>(kTrailCells), static_cast<float>(kTrailCells), 0, 1};
            ctx->RSSetViewports(1, &vp);
            ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
            ID3D11Buffer* cb = trailCb_.Get();
            ID3D11ShaderResourceView* before = from.srv.Get();
            ctx->PSSetConstantBuffers(4, 1, &cb);
            ctx->PSSetShaderResources(9, 1, &before);
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(trailVs_.Get(), nullptr, 0);
            ctx->PSSetShader(trailPs_.Get(), nullptr, 0);
            ctx->Draw(3, 0);
            ctx->PSSetShaderResources(9, 1, &noTexture);
            ctx->PSSetConstantBuffers(4, 1, &none);
            ctx->OMSetRenderTargets(1, rtvBefore.GetAddressOf(), dsvBefore.Get());
            ctx->RSSetViewports(1, &viewportBefore);
            ctx->OMSetBlendState(blendBefore.Get(), factorBefore, maskBefore);
            trailNow_ = 1 - trailNow_;
        }
        // The layers (corners read the maps and the footprints in the vertex shader; the pixel shader the
        // maps again - which way draws a place - and the models' lighting, still bound).
        ID3D11Buffer* cb = layerCb_.Get();
        ID3D11ShaderResourceView* trail = trails_[trailNow_].srv.Get();
        ctx->VSSetConstantBuffers(4, 1, &cb);
        ctx->PSSetConstantBuffers(4, 1, &cb);
        ctx->VSSetConstantBuffers(6, 1, &maps);
        ctx->VSSetShaderResources(8, 1, &mapTexture);
        ctx->VSSetShaderResources(9, 1, &trail);
        ctx->IASetInputLayout(nullptr);  // (each corner made from its number)
        ctx->IASetIndexBuffer(layerIndices_.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(layerVs_.Get(), nullptr, 0);
        ctx->PSSetShader(layerPs_.Get(), nullptr, 0);
        const SnowLayerConstants lk = SnowLayerOf(dc.eye, wet_, wetMaps_, downMap_, trailCount_, trailsOn_);
        ctx->UpdateSubresource(cb, 0, nullptr, &lk, 0, 0);
        ctx->DrawIndexed(kSnowCells * kSnowCells * 6, 0, 0);
        ctx->VSSetConstantBuffers(4, 1, &none);
        ctx->PSSetConstantBuffers(4, 1, &none);
        ctx->VSSetConstantBuffers(6, 1, &none);
        ctx->VSSetShaderResources(8, 1, &noTexture);
        ctx->VSSetShaderResources(9, 1, &noTexture);
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
        k.right[3] = snow ? 0.f : std::clamp(p.amount, 0.f, 1.f);  // (how heavy: the drops' look)
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
    std::vector<uint8_t> movingOnly_;  // RenderFrame::mapMovingOnly: the map from below
    int upMap_ = -1;                   // the moving shapes from below (footprints; -1: none)
    int downMap_ = -1;                 // what the snow lies on: the tops, straight down (-1: none)
    bool anySnow_ = false;
    // 積雪's GPU things (LayerReady) and the footprints: two pictures, the one drawn last (trailNow_);
    // trailCount_ goes up with each Play (each its own footprints); trailFill_: m to fill up this frame.
    struct TrailPicture {
        Ptr<ID3D11Texture2D> texture;
        Ptr<ID3D11ShaderResourceView> srv;
        Ptr<ID3D11RenderTargetView> rtv;
    };
    Ptr<ID3D11VertexShader> layerVs_, trailVs_;
    Ptr<ID3D11PixelShader> layerPs_, trailPs_;
    Ptr<ID3D11Buffer> layerCb_, trailCb_, layerIndices_;
    TrailPicture trails_[2];
    size_t trailNow_ = 0;
    uint32_t trailCount_ = 0;
    float trailFill_ = 0;
    bool trailsOn_ = false, trailsCleared_ = false;
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
