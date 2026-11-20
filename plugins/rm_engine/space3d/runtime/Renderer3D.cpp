#include "rm_engine/space3d/runtime/Renderer3D.h"

#include "rm_engine/space3d/runtime/Lighting.h"
#include "base/Log.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/RendererShaders.h"
#include "rm_engine/space3d/runtime/TextureFile.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace rm::space3d {

namespace {

// The constant buffer `Frame`.
struct FrameConstants {
    float viewProj[16];
    float lightViewProj[16];
    float eye[4];
    float fog[4];  // rgb: its colour (linear); a: how thick (per metre)
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

// The renderer's shaders with ToScreen / ToLinear (SkyShader.h: the sky's are the same) in front.
const std::string& ShaderText() {
    static const std::string text = RendererShaderText();
    return text;
}

bool Compile(const std::string& text, const char* name, const char* entry, const char* target, Microsoft::WRL::ComPtr<ID3DBlob>& out,
             std::string* error) {
    Microsoft::WRL::ComPtr<ID3DBlob> messages;
    const HRESULT hr = D3DCompile(text.data(), text.size(), name, nullptr, nullptr, entry, target,
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
bool Compile(const char* entry, const char* target, Microsoft::WRL::ComPtr<ID3DBlob>& out, std::string* error) {
    return Compile(ShaderText(), "space3d", entry, target, out, error);
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

struct SkyProgram {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
};

struct Renderer::Shared {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11VertexShader> lineVs, meshVs, shadowVs, clearVs;
    Ptr<ID3D11PixelShader> linePs, meshPs, floorPs, copyPs;
    // MeshPS with a plugin's touch (RenderFrame::surface), by its code (null: it would not compile - told once).
    std::unordered_map<std::string, Ptr<ID3D11PixelShader>> surfaces;
    Ptr<ID3D11InputLayout> lineLayout, meshLayout;
    Ptr<ID3D11Buffer> frame, lights, shadows;  // the constant buffers (b0, b1, b2)
    std::shared_ptr<const GpuMesh> cube, floor;
    std::array<std::shared_ptr<const GpuMesh>, 5> shapes;  // by Shape (the box is `cube`)
    Ptr<ID3D11RasterizerState> raster, shadowRaster;
    Ptr<ID3D11DepthStencilState> depth, depthNoWrite, depthClear, depthBehind;
    Ptr<ID3D11BlendState> blend;
    // Pictures: one for each フィルタ and 端の扱い (the image's import settings - TextureSampling.h's
    // SamplerIndex): 滑らか = smooth, mipmapped and anisotropic; くっきり = the nearest pixel (mipmapped far away).
    std::array<Ptr<ID3D11SamplerState>, kSamplerCount> samplers;
    Ptr<ID3D11SamplerState> shadowSampler;
    // The sky: its constant buffer (b3), a sampler stopping at the edges (one of six pictures), the
    // depth test off (drawn first, writing nothing), and the programs made so far by their text.
    Ptr<ID3D11Buffer> skyFrame;
    Ptr<ID3D11Buffer> directionCb, surfaceCb;  // 向きの地図 (b6) and a plugin's surface numbers (b5)
    Ptr<ID3D11SamplerState> edgeSampler;
    Ptr<ID3D11DepthStencilState> depthOff;
    // ミップマップ・ストリーミング: the streamed pictures made on this device (Renderer::Stream).
    std::mutex streamMutex;
    std::vector<std::weak_ptr<const GpuTexture>> streamed;
    std::mutex skyMutex;
    std::unordered_map<std::string, std::weak_ptr<const SkyProgram>> skies;
    // The shadow maps (depth seen from the lights): the sun's (1, or kCascades) and the point / spot
    // lights' (kLocalShadowSlices). Made when first needed. And the map from above (雨・雪・濡れ:
    // Lighting.h's PlanCover).
    ShadowMaps sun, local, directionMaps;
    UINT samples = 1;

    bool Init(Renderer& owner, std::string* error);
    bool EnsureMaps(ShadowMaps& maps, uint32_t tiles, uint32_t size);
    bool EnsureLayer(ShadowMaps& maps);
    ID3D11PixelShader* MeshPsFor(std::string_view surface);
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

const char kSamplerMark[kSamplerCount] = {};  // PictureSet's mark for the colour picture's sampler

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
    for (size_t k = 0; k < shapes.size(); ++k) {
        shapes[k] = owner.Upload(ShapeMesh(static_cast<Shape>(k)), &why);
        if (!shapes[k]) return fail("形");
    }
    cube = shapes[static_cast<size_t>(Shape::Box)];
    floor = owner.Upload(FloorPlane(), &why);
    if (!floor) return fail("床");
    D3D11_BUFFER_DESC cd{};
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.ByteWidth = sizeof(FrameConstants);
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cd, nullptr, &frame))) return fail("定数");
    cd.ByteWidth = sizeof(LightBlock);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &lights))) return fail("光");
    cd.ByteWidth = sizeof(ShadowConstants);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &shadows))) return fail("影");
    cd.ByteWidth = sizeof(DirectionMapConstants);
    if (FAILED(device->CreateBuffer(&cd, nullptr, &directionCb))) return fail("向きの地図");
    cd.ByteWidth = sizeof(float) * 4 * kSurfaceData;
    if (FAILED(device->CreateBuffer(&cd, nullptr, &surfaceCb))) return fail("面の塗り");

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
    D3D11_DEPTH_STENCIL_DESC db = dn;  // only where something is in front (debug lines' hidden parts)
    db.DepthFunc = D3D11_COMPARISON_GREATER;
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
        FAILED(device->CreateDepthStencilState(&db, &depthBehind)) ||
        FAILED(device->CreateDepthStencilState(&dc, &depthClear)) || FAILED(device->CreateBlendState(&bl, &blend))) {
        return fail("描画の設定");
    }
    for (size_t k = 0; k < kSamplerCount; ++k) {
        D3D11_SAMPLER_DESC d = ts;
        if (k >= 3) {  // くっきり: the nearest pixel (the smaller copies still far away)
            d.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
            d.MaxAnisotropy = 1;
        }
        static constexpr D3D11_TEXTURE_ADDRESS_MODE kWrap[3] = {D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_MIRROR};
        d.AddressU = d.AddressV = d.AddressW = kWrap[k % 3];
        if (FAILED(device->CreateSamplerState(&d, &samplers[k]))) return fail("描画の設定");
    }
    if (FAILED(device->CreateSamplerState(&ss, &shadowSampler))) return fail("影");
    cd.ByteWidth = sizeof(SkyConstants);
    D3D11_SAMPLER_DESC es = ts;
    es.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    es.MaxAnisotropy = 1;
    es.AddressU = es.AddressV = es.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    D3D11_DEPTH_STENCIL_DESC off{};
    off.DepthEnable = FALSE;
    off.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    off.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device->CreateBuffer(&cd, nullptr, &skyFrame)) ||
        FAILED(device->CreateSamplerState(&es, &edgeSampler)) ||
        FAILED(device->CreateDepthStencilState(&off, &depthOff))) {
        return fail("空");
    }

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

ID3D11PixelShader* Renderer::Shared::MeshPsFor(std::string_view surface) {
    if (surface.empty()) return meshPs.Get();
    const std::string key(surface);
    if (const auto it = surfaces.find(key); it != surfaces.end()) return it->second ? it->second.Get() : meshPs.Get();
    Ptr<ID3D11PixelShader>& ps = surfaces[key];  // (null if it fails: not tried again)
    Ptr<ID3DBlob> code;
    std::string error;
    if (!Compile(RendererShaderText(surface), "surface", "MeshPS", "ps_4_0", code, &error) ||
        FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps))) {
        log::Error("面の塗りに足すシェーダーを作れません (塗りはそのまま): {}", error);
        ps.Reset();
        return meshPs.Get();
    }
    return ps.Get();
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
    // Called again (the GPU's devices made again): nothing of the device before is kept.
    lineVb_.Reset();
    lineCapacity_ = 0;
    thingsBuffer_.Reset();
    drawsBuffer_.Reset();
    thingsView_.Reset();
    thingsCapacity_ = drawsCapacity_ = 0;
    pictureSets_.clear();
    picture_.Reset();
    multisample_.Reset();
    rtv_.Reset();
    depthTex_.Reset();
    dsv_.Reset();
    depthSrv_.Reset();
    width_ = height_ = 0;
    samples_ = 1;
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

std::shared_ptr<const SkyProgram> Renderer::MakeSky(const std::string& hlsl, std::string* error) {
    if (!shared_) {
        if (error) *error = "3D の描画が始まっていません";
        return nullptr;
    }
    Shared& s = *shared_;
    {
        std::lock_guard lock(s.skyMutex);
        std::erase_if(s.skies, [](const auto& kv) { return kv.second.expired(); });
        if (const auto it = s.skies.find(hlsl); it != s.skies.end()) {
            if (auto found = it->second.lock()) return found;
        }
    }
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
    if (!Compile(hlsl, "sky", "SkyPS", "ps_4_0", ps, error) || !Compile(hlsl, "sky", "SkyVS", "vs_4_0", vs, error)) return nullptr;
    auto program = std::make_shared<SkyProgram>();
    if (FAILED(s.device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &program->vs)) ||
        FAILED(s.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &program->ps))) {
        if (error) *error = "空のシェーダーを GPU に載せられませんでした";
        return nullptr;
    }
    std::lock_guard lock(s.skyMutex);
    s.skies[hlsl] = program;
    return program;
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

std::shared_ptr<const GpuTexture> Renderer::UploadTexture(const platform::Image& rgba, bool srgb, std::string* error, const TextureSampling& sampling) {
    return UploadTexture(MakeTexturePixels(rgba, srgb), error, sampling);
}

namespace {

// ミップマップ・ストリーミング: the levels this small (or smaller) are always on the GPU.
constexpr uint32_t kStreamAlwaysKept = 64;
// A finer level asked for is kept this long after the last draw that needed it (seconds); one not
// needed by any draw that long goes back to the always-kept levels.
constexpr double kStreamKeepFine = 2.0;
constexpr double kStreamForget = 5.0;
// Finer levels put on the GPU in one frame at most (the rest the next frames).
constexpr int kStreamUploadsPerFrame = 8;

uint32_t SmallestKept(const TexturePixels& p) {
    uint32_t k = 0;
    while (k + 1 < p.levels.size() && std::max(p.levels[k].width, p.levels[k].height) > kStreamAlwaysKept) ++k;
    return k;
}

double StreamClock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// Levels top .. the last of `pixels` as one texture on the GPU. False and *why when it cannot.
bool MakeTextureView(ID3D11Device* device, const TexturePixels& pixels, uint32_t top, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view,
                     size_t& bytes, const char** why) {
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    size_t total = 0;
    for (size_t k = top; k < pixels.levels.size(); ++k) {
        const platform::Image& l = pixels.levels[k];
        if (l.pixels.size() < LevelBytes(pixels.format, l.width, l.height)) {
            *why = "画像のデータが足りません";
            return false;
        }
        init.push_back({l.pixels.data(), static_cast<UINT>(RowPitch(pixels.format, l.width)), 0});
        total += l.pixels.size();
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = pixels.levels[top].width;
    td.Height = pixels.levels[top].height;
    td.MipLevels = static_cast<UINT>(init.size());
    td.ArraySize = 1;
    // sRGB forms: the GPU reads linear light. The numbers are DXGI_FORMAT's (TextureFile.h).
    td.Format = static_cast<DXGI_FORMAT>(DxgiFormatOf(pixels.format, pixels.srgb));
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> made;
    if (FAILED(device->CreateTexture2D(&td, init.data(), &tex)) || FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, &made))) {
        *why = "画像を GPU に載せられませんでした (メモリーが足りない?)";
        return false;
    }
    view = std::move(made);
    bytes = total;
    return true;
}

}  // namespace

std::shared_ptr<const GpuTexture> Renderer::UploadTexture(const TexturePixels& pixels, std::string* error, const TextureSampling& sampling) {
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
    auto gpu = std::make_shared<GpuTexture>();
    gpu->sampling = sampling;
    gpu->width = top.width;
    gpu->height = top.height;
    gpu->srgb = pixels.srgb;
    gpu->format = pixels.format;
    gpu->levels = static_cast<uint32_t>(pixels.levels.size());
    gpu->madeBy = id_;
    // Streamed (and with smaller copies): only the always-kept small levels now; Render adds finer ones.
    const bool streamed = sampling.streamed && pixels.levels.size() > 1;
    const uint32_t first = streamed ? SmallestKept(pixels) : 0;
    const char* why = nullptr;
    if (!MakeTextureView(device_.Get(), pixels, first, gpu->view, gpu->bytes, &why)) return fail(why);
    if (streamed) {
        gpu->stream_ = std::make_unique<GpuTexture::Stream>();
        gpu->stream_->pixels = std::make_shared<const TexturePixels>(pixels);
        gpu->stream_->top = first;
        gpu->stream_->want = first;
        std::lock_guard lock(shared_->streamMutex);
        shared_->streamed.push_back(gpu);
    }
    return gpu;
}

void Renderer::Stream(const RenderFrame& f, const Vec3& eye) {
    Shared& s = *shared_;
    std::vector<std::shared_ptr<const GpuTexture>> all;
    {
        std::lock_guard lock(s.streamMutex);
        if (s.streamed.empty()) return;
        std::erase_if(s.streamed, [](const std::weak_ptr<const GpuTexture>& w) { return w.expired(); });
        for (const auto& w : s.streamed) {
            if (auto t = w.lock(); t && t->madeBy == id_) all.push_back(std::move(t));
        }
    }
    if (all.empty()) return;
    // The finest level each is seen at in this picture: the picture spread over the thing's size on
    // the screen (its bounds' sphere), one level finer for pictures repeated over it.
    const bool ortho = f.projection.m[3][3] == 1.f;
    const float pixelsPerUnit = f.projection.m[1][1] * 0.5f * static_cast<float>(f.height);
    streamSeen_.clear();
    for (const Item& it : items_) {
        if (!it.seen || it.materials.empty()) continue;
        const Vec3 c = (it.bounds.min + it.bounds.max) * 0.5f;
        const float r = Length(it.bounds.max - it.bounds.min) * 0.5f;
        const float d = std::max(Length(c - eye) - r, 0.05f);
        const float onScreen = std::max(1.f, 2.f * r * pixelsPerUnit / (ortho ? 1.f : d));
        for (const DrawMaterial& m : it.materials) {
            for (const GpuTexture* t : {m.texture.get(), m.metalRough.get(), m.normal.get(), m.emission.get()}) {
                if (!t || !t->stream_) continue;
                const float ratio = static_cast<float>(std::max(t->width, t->height)) / onScreen;
                const uint32_t level = ratio <= 2.f ? 0u : static_cast<uint32_t>(std::floor(std::log2(ratio))) - 1u;
                auto [at, added] = streamSeen_.try_emplace(t, level);
                if (!added) at->second = std::min(at->second, level);
            }
        }
    }
    const double now = StreamClock();
    int uploads = 0;
    for (const auto& t : all) {
        GpuTexture::Stream& st = *t->stream_;
        const uint32_t smallest = SmallestKept(*st.pixels);
        if (const auto at = streamSeen_.find(t.get()); at != streamSeen_.end()) {
            const uint32_t level = std::min(at->second, smallest);
            if (level <= st.want || now - st.wantAt > kStreamKeepFine) {
                st.want = level;
                st.wantAt = now;
            }
        } else if (now - st.wantAt > kStreamForget) {
            st.want = smallest;  // not drawn for a while (by any renderer): only the small levels
        }
        if (st.want == st.top) continue;
        if (st.want < st.top && uploads >= kStreamUploadsPerFrame) continue;
        const char* why = nullptr;
        size_t bytes = 0;
        if (MakeTextureView(device_.Get(), *st.pixels, st.want, t->view, bytes, &why)) {
            st.top = st.want;
            t->bytes = bytes;
            uploads += 1;
        }
    }
}

const GpuMesh* Renderer::ShapeOf(Shape shape) const {
    const size_t k = static_cast<size_t>(shape);
    return shared_ && k < shared_->shapes.size() ? shared_->shapes[k].get() : nullptr;
}

bool Renderer::SupportsBC7() const { return device_ && device_->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0; }

bool Renderer::Resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_ && picture_) return true;
    picture_.Reset();
    multisample_.Reset();
    rtv_.Reset();
    depthTex_.Reset();
    dsv_.Reset();
    depthSrv_.Reset();
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
    // The depth: typeless, so shaders can read it too (particles collide with what is drawn).
    td.Format = DXGI_FORMAT_R24G8_TYPELESS;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    td.SampleDesc.Count = samples_;
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.ViewDimension = samples_ > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &depthTex_)) || FAILED(device_->CreateDepthStencilView(depthTex_.Get(), &dd, &dsv_))) {
        // (a GPU that cannot read a depth buffer: one only for the depth test)
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthTex_.Reset();
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &depthTex_)) || FAILED(device_->CreateDepthStencilView(depthTex_.Get(), nullptr, &dsv_))) {
            return false;
        }
    } else {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sd.ViewDimension = samples_ > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(depthTex_.Get(), &sd, &depthSrv_))) depthSrv_.Reset();
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
                                         m->texture ? static_cast<const void*>(&kSamplerMark[SamplerIndex(m->texture->sampling)]) : nullptr};
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
    stats_ = Stats{};
    stats_.things = items_.size();
    const Frustum camera = FrustumOf(viewProj);
    for (Item& it : items_) {
        it.bounds = WorldBounds(*it.world, it.mesh->min, it.mesh->max);
        it.seen = BoxInFrustum(camera, it.bounds);
        if (it.seen) ++stats_.drawn;
    }
    // The shapes of the 向きの地図 (a plugin's: 天候's colliders): never in the picture, only in those maps.
    for (const MapShape& b : f.mapShapes) {
        if (!b.mesh || b.mesh->indexCount == 0) continue;
        items_.push_back({b.mesh, &b.world, {1, 1, 1, 1}, false, {}, {0, 0, 0, 0}});
        Item& it = items_.back();
        it.moving = b.moving;
        it.mapOnly = true;
        it.seen = false;
        it.bounds = WorldBounds(b.world, b.mesh->min, b.mesh->max);
    }
    Vec3 eye{0, 0, 0}, forward{0, 0, -1};
    if (const auto toWorld = Inverse(f.view)) {
        eye = TransformPoint(*toWorld, {0, 0, 0});
        forward = Normalize(TransformDirection(*toWorld, {0, 0, -1}));
    }
    Stream(f, eye);  // ミップマップ・ストリーミング: the levels the streamed pictures need now
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
    // The same for the 向きの地図's shapes (mapOrder_).
    for (const bool blocks : {false, true}) {
        keys_.clear();
        still_.clear();
        for (uint32_t i = 0; i < items_.size(); ++i) {
            Item& it = items_[i];
            if (blocks ? !it.mapOnly : !it.castShadow) continue;
            it.hash = HashWords(HashWords(kFingerprintStart, &it.mesh->serial, sizeof(it.mesh->serial)), it.world, sizeof(Mat4));
            keys_.push_back({0, 0, it.mesh->serial, 0});
            still_.push_back(i);
        }
        SortDraws(keys_, order_);
        std::vector<uint32_t>& list = blocks ? mapOrder_ : casterOrder_;
        list.clear();
        for (const uint32_t k : order_) list.push_back(still_[k]);
    }
    mapMovingOrder_.clear();  // (the moving ones only: RenderFrame::mapMovingOnly)
    for (const uint32_t i : mapOrder_) {
        if (items_[i].moving) mapMovingOrder_.push_back(i);
    }

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
    auto planMap = [&](ShadowMaps& maps, uint32_t tile, const Mat4& lightViewProj, const std::vector<uint32_t>& drawn) {
        const Frustum view = FrustumOf(lightViewProj);
        uint64_t still = HashWords(kFingerprintStart, &lightViewProj, sizeof(Mat4)), moving = kFingerprintStart;
        still_.clear();
        moving_.clear();
        for (const uint32_t i : drawn) {
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
                planMap(s.sun, static_cast<uint32_t>(m), plan.viewProj[m], casterOrder_);
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
            planMap(s.local, static_cast<uint32_t>(m), lights.localViewProj[m], casterOrder_);
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
    // 向きの地図: the hidden shapes seen along each direction asked for (a plugin's: 天候's rain, snow
    // and what got wet). No shapes: none (everything open, MapPast says).
    DirectionMapConstants dm{};
    const uint32_t mapCount = static_cast<uint32_t>(std::min<size_t>(f.mapDirections.size(), kDirectionMaps));
    dm.size[0] = static_cast<float>(kCoverSize);
    dm.size[1] = static_cast<float>(mapCount);
    bool directionMaps = false;
    if (mapCount > 0 && !mapOrder_.empty() && s.EnsureMaps(s.directionMaps, kDirectionMaps, kCoverSize)) {
        directionMaps = true;
        for (uint32_t k = 0; k < mapCount; ++k) {
            const CoverPlan plan = PlanCover(eye, f.mapDirections[k]);
            CopyMatrix(dm.viewProj[k], plan.viewProj);
            dm.info[k][0] = 1;
            dm.info[k][1] = static_cast<float>((k % s.directionMaps.cols) * kCoverSize);
            dm.info[k][2] = static_cast<float>((k / s.directionMaps.cols) * kCoverSize);
            dm.info[k][3] = plan.range;
            const bool movingOnly = k < f.mapMovingOnly.size() && f.mapMovingOnly[k] != 0;
            planMap(s.directionMaps, k, plan.viewProj, movingOnly ? mapMovingOrder_ : mapOrder_);
        }
    }

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
    {
        std::unordered_set<const GpuTexture*> used;
        for (const Cmd& cmd : cmds_) {
            stats_.triangles += static_cast<size_t>(cmd.indexCount / 3) * cmd.instances;
            if (!cmd.material) continue;
            for (const TextureRef* t : {&cmd.material->texture, &cmd.material->metalRough, &cmd.material->normal, &cmd.material->emission}) {
                if (!*t || !used.insert(t->get()).second) continue;
                ++stats_.textures;
                stats_.textureBytes += (*t)->bytes;
                if ((*t)->sampling.streamed) {
                    ++stats_.streamed;
                    if ((*t)->TopLevel() > 0) ++stats_.streamedCut;
                }
            }
        }
    }

    // The frame's numbers go to the GPU at once.
    if (!UploadFrame(ctx)) return nullptr;
    FrameConstants fc{};
    CopyMatrix(fc.viewProj, viewProj);
    fc.eye[0] = eye.x, fc.eye[1] = eye.y, fc.eye[2] = eye.z;
    if (f.fog.a > 0 && std::isfinite(f.fog.a)) {
        fc.fog[0] = f.fog.r, fc.fog[1] = f.fog.g, fc.fog[2] = f.fog.b, fc.fog[3] = std::min(f.fog.a, 10.f);
    }
    ctx->UpdateSubresource(s.lights.Get(), 0, nullptr, &lights.block, 0, 0);
    ctx->UpdateSubresource(s.shadows.Get(), 0, nullptr, &sc, 0, 0);
    ctx->UpdateSubresource(s.directionCb.Get(), 0, nullptr, &dm, 0, 0);
    if (f.surface) {
        float data[4 * kSurfaceData] = {};
        std::copy_n(f.surface->data.begin(), std::min<size_t>(f.surface->data.size(), std::size(data)), data);
        ctx->UpdateSubresource(s.surfaceCb.Get(), 0, nullptr, data, 0, 0);
    }
    ID3D11PixelShader* meshPs = s.MeshPsFor(f.surface ? std::string_view(f.surface->code) : std::string_view());
    ID3D11Buffer* frameCb = s.frame.Get();
    ID3D11Buffer* cbs[3] = {frameCb, s.lights.Get(), s.shadows.Get()};
    ctx->VSSetConstantBuffers(0, 1, &frameCb);
    ctx->PSSetConstantBuffers(0, 3, cbs);
    ID3D11ShaderResourceView* noSrvs[8] = {};
    ctx->PSSetShaderResources(0, 8, noSrvs);  // the maps may still be bound for reading: they are written first
    ctx->PSSetShaderResources(8, 1, noSrvs);
    ctx->VSSetShaderResources(8, 1, noSrvs);  // (a plugin's draw may have read the 向きの地図 there)
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
                // (the colour picture decides: フィルタ and 端の扱い are the image's import settings)
                ID3D11SamplerState* sampler = s.samplers[mat.texture ? SamplerIndex(mat.texture->sampling) : 0].Get();
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
    // The sky, behind everything: one triangle over the picture, the depth test off (nothing written).
    if (f.sky && f.sky->program) {
        SkyConstants skyK = f.sky->constants;
        Mat4 turn = f.view;  // the camera's turn only: the sky is infinitely far
        turn.m[3][0] = turn.m[3][1] = turn.m[3][2] = 0;
        if (const auto inv = Inverse(f.projection * turn)) {
            CopyMatrix(skyK.invViewProj, *inv);
            ctx->UpdateSubresource(s.skyFrame.Get(), 0, nullptr, &skyK, 0, 0);
            ID3D11Buffer* skyCb = s.skyFrame.Get();
            ctx->PSSetConstantBuffers(3, 1, &skyCb);
            ID3D11ShaderResourceView* views[kSkyTextures] = {};
            for (size_t k = 0; k < kSkyTextures; ++k) views[k] = f.sky->textures[k] ? f.sky->textures[k]->view.Get() : nullptr;
            ctx->PSSetShaderResources(0, static_cast<UINT>(kSkyTextures), views);
            ID3D11SamplerState* samplers[2] = {s.samplers[0].Get(), s.edgeSampler.Get()};
            ctx->PSSetSamplers(1, 2, samplers);
            ctx->OMSetDepthStencilState(s.depthOff.Get(), 0);
            ctx->VSSetShader(f.sky->program->vs.Get(), nullptr, 0);
            ctx->PSSetShader(f.sky->program->ps.Get(), nullptr, 0);
            ctx->Draw(3, 0);
            ctx->PSSetShaderResources(0, static_cast<UINT>(kSkyTextures), noSrvs);
        }
    }
    ctx->OMSetDepthStencilState(s.depth.Get(), 0);
    if (shadows) {
        ID3D11ShaderResourceView* sunSrv = s.sun.srv.Get();
        ID3D11ShaderResourceView* localSrv = s.local.srv.Get();
        ID3D11SamplerState* sampler = s.shadowSampler.Get();
        ctx->PSSetShaderResources(0, 1, &sunSrv);
        ctx->PSSetShaderResources(5, 1, &localSrv);
        ctx->PSSetSamplers(0, 1, &sampler);
    }
    // 向きの地図 (b6, t8) and a plugin's surface numbers (b5): for the models, and for what plugins draw.
    ID3D11ShaderResourceView* mapsSrv = directionMaps ? s.directionMaps.srv.Get() : nullptr;
    ID3D11Buffer* extraCbs[2] = {s.surfaceCb.Get(), s.directionCb.Get()};
    ctx->PSSetShaderResources(8, 1, &mapsSrv);
    ctx->PSSetConstantBuffers(5, 2, extraCbs);
    ctx->VSSetShader(s.meshVs.Get(), nullptr, 0);
    ctx->PSSetShader(meshPs, nullptr, 0);
    run(opaqueFirst, opaqueEnd, true);
    // What plugins draw as solid things (RenderFrame::solids: 天候's snow), lit as the models are; then
    // ours is set again.
    if (!f.solids.empty()) {
        const ExtraDrawContext ec{ctx, f.view, f.projection, eye, f.width, f.height, nullptr, samples_, s.directionCb.Get(), mapsSrv};
        for (const ExtraDraw& draw : f.solids) {
            if (draw) draw(ec);
        }
        ctx->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        ctx->OMSetBlendState(s.blend.Get(), factor, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(s.depth.Get(), 0);
        ctx->RSSetState(s.raster.Get());
        ctx->VSSetConstantBuffers(0, 1, &frameCb);
        ctx->PSSetConstantBuffers(0, 3, cbs);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->IASetInputLayout(s.meshLayout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        {
            const UINT stride = sizeof(uint32_t) * 2, offset = 0;
            ID3D11Buffer* db = drawsBuffer_.Get();
            ctx->IASetVertexBuffers(1, 1, &db, &stride, &offset);
        }
        ctx->VSSetShaderResources(6, 1, &thingsView);
        ctx->PSSetShaderResources(6, 1, &thingsView);
        ctx->VSSetShader(s.meshVs.Get(), nullptr, 0);
        ctx->PSSetShader(meshPs, nullptr, 0);
        bound = nullptr;
    }
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
            // (depth-tested, but writing none: what reads the depth - particles colliding - sees no grid)
            ctx->OMSetDepthStencilState(s.depthNoWrite.Get(), 0);
            ctx->Draw(static_cast<UINT>(f.lines.size() & ~size_t{1}), 0);
            ctx->OMSetDepthStencilState(s.depth.Get(), 0);
        }
    }

    // 3. What is see-through, over everything else and writing no depth (what is behind another
    // see-through part still shows through it).
    if (seeThroughEnd > floorEnd) {
        ctx->IASetInputLayout(s.meshLayout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->OMSetDepthStencilState(s.depthNoWrite.Get(), 0);
        ctx->VSSetShader(s.meshVs.Get(), nullptr, 0);
        ctx->PSSetShader(meshPs, nullptr, 0);
        run(floorEnd, seeThroughEnd, true);
        ctx->OMSetDepthStencilState(s.depth.Get(), 0);
    }

    // 3-2. What plugins draw themselves (particles: see-through, depth-tested, writing no depth); then
    // ours is set again for the lines.
    if (!f.extras.empty()) {
        ctx->OMSetDepthStencilState(s.depthNoWrite.Get(), 0);
        const ExtraDrawContext ec{ctx, f.view, f.projection, eye, f.width, f.height, depthSrv_.Get(), samples_, s.directionCb.Get(), mapsSrv};
        for (const ExtraDraw& draw : f.extras) {
            if (draw) draw(ec);
        }
        ctx->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        ctx->OMSetBlendState(s.blend.Get(), factor, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(s.depth.Get(), 0);
        ctx->RSSetState(s.raster.Get());
        ctx->VSSetConstantBuffers(0, 1, &frameCb);
        ctx->PSSetConstantBuffers(0, 3, cbs);
        ctx->GSSetShader(nullptr, nullptr, 0);
        bound = nullptr;
    }

    // 4. Debug lines over everything: the parts nothing hides as given, the hidden parts faint (the
    // same lines again, a quarter as strong, where the depth test says something is in front).
    if (const size_t n = f.debugLines.size() & ~size_t{1}; n > 0 && LineCapacity(n * 2)) {
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(lineVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            auto* out = static_cast<LineVertex*>(map.pData);
            std::memcpy(out, f.debugLines.data(), n * sizeof(LineVertex));
            for (size_t k = 0; k < n; ++k) {
                out[n + k] = f.debugLines[k];
                out[n + k].color.a *= 0.25f;
            }
            ctx->Unmap(lineVb_.Get(), 0);
            ctx->IASetInputLayout(s.lineLayout.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            const UINT stride = sizeof(LineVertex), offset = 0;
            ID3D11Buffer* vb = lineVb_.Get();
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            bound = nullptr;
            ctx->VSSetShader(s.lineVs.Get(), nullptr, 0);
            ctx->PSSetShader(s.linePs.Get(), nullptr, 0);
            ctx->OMSetDepthStencilState(s.depthNoWrite.Get(), 0);
            ctx->Draw(static_cast<UINT>(n), 0);
            ctx->OMSetDepthStencilState(s.depthBehind.Get(), 0);
            ctx->Draw(static_cast<UINT>(n), static_cast<UINT>(n));
            ctx->OMSetDepthStencilState(s.depth.Get(), 0);
        }
    }

    if (samples_ > 1) ctx->ResolveSubresource(picture_.Get(), 0, multisample_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
    // Nothing of ours stays bound (Direct2D draws the picture next).
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->PSSetShaderResources(0, 8, noSrvs);
    ctx->PSSetShaderResources(8, 1, noSrvs);
    ctx->VSSetShaderResources(6, 1, noSrvs);
    ID3D11Buffer* noCbs[7] = {};
    ctx->PSSetConstantBuffers(0, 7, noCbs);
    ctx->VSSetShaderResources(8, 1, noSrvs);
    ID3D11Buffer* noVb = nullptr;
    const UINT zero = 0;
    ctx->IASetVertexBuffers(1, 1, &noVb, &zero, &zero);
    return picture_.Get();
}

}  // namespace rm::space3d

