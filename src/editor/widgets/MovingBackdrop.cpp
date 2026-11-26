#include "editor/widgets/MovingBackdrop.h"

#include "base/Log.h"
#include "ui/Context.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <future>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

namespace rm::editor::w {

namespace {

using Microsoft::WRL::ComPtr;

// どの絵にも共通の HLSL: 定数、ノイズ、虹の色、全画面の三角形。パックの Field はこの後ろに付く。
constexpr char kCommonHlsl[] = R"(
cbuffer Frame : register(b0) {
    float2 size;        // ウィンドウ (DIP)
    float time;         // 秒
    float floating;     // 1 = パネルのウィンドウ
    float4 look[4];     // パックが入れる数
    float2 fieldTexel;  // 場のテクスチャの 1 ピクセル (UV)
    float pixelScale;   // 1 DIP が何ピクセルか
    float unused;
};
// 0 .. 1 の乱数 (Dave Hoskins の Hash without Sine)。
float Hash21(float2 p) {
    float3 p3 = frac(p.xyx * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}
float2 Hash22(float2 p) {
    float3 p3 = frac(p.xyx * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}
// なめらかなノイズ (勾配ノイズ)。だいたい 0 .. 1。
float Noise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(Hash22(i) * 2.0 - 1.0, f);
    float b = dot(Hash22(i + float2(1, 0)) * 2.0 - 1.0, f - float2(1, 0));
    float c = dot(Hash22(i + float2(0, 1)) * 2.0 - 1.0, f - float2(0, 1));
    float d = dot(Hash22(i + float2(1, 1)) * 2.0 - 1.0, f - float2(1, 1));
    return saturate(0.5 + 0.9 * lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y));
}
// 重ねたノイズ (octaves 段。細かい段ほど弱い)。だいたい 0 .. 1。
float Fbm(float2 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    const float2x2 turn = float2x2(0.80, 0.60, -0.60, 0.80);
    for (int i = 0; i < octaves; ++i) {
        sum += amp * Noise(p);
        norm += amp;
        p = mul(turn, p) * 2.03 + 17.1;
        amp *= 0.5;
    }
    return sum / norm;
}
// 虹の色 (t = 0 .. 1 で一回り。鮮やか)。
float3 Hue(float t) { return saturate(abs(frac(t + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0); }

struct FullOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
FullOut FullVS(uint id : SV_VertexID) {
    FullOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

// パックの絵ごとのピクセルシェーダー 2 つ:
//   FieldPS  粗い場 (パックの Field)
//   UpPS     場を画面の細かさに引き伸ばし、パックの Sharp (くっきりした物: 惑星・山のかげ・格子) を画面の細かさで
//            重ね、細かいざらつき (三角分布のディザ) で暗いグラデーションの縞を消す。
constexpr char kLookMainHlsl[] = R"(
float4 FieldPS(FullOut i) : SV_Target { return float4(max(Field(i.uv * size), 0.0), 1.0); }
Texture2D fieldTex : register(t0);
SamplerState linearClamp : register(s0);
float4 UpPS(FullOut i) : SV_Target {
    float3 c = Sharp(i.uv * size, fieldTex.Sample(linearClamp, i.uv).rgb);
    float n = Hash21(i.pos.xy) + Hash21(i.pos.xy + 71.3) - 1.0;
    return float4(saturate(c + n / 255.0), 1.0);
}
)";

// 粒 (加算で足す)。
constexpr char kSpriteHlsl[] = R"(

// 粒: 1 つに float4 が 3 つ (場所・半径・形 / 色 / 尾・筋)。
cbuffer Sprites : register(b1) { float4 sprite[3 * 1024]; };
struct SpriteOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;                // 粒の真ん中からのずれ (DIP。尾は頭から尾の向きが x)
    nointerpolation float4 color : COLOR0;
    nointerpolation float4 info : TEXCOORD1; // 半径, 形, 筋, 尾の長さ
};
SpriteOut SpriteVS(uint id : SV_VertexID) {
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(-1, 1), float2(1, -1), float2(1, 1)};
    uint n = id / 6;
    float2 k = corners[id % 6];
    float4 a = sprite[n * 3], col = sprite[n * 3 + 1], b = sprite[n * 3 + 2];
    float r = max(a.z, 0.05);
    uint shape = (uint)(a.w + 0.5);
    float2 at;
    SpriteOut o;
    float len = length(b.xy);
    if (shape == 3) {
        float2 dir = len > 1e-3 ? b.xy / len : float2(1, 0);
        float2 nrm = float2(-dir.y, dir.x);
        float along = lerp(-3.0 * r, len + r, k.x * 0.5 + 0.5);
        float across = k.y * 3.0 * r;
        at = a.xy + dir * along + nrm * across;
        o.local = float2(along, across);
    } else {
        float reach = shape == 1 ? r * (3.0 + 10.0 * b.z) : (shape == 2 ? r * 1.2 : r * 3.0);
        reach += 1.0 / pixelScale;  // (小さな粒でも 1 ピクセルの余白)
        at = a.xy + k * reach;
        o.local = k * reach;
    }
    o.pos = float4(at / size * float2(2, -2) + float2(-1, 1), 0, 1);
    o.color = col;
    o.info = float4(r, (float)shape, b.z, len);
    return o;
}
float4 SpritePS(SpriteOut i) : SV_Target {
    float r = i.info.x;
    uint shape = (uint)(i.info.y + 0.5);
    float2 q = i.local;
    float px = 1.0 / pixelScale;  // 1 ピクセル (DIP)
    float light = 0.0;
    if (shape == 0) {
        float d2 = dot(q, q) / (r * r);
        light = exp(-d2 * 1.6) + 0.12 * exp(-d2 * 0.25);
    } else if (shape == 1) {
        float rr = max(r, 0.55 * px);
        float d2 = dot(q, q) / (rr * rr);
        float core = exp(-d2 * 1.4) * (r / rr) * (r / rr);  // 1 ピクセルより小さい星は、明るさを保ったまま暗く
        float halo = 0.10 * exp(-d2 * 0.12);
        float s = i.info.z;
        float reach = r * 10.0 * s + 1e-3;
        float spike = exp(-abs(q.y) / (0.45 * px + 0.08 * r)) * saturate(1.0 - abs(q.x) / reach) +
                      exp(-abs(q.x) / (0.45 * px + 0.08 * r)) * saturate(1.0 - abs(q.y) / reach);
        light = core + halo * s + 0.55 * s * spike * spike;
    } else if (shape == 2) {
        float d = length(q) / r;
        float edge = px / r * 1.2;
        float inside = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, d);
        float rim = smoothstep(0.70, 0.98, d) * inside;
        float glint = exp(-dot(q / r - float2(-0.38, -0.40), q / r - float2(-0.38, -0.40)) * 40.0);
        float glint2 = exp(-dot(q / r - float2(0.42, 0.45), q / r - float2(0.42, 0.45)) * 90.0);
        light = inside * 0.07 + rim * 0.75 + 1.2 * glint + 0.35 * glint2;
    } else {
        float len = max(i.info.w, 1e-3);
        float x = q.x, y = q.y;
        float across = exp(-y * y / (r * r) * 1.2);
        float along = x < 0.0 ? exp(-x * x / (r * r) * 1.2) : pow(saturate(1.0 - x / len), 2.2);
        float head = exp(-(x * x + y * y) / (r * r) * 0.8);
        light = across * along + 0.6 * head;
    }
    return float4(i.color.rgb * (i.color.a * light), 0.0);
}
)";

bool Compile(const std::string& text, const char* name, const char* entry, const char* target, ComPtr<ID3DBlob>& out, std::string& error) {
    ComPtr<ID3DBlob> messages;
    const HRESULT hr = D3DCompile(text.data(), text.size(), name, nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &messages);
    if (SUCCEEDED(hr)) return true;
    error = messages ? std::string(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize())
                     : std::format("0x{:08X}", static_cast<uint32_t>(hr));
    while (!error.empty() && (error.back() == '\n' || error.back() == '\r' || error.back() == '\0')) error.pop_back();
    return false;
}

struct FrameConstants {
    float size[2];
    float time;
    float floating;
    float look[16];
    float fieldTexel[2];
    float pixelScale;
    float unused;
};
static_assert(sizeof(FrameConstants) == 96, "HLSL の Frame と合わせる");
constexpr UINT kSpritesPerDraw = 1024;
constexpr UINT kSpriteFloats = 12;  // float4 が 3 つ

// HLSL を GPU の言葉 (バイトコード) にする仕事。重い (絵によっては 1 秒ほど) ので、画面を止めないように別のスレッドで
// する。出来るまでは絵を描かない (テーマの色だけ。次のフレームでまた見る)。バイトコードは GPU のデバイスによらないので、
// デバイスが作り直されても作り直さない。
struct Compiled {
    std::vector<ComPtr<ID3DBlob>> code;  // entries と同じ順
    std::string error;                   // (空 = 出来た)
};
struct Entry {
    const char* entry;
    const char* target;
};

// `key` の HLSL の仕事 (無ければ始める)。出来ていなければ null。
const Compiled* CompiledFor(const void* key, const char* name, std::string text, std::vector<Entry> entries) {
    static std::vector<std::pair<const void*, std::shared_future<Compiled>>> jobs;
    auto it = std::find_if(jobs.begin(), jobs.end(), [&](const auto& j) { return j.first == key; });
    if (it == jobs.end()) {
        std::shared_future<Compiled> job = std::async(std::launch::async, [name, text = std::move(text), entries = std::move(entries)] {
                                               Compiled c;
                                               for (const Entry& e : entries) {
                                                   ComPtr<ID3DBlob> code;
                                                   if (!Compile(text, name, e.entry, e.target, code, c.error)) {
                                                       c.code.clear();
                                                       break;
                                                   }
                                                   c.code.push_back(code);
                                               }
                                               return c;
                                           }).share();
        it = jobs.insert(jobs.end(), {key, job});
    }
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    return &it->second.get();
}

// どの絵にも共通の GPU の物 (デバイスごとに 1 つ)。
struct Shared {
    ID3D11Device* device = nullptr;  // (作ったデバイス。比べるだけ)
    ComPtr<ID3D11Device> hold;
    bool failed = false;
    ComPtr<ID3D11VertexShader> fullVs, spriteVs;
    ComPtr<ID3D11PixelShader> spritePs;
    ComPtr<ID3D11Buffer> frame, sprites;
    ComPtr<ID3D11SamplerState> smooth;
    ComPtr<ID3D11BlendState> add;
    ComPtr<ID3D11RasterizerState> raster;
};

// 共通の GPU の物。まだ用意できない (シェーダーを作っている途中・作れなかった) ときは null。
Shared* SharedFor(ID3D11Device* device) {
    static Shared s;
    if (s.device == device && (s.failed || s.fullVs)) return s.failed ? nullptr : &s;
    const Compiled* compiled = CompiledFor(&s, "moving_backdrop", std::string(kCommonHlsl) + kSpriteHlsl,
                                           {{"FullVS", "vs_5_0"}, {"SpriteVS", "vs_5_0"}, {"SpritePS", "ps_5_0"}});
    if (!compiled) return nullptr;
    s = Shared{};
    s.device = device;
    s.hold = device;
    if (!compiled->error.empty()) {
        log::Warn("動くテーマの絵のシェーダーを作れません: {}", compiled->error);
        s.failed = true;
        return nullptr;
    }
    const auto& code = compiled->code;
    D3D11_BUFFER_DESC frame{sizeof(FrameConstants), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    D3D11_BUFFER_DESC sprites{kSpritesPerDraw * kSpriteFloats * 4, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    D3D11_SAMPLER_DESC smooth{};
    smooth.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smooth.AddressU = smooth.AddressV = smooth.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smooth.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC add{};
    add.RenderTarget[0].BlendEnable = TRUE;
    add.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    add.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    add.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    add.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    add.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    add.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    add.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    if (FAILED(device->CreateVertexShader(code[0]->GetBufferPointer(), code[0]->GetBufferSize(), nullptr, &s.fullVs)) ||
        FAILED(device->CreateVertexShader(code[1]->GetBufferPointer(), code[1]->GetBufferSize(), nullptr, &s.spriteVs)) ||
        FAILED(device->CreatePixelShader(code[2]->GetBufferPointer(), code[2]->GetBufferSize(), nullptr, &s.spritePs)) ||
        FAILED(device->CreateBuffer(&frame, nullptr, &s.frame)) || FAILED(device->CreateBuffer(&sprites, nullptr, &s.sprites)) ||
        FAILED(device->CreateSamplerState(&smooth, &s.smooth)) || FAILED(device->CreateBlendState(&add, &s.add)) ||
        FAILED(device->CreateRasterizerState(&raster, &s.raster))) {
        log::Warn("動くテーマの絵の GPU の準備ができません");
        s.failed = true;
        return nullptr;
    }
    return &s;
}

// パックの絵ごとのシェーダー (デバイスごと)。
struct LookShaders {
    const MovingLook* look = nullptr;
    ID3D11Device* device = nullptr;
    ComPtr<ID3D11PixelShader> field, up;  // null: 作れなかった (何度も試さない)
};

// パックの絵のシェーダー。まだ出来ていない・作れなかったときは null。
const LookShaders* ShadersFor(ID3D11Device* device, const MovingLook& look) {
    static std::vector<LookShaders> made;
    std::erase_if(made, [&](const LookShaders& f) { return f.device != device; });  // (古いデバイスの物は捨てる)
    for (const LookShaders& f : made) {
        if (f.look == &look) return f.field ? &f : nullptr;
    }
    const Compiled* compiled =
        CompiledFor(&look, look.name, std::string(kCommonHlsl) + look.hlsl + kLookMainHlsl, {{"FieldPS", "ps_5_0"}, {"UpPS", "ps_5_0"}});
    if (!compiled) return nullptr;
    LookShaders f{&look, device, nullptr, nullptr};
    if (!compiled->error.empty()) {
        log::Warn("動くテーマの絵 ({}) のシェーダーを作れません: {}", look.name, compiled->error);
    } else if (FAILED(device->CreatePixelShader(compiled->code[0]->GetBufferPointer(), compiled->code[0]->GetBufferSize(), nullptr, &f.field)) ||
               FAILED(device->CreatePixelShader(compiled->code[1]->GetBufferPointer(), compiled->code[1]->GetBufferSize(), nullptr, &f.up))) {
        f.field.Reset(), f.up.Reset();
    }
    made.push_back(f);
    return made.back().field ? &made.back() : nullptr;
}

// 1 つのウィンドウの絵 (描く先のテクスチャと、それを包む Direct2D のビットマップ)。
struct Target {
    const void* key = nullptr;
    const MovingLook* look = nullptr;
    ID2D1DeviceContext* dc = nullptr;  // (比べるだけ。持たない: ウィンドウのスワップチェーンを生かしてしまう)
    ID3D11Device* device = nullptr;
    UINT width = 0, height = 0, fieldWidth = 0, fieldHeight = 0;
    ComPtr<ID3D11Texture2D> field, picture;
    ComPtr<ID3D11RenderTargetView> fieldTarget, pictureTarget;
    ComPtr<ID3D11ShaderResourceView> fieldView;
    ComPtr<ID2D1Bitmap1> bitmap;
    ComPtr<ID2D1BitmapBrush> brush;
    double time = -1;
    bool floating = false;
    int64_t usedMs = 0;  // 最後に使った時 (しばらく使わない絵は捨てる)
};

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<Target>& Targets() {
    static std::vector<Target> targets;
    return targets;
}

ComPtr<ID3D11Device> DeviceOf(ID2D1DeviceContext* dc) {
    ComPtr<ID2D1Image> image;
    dc->GetTarget(&image);
    ComPtr<ID2D1Bitmap1> bitmap;
    ComPtr<IDXGISurface> surface;
    ComPtr<ID3D11Device> device;
    if (!image || FAILED(image.As(&bitmap)) || FAILED(bitmap->GetSurface(&surface)) || !surface ||
        FAILED(surface->GetDevice(IID_PPV_ARGS(&device)))) {
        return nullptr;
    }
    return device;
}

bool MakeTexture(ID3D11Device* device, UINT w, UINT h, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& tex, ComPtr<ID3D11RenderTargetView>& rtv) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(device->CreateTexture2D(&d, nullptr, &tex)) && SUCCEEDED(device->CreateRenderTargetView(tex.Get(), nullptr, &rtv));
}

// t の大きさ・デバイス・描く先に合った GPU の物を用意する。
bool Prepare(Target& t, ID2D1DeviceContext* dc, ID3D11Device* device, UINT w, UINT h, float fieldScale, float dpi) {
    const UINT fw = std::max(1u, static_cast<UINT>(std::ceil(static_cast<float>(w) * fieldScale)));
    const UINT fh = std::max(1u, static_cast<UINT>(std::ceil(static_cast<float>(h) * fieldScale)));
    if (t.bitmap && t.dc == dc && t.device == device && t.width == w && t.height == h && t.fieldWidth == fw && t.fieldHeight == fh) return true;
    t.field.Reset(), t.picture.Reset(), t.fieldTarget.Reset(), t.pictureTarget.Reset(), t.fieldView.Reset(), t.bitmap.Reset(), t.brush.Reset();
    t.dc = dc, t.device = device, t.width = w, t.height = h, t.fieldWidth = fw, t.fieldHeight = fh;
    t.time = -1;
    if (!MakeTexture(device, fw, fh, DXGI_FORMAT_R16G16B16A16_FLOAT, t.field, t.fieldTarget) ||
        FAILED(device->CreateShaderResourceView(t.field.Get(), nullptr, &t.fieldView)) ||
        !MakeTexture(device, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, t.picture, t.pictureTarget)) {
        return false;
    }
    ComPtr<IDXGISurface> surface;
    const D2D1_BITMAP_PROPERTIES1 props =
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    if (FAILED(t.picture.As(&surface)) || FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &t.bitmap))) {
        t.bitmap.Reset();
        return false;
    }
    const D2D1_BITMAP_BRUSH_PROPERTIES bp = D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    if (FAILED(dc->CreateBitmapBrush(t.bitmap.Get(), bp, &t.brush))) {
        t.bitmap.Reset();
        return false;
    }
    return true;
}

void Draw(ID3D11DeviceContext* ctx, Shared& s, const LookShaders& look, Target& t, const BackdropScene& scene, Vec2 sizeDip, double time, bool floating) {
    FrameConstants k{};
    k.size[0] = sizeDip.x, k.size[1] = sizeDip.y;
    k.time = static_cast<float>(time);
    k.floating = floating ? 1.f : 0.f;
    std::memcpy(k.look, scene.look, sizeof k.look);
    k.fieldTexel[0] = 1.f / static_cast<float>(t.fieldWidth), k.fieldTexel[1] = 1.f / static_cast<float>(t.fieldHeight);
    k.pixelScale = static_cast<float>(t.width) / std::max(sizeDip.x, 1.f);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(s.frame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    std::memcpy(m.pData, &k, sizeof k);
    ctx->Unmap(s.frame.Get(), 0);

    // ほかの描き手 (3D のシーン、パーティクル) が残した物に頼らない: 使う物は全部ここで決める。
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->RSSetState(s.raster.Get());
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ID3D11Buffer* frame = s.frame.Get();
    ctx->VSSetConstantBuffers(0, 1, &frame);
    ctx->PSSetConstantBuffers(0, 1, &frame);

    // 1. 場 (粗く)
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(t.fieldWidth), static_cast<float>(t.fieldHeight), 0, 1};
    ID3D11RenderTargetView* rtv = t.fieldTarget.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->VSSetShader(s.fullVs.Get(), nullptr, 0);
    ctx->PSSetShader(look.field.Get(), nullptr, 0);
    ctx->Draw(3, 0);

    // 2. 引き伸ばす
    vp.Width = static_cast<float>(t.width), vp.Height = static_cast<float>(t.height);
    rtv = t.pictureTarget.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ID3D11ShaderResourceView* srv = t.fieldView.Get();
    ID3D11SamplerState* smooth = s.smooth.Get();
    ctx->PSSetShader(look.up.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &smooth);
    ctx->Draw(3, 0);

    // 3. 粒を足す
    if (!scene.sprites.empty()) {
        ctx->OMSetBlendState(s.add.Get(), nullptr, 0xFFFFFFFF);
        ctx->VSSetShader(s.spriteVs.Get(), nullptr, 0);
        ctx->PSSetShader(s.spritePs.Get(), nullptr, 0);
        ID3D11Buffer* sprites = s.sprites.Get();
        ctx->VSSetConstantBuffers(1, 1, &sprites);
        ctx->PSSetConstantBuffers(1, 1, &sprites);
        for (size_t first = 0; first < scene.sprites.size(); first += kSpritesPerDraw) {
            const size_t count = std::min<size_t>(kSpritesPerDraw, scene.sprites.size() - first);
            if (FAILED(ctx->Map(s.sprites.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) break;
            float* out = static_cast<float*>(m.pData);
            for (size_t i = 0; i < count; ++i, out += kSpriteFloats) {
                const BackdropSprite& p = scene.sprites[first + i];
                const float v[kSpriteFloats] = {p.at.x, p.at.y, p.radius, static_cast<float>(p.shape), p.color.r, p.color.g, p.color.b, p.color.a,
                                                p.tail.x, p.tail.y, p.spikes, 0.f};
                std::memcpy(out, v, sizeof v);
            }
            ctx->Unmap(s.sprites.Get(), 0);
            ctx->Draw(static_cast<UINT>(count * 6), 0);
        }
    }

    // 描いたテクスチャを Direct2D が読めるように、つなぎを外す。
    ID3D11ShaderResourceView* noSrv = nullptr;
    ctx->PSSetShaderResources(0, 1, &noSrv);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}

// 動く絵の時刻 (最初に描いた時から。float のシェーダーでも細かさが残るように、小さな数にしておく)。
double LookTime(double now) {
    static double start = -1;
    if (start < 0) start = now;
    return now - start;
}

}  // namespace

ID2D1Bitmap1* RenderMovingLook(ID2D1DeviceContext* dc, const void* key, const MovingLook& look, Vec2 sizeDip, double time, bool floating) {
    if (!dc || sizeDip.x < 1 || sizeDip.y < 1) return nullptr;
    float dpi = 96, dpiY = 96;
    dc->GetDpi(&dpi, &dpiY);
    const UINT w = static_cast<UINT>(std::lround(sizeDip.x * dpi / 96.f)), h = static_cast<UINT>(std::lround(sizeDip.y * dpi / 96.f));
    if (w == 0 || h == 0 || w > 16384 || h > 16384) return nullptr;
    std::vector<Target>& targets = Targets();
    const int64_t now = NowMs();
    auto mine = [&](const Target& e) { return e.key == key && e.look == &look; };
    // しばらく使っていない絵 (閉じたウィンドウ、前のテーマ) は捨てる。
    std::erase_if(targets, [&](const Target& e) { return !mine(e) && now - e.usedMs > 3000; });
    auto it = std::find_if(targets.begin(), targets.end(), mine);
    if (it == targets.end()) {
        it = targets.insert(targets.end(), Target{});
        it->key = key;
        it->look = &look;
    }
    Target* t = &*it;
    if (t->bitmap && t->dc == dc && t->time == time && t->floating == floating && t->width == w && t->height == h) {
        t->usedMs = now;
        return t->bitmap.Get();
    }
    t->usedMs = now;

    const ComPtr<ID3D11Device> device = DeviceOf(dc);
    if (!device) return nullptr;
    Shared* s = SharedFor(device.Get());
    const LookShaders* shaders = ShadersFor(device.Get(), look);  // (どちらも、出来るまでは描かない)
    if (!s || !shaders || !Prepare(*t, dc, device.Get(), w, h, std::clamp(look.fieldScale, 0.1f, 1.f), dpi)) return nullptr;

    BackdropScene scene;
    if (look.fill) look.fill(scene, {0, 0, sizeDip.x, sizeDip.y}, time, floating);
    ComPtr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(&ctx);
    dc->Flush();  // ここまでの Direct2D の描画を先に GPU へ
    Draw(ctx.Get(), *s, *shaders, *t, scene, sizeDip, time, floating);
    t->time = time;
    t->floating = floating;
    return t->bitmap.Get();
}

void AddMovingPicture(ID2D1DeviceContext* dc, ID2D1Bitmap1* picture, const ui::Rect& r, float corner, float strength) {
    if (!dc || !picture || r.w <= 0 || r.h <= 0 || strength <= 0) return;
    ID2D1BitmapBrush* brush = nullptr;
    for (const Target& t : Targets()) {
        if (t.bitmap.Get() == picture) brush = t.brush.Get();
    }
    if (!brush) return;
    brush->SetOpacity(std::min(strength, 1.f));
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);  // 光を地の色に足す
    const D2D1_RECT_F rect = D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
    if (corner > 0) {
        dc->FillRoundedRectangle(D2D1::RoundedRect(rect, corner, corner), brush);
    } else {
        dc->FillRectangle(rect, brush);
    }
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

double MovingTime(const ui::Context& c) { return LookTime(c.Time()); }

void DrawMovingBackdrop(ui::Context& c, const MovingLook& look, bool floating, float strength) {
    const ui::Rect& v = c.Viewport();
    AddMovingPicture(c.P().DC(), RenderMovingLook(c.P().DC(), &c, look, {v.w, v.h}, LookTime(c.Time()), floating), v, 0, strength);
    c.RequestFrameAfter(kFrameSeconds);
}

void DrawMovingBackdropOn(ui::Context& c, const MovingLook& look, const ui::Rect& r, float corner, bool floating, float strength) {
    const ui::Rect& v = c.Viewport();
    AddMovingPicture(c.P().DC(), RenderMovingLook(c.P().DC(), &c, look, {v.w, v.h}, LookTime(c.Time()), floating), r, corner, strength);
    c.RequestFrameAfter(kFrameSeconds);
}

}  // namespace rm::editor::w
