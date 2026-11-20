#include "rm_engine/space3d/runtime/SkyShader.h"

#include <format>

namespace rm::space3d {

const char kColorHlsl[] = R"(
// 画面の数値 (sRGB) <-> 線形の光。成分ごと。
float3 ToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4); }
float3 ToScreen(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055;
}
)";

namespace {

// どの空のコードでも使えるもの (その前に置く)。
constexpr char kSkyPrelude[] = R"(
cbuffer SkyFrame : register(b3) {
    float4x4 skyInvViewProj;  // クリップ空間 -> ワールドでの向き (カメラの回転だけ)
    float4 skySun;            // xyz: 太陽の向き、w: 秒
    float4 skyMoon;           // xyz: 月の向き、w: 時刻 0..24
    float4 skyInfo;           // x: 明るさ
    float4 skyParam[8];       // 空自身の数値 (SkyShader.h)
};
Texture2D sky0 : register(t0);
Texture2D sky1 : register(t1);
Texture2D sky2 : register(t2);
Texture2D sky3 : register(t3);
Texture2D sky4 : register(t4);
Texture2D sky5 : register(t5);
SamplerState skySmooth : register(s1);  // なめらか、繰り返す (パノラマは 1 周する)
SamplerState skyEdge : register(s2);    // なめらか、端で止まる (6 枚のうちの 1 枚)

static const float PI = 3.14159265;

struct SkyInput {
    float3 dir;    // このピクセルが見ている向き (ワールドの: 長さ 1、+Y が上、-Z が前)
    float2 uv;     // 画面上の位置 (0, 0 が左上 .. 1, 1 が右下)
    float time;    // 秒 (ゲームが動いている間は進み続ける)
    float3 sun;    // 太陽の向き (長さ 1)
    float3 moon;   // 月の向き
    float hour;    // 0 .. 24 (地球の空's 時刻)
    float3 value;  // the inspector's 値 (自分のシェーダー)
    float4 color;  // the inspector's 色 (linear)
};

// 0 .. 1。同じ点なら同じ値 (模様は見えない)。
float SkyHash(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
// なめらかなノイズ 0 .. 1 (バリューノイズ: 格子の角で SkyHash を取り、混ぜる)。
float SkyNoise(float3 x) {
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3 - 2 * f);
    return lerp(lerp(lerp(SkyHash(i), SkyHash(i + float3(1, 0, 0)), f.x), lerp(SkyHash(i + float3(0, 1, 0)), SkyHash(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(SkyHash(i + float3(0, 0, 1)), SkyHash(i + float3(1, 0, 1)), f.x), lerp(SkyHash(i + float3(0, 1, 1)), SkyHash(i + float3(1, 1, 1)), f.x), f.y),
                f.z);
}
// 大きさの違うノイズを足したもの (雲、星雲): おおよそ 0 .. 1。octaves: 1 .. 8。
float SkyFbm(float3 p, int octaves) {
    float sum = 0, amp = 0.5, total = 0;
    [loop] for (int k = 0; k < octaves && k < 8; ++k) {
        sum += amp * SkyNoise(p);
        total += amp;
        p = p * 2.03 + 17.1;
        amp *= 0.5;
    }
    return sum / max(total, 1e-4);
}
// 上向きの軸まわりに回した向き (ラジアン: + は上から見て左へ回す)。
float3 RotateY(float3 d, float a) {
    float c = cos(a), s = sin(a);
    return float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}
// パノラマ (正距円筒: 360 x 180 度): 真ん中が正面 (-Z)、一番上が真上。
float3 SamplePanorama(Texture2D t, float3 d) {
    float2 uv = float2(0.5 + atan2(d.x, -d.z) / (2 * PI), acos(clamp(d.y, -1, 1)) / PI);
    float2 dx = ddx(uv), dy = ddy(uv);
    dx.x -= round(dx.x);  // 後ろの継ぎ目をまたぐ: 絵全体が飛ばないように
    dy.x -= round(dy.x);
    return t.SampleGrad(skySmooth, uv, dx, dy).rgb;
}
// 6 枚の絵。目のまわりの立方体の各面に 1 枚ずつで、内側から見たもの (sky0 .. sky5: +X 右、-X 左、
// +Y 上、-Y 下、+Z 後ろ、-Z 前。上 / 下の絵は、前が下 / 上の縁に来る)。
float3 SampleSixFaces(float3 d) {
    float3 a = abs(d);
    float2 st;
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0 ? 0 : 1;
        st = float2(d.x > 0 ? d.z : -d.z, -d.y) / a.x;
    } else if (a.y >= a.z) {
        face = d.y > 0 ? 2 : 3;
        st = float2(d.x, d.y > 0 ? -d.z : d.z) / a.y;
    } else {
        face = d.z > 0 ? 4 : 5;
        st = float2(d.z > 0 ? -d.x : d.x, -d.y) / a.z;
    }
    float2 uv = st * 0.5 + 0.5;
    if (face == 0) return sky0.SampleLevel(skyEdge, uv, 0).rgb;
    if (face == 1) return sky1.SampleLevel(skyEdge, uv, 0).rgb;
    if (face == 2) return sky2.SampleLevel(skyEdge, uv, 0).rgb;
    if (face == 3) return sky3.SampleLevel(skyEdge, uv, 0).rgb;
    if (face == 4) return sky4.SampleLevel(skyEdge, uv, 0).rgb;
    return sky5.SampleLevel(skyEdge, uv, 0).rgb;
}
)";

// 空のコードの後に付くエンジンの部分: 絵全体を覆う三角形 1 つ。全部の奥に描く。
constexpr char kSkyMain[] = R"(
#line 1 "sky"
struct SkyOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
SkyOut SkyVS(uint id : SV_VertexID) {
    SkyOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * float2(2, -2) + float2(-1, 1);
    o.pos = float4(o.ndc, 1, 1);
    return o;
}
float4 SkyPS(SkyOut o) : SV_Target {
    float4 nearPoint = mul(skyInvViewProj, float4(o.ndc, 0, 1));
    float4 farPoint = mul(skyInvViewProj, float4(o.ndc, 1, 1));
    SkyInput i;
    i.dir = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    i.uv = o.ndc * float2(0.5, -0.5) + 0.5;
    i.time = skySun.w;
    i.sun = skySun.xyz;
    i.moon = skyMoon.xyz;
    i.hour = skyMoon.w;
    i.value = skyParam[0].xyz;
    i.color = skyParam[1];
    float3 c = Sky(i) * skyInfo.x;
    return float4(ToScreen(c), 1);
}
)";

// 地球の空: 空気の色 (CPU が時刻と天気から計算する: game_x_3d の Sky)、太陽とそのにじみ、
// 夜の月と星、流れる雲、もや、雷。
constexpr char kEarth[] = R"(
float3 Sky(SkyInput i) {
    float3 d = i.dir;
    float3 sunLight = skyParam[0].rgb;
    float cover = skyParam[1].x, dark = skyParam[1].y, haze = skyParam[1].z, flash = skyParam[1].w;
    float3 zenith = skyParam[2].rgb;
    float stars = skyParam[2].a;
    float3 horizon = skyParam[3].rgb;
    float moonLight = skyParam[3].a;
    float3 ground = skyParam[4].rgb;
    float glow = skyParam[4].a;
    float up = max(d.y, 0);
    // 空気: 低い所は地平線の色、高い所は天頂の色。夕焼けのにじみは太陽の側。
    float3 c = lerp(horizon, zenith, pow(up, 0.45));
    float toSun = dot(d, i.sun);
    c += sunLight * glow * pow(saturate(toSun * 0.5 + 0.5), 5) * exp(-up * 3.5) * 0.7;
    float clear = 1 - cover * 0.9;
    // 夜の星 (目のまわりの格子の、それぞれ自分のマスにある。またたく)。
    if (stars > 0.001) {
        float3 q = d * 260;
        float3 cell = floor(q);
        float h = SkyHash(cell);
        float3 at = cell + 0.5 + (float3(SkyHash(cell + 7.1), SkyHash(cell + 3.7), SkyHash(cell + 1.3)) - 0.5) * 0.7;
        float star = h > 0.993 ? saturate(1 - length(q - at) * 2.2) : 0;
        float twinkle = 0.7 + 0.3 * sin(i.time * (2 + h * 6) + h * 40);
        c += star * star * twinkle * stars * smoothstep(0, 0.12, d.y) * 1.6 * clear;
    }
    // 月 (満月) とそのまわりの光。
    float toMoon = dot(d, i.moon);
    float disk = smoothstep(0.99985, 0.99992, toMoon);
    c += (disk * float3(0.9, 0.92, 1.0) * (0.55 + 0.45 * SkyNoise(d * 900)) + float3(0.5, 0.6, 0.8) * pow(saturate(toMoon), 300) * 0.12) * moonLight * clear *
         smoothstep(-0.02, 0.02, d.y);
    // 太陽: その円盤とまわりの光。
    float sunDisk = smoothstep(0.99990, 0.99996, toSun);
    c += sunLight * (sunDisk * 40 + pow(saturate(toSun), 600) * 1.5 + pow(saturate(toSun), 12) * 0.1) * (1 - cover * 0.85) * smoothstep(-0.02, 0.02, d.y);
    // 雲: 高い所の 1 つの層を斜めに見る (地平線に近いほど薄い)。流れる。
    if (d.y > 0 && cover > 0.001) {
        float2 p = d.xz / (d.y + 0.08) * 0.8 - skyParam[5].xy * 0.003;  // 風に運ばれる
        float n = SkyFbm(float3(p, i.time * 0.004), 6);
        float edge = lerp(0.78, 0.22, cover);
        float density = smoothstep(edge, edge + 0.18, n) * smoothstep(0.0, 0.1, d.y);
        float through = SkyFbm(float3(p + i.sun.xz * 0.06, i.time * 0.004), 4);  // 太陽に近いほど薄い
        float3 lit = sunLight * 0.85 + zenith * 0.7 + horizon * 0.35;
        float3 shade = (zenith + horizon) * 0.3;
        float3 cloud = lerp(lit, shade, saturate(dark + (through - n) * 1.5 + 0.25)) * (1 - dark * 0.6);
        cloud += flash * float3(0.8, 0.85, 1.0) * 2.5;  // 雷 (光るとき: CPU が知らせる)
        c = lerp(c, cloud, density);
    }
    c += flash * float3(0.25, 0.27, 0.32) * up;  // 光ると空全体が少し明るくなる
    // もや: 全体が地平線の色へ寄る。低いほど強い (雨と雪)。
    c = lerp(c, horizon, saturate(haze * (1 - up * 0.6)));
    // 地平線より下: 地面の色。
    return lerp(c, ground, saturate(-d.y * 6));
}
)";

// 宇宙: 空を横切る天の川、2 色の星雲、2 種類の大きさと色の星、太陽。
constexpr char kSpace[] = R"(
float3 Sky(SkyInput i) {
    float3 d = i.dir;
    float amount = skyParam[0].x, nebula = skyParam[0].y, galaxy = skyParam[0].z, showSun = skyParam[0].w;
    float3 c = float3(0.0015, 0.0018, 0.003);
    // 天の川: 空を横切る帯。暗い塵が混じる。
    float band = exp(-pow(dot(d, normalize(float3(0.35, 1, 0.25))) / 0.2, 2));
    float dust = SkyFbm(d * 6, 5);
    c += galaxy * band * (0.4 + 0.6 * SkyFbm(d * 3 + 11, 4)) * smoothstep(0.3, 0.7, dust + 0.15) * float3(0.10, 0.09, 0.08);
    // 星雲。
    float m = SkyFbm(d * 2.2 + 3, 6);
    float k = SkyFbm(d * 3.5 + 21, 5);
    c += nebula * (skyParam[1].rgb * pow(saturate(m * 1.8 - 0.65), 2) + skyParam[2].rgb * pow(saturate(k * 1.9 - 0.8), 2)) * 0.8;
    // 星: 暗いものがたくさんと、明るいものが少し (帯の中に多い)。それぞれ少し色が付く。
    [unroll] for (int layer = 0; layer < 2; ++layer) {
        float scale = layer == 0 ? 420 : 140;
        float3 q = d * scale;
        float3 cell = floor(q);
        float h = SkyHash(cell + layer * 17);
        float3 at = cell + 0.5 + (float3(SkyHash(cell + 7.1), SkyHash(cell + 3.7), SkyHash(cell + 1.3)) - 0.5) * 0.7;
        float limit = 1 - (layer == 0 ? 0.05 : 0.008) * amount * (1 + band * 2 * galaxy);
        float s = h > limit ? saturate(1 - length(q - at) * (layer == 0 ? 2.5 : 1.6)) : 0;
        float3 tint = lerp(float3(1.0, 0.75, 0.55), float3(0.65, 0.78, 1.0), SkyHash(cell + 5.5));
        c += s * s * tint * (layer == 0 ? 0.7 : 2.0);
    }
    // 太陽: 白い円盤とそのにじみ。
    if (showSun > 0.5) {
        float t = dot(d, i.sun);
        c += float3(1.0, 0.96, 0.9) * (smoothstep(0.99985, 0.99993, t) * 60 + pow(saturate(t), 2000) * 4 + pow(saturate(t), 60) * 0.05);
    }
    return c;
}
)";

constexpr char kPanorama[] = R"(
float3 Sky(SkyInput i) { return SamplePanorama(sky0, RotateY(i.dir, skyParam[0].x)); }
)";

constexpr char kSixFaces[] = R"(
float3 Sky(SkyInput i) { return SampleSixFaces(RotateY(i.dir, skyParam[0].x)); }
)";

constexpr char kNewSky[] = R"(// 空のシェーダー (RM Engine)
// 使い方: 階層の一番上の行 (シーン) を選ぶ → インスペクターの「空」の種類を「自分のシェーダー」にして、
// このファイルを選びます。保存するとすぐに描き直されます。間違いがあるとコンソールに何行目かが出ます。
//
// Sky(i) は、向き i.dir に見える空の色を返します (光の強さ: 1 で白。もっと明るくてもよい)。
//   i.dir    見ている向き (長さ 1。+Y が上、-Z が前)
//   i.uv     画面の位置 (左上 0,0 〜 右下 1,1)
//   i.time   秒 (Play 中に進む)
//   i.sun    太陽の向き (インスペクターの「太陽の高さ」「太陽の向き」)
//   i.value  インスペクターの「値」(float3)
//   i.color  インスペクターの「色」(float4)
//   sky0     インスペクターの「画像」。SamplePanorama(sky0, i.dir) でパノラマとして読めます
//   関数:    SkyNoise(p)  SkyFbm(p, 回数)  SkyHash(p)  RotateY(向き, 角度)  PI
float3 Sky(SkyInput i) {
    float up = saturate(i.dir.y * 0.5 + 0.5);
    float3 low = float3(0.9, 0.55, 0.35);    // 地平線の色
    float3 high = float3(0.10, 0.18, 0.45);  // 真上の色
    float3 c = lerp(low, high, up);
    c += float3(1, 0.9, 0.7) * pow(saturate(dot(i.dir, i.sun)), 300) * 3;  // 太陽
    return c * i.color.rgb;
}
)";

}  // namespace

std::string AssembleSky(std::string_view code, std::string_view fileName) {
    std::string name(fileName);
    for (char& ch : name) {
        if (ch == '"' || ch == '\\' || ch == '\n' || ch == '\r') ch = '_';  // (#line の引用符の中)
    }
    std::string out = kColorHlsl;
    out += kSkyPrelude;
    out += std::format("#line 1 \"{}\"\n", name);
    out += code;
    out += kSkyMain;
    return out;
}

std::string_view BuiltinSkyCode(BuiltinSky sky) {
    switch (sky) {
        case BuiltinSky::Earth: return kEarth;
        case BuiltinSky::Space: return kSpace;
        case BuiltinSky::Panorama: return kPanorama;
        case BuiltinSky::SixFaces: return kSixFaces;
    }
    return kEarth;
}

const char* BuiltinSkyName(BuiltinSky sky) {
    switch (sky) {
        case BuiltinSky::Earth: return "地球の空";
        case BuiltinSky::Space: return "宇宙";
        case BuiltinSky::Panorama: return "パノラマ";
        case BuiltinSky::SixFaces: return "6 枚の画像";
    }
    return "空";
}

std::string_view NewSkyCode() { return kNewSky; }

}  // namespace rm::space3d
