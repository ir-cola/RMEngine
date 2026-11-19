#include "rm_engine/space3d/runtime/SkyShader.h"

#include <format>

namespace rm::space3d {

const char kColorHlsl[] = R"(
// The screen's numbers (sRGB) <-> linear light, per component.
float3 ToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow(abs((c + 0.055) / 1.055), 2.4); }
float3 ToScreen(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055;
}
)";

namespace {

// What every sky's code can use (before it).
constexpr char kSkyPrelude[] = R"(
cbuffer SkyFrame : register(b3) {
    float4x4 skyInvViewProj;  // clip space -> a direction in the world (the camera's turn only)
    float4 skySun;            // xyz: towards the sun; w: seconds
    float4 skyMoon;           // xyz: towards the moon; w: the hour 0..24
    float4 skyInfo;           // x: brightness
    float4 skyParam[8];       // the sky's own numbers (SkyShader.h)
};
Texture2D sky0 : register(t0);
Texture2D sky1 : register(t1);
Texture2D sky2 : register(t2);
Texture2D sky3 : register(t3);
Texture2D sky4 : register(t4);
Texture2D sky5 : register(t5);
SamplerState skySmooth : register(s1);  // smooth, repeating (a panorama goes round)
SamplerState skyEdge : register(s2);    // smooth, stopping at the edges (one of six pictures)

static const float PI = 3.14159265;

struct SkyInput {
    float3 dir;    // where this pixel looks (the world's: length 1, +Y up, -Z in front)
    float2 uv;     // where it is on the screen (0, 0 top left .. 1, 1 bottom right)
    float time;    // seconds (they go on while the game plays)
    float3 sun;    // towards the sun (length 1)
    float3 moon;   // towards the moon
    float hour;    // 0 .. 24 (地球の空's 時刻)
    float3 value;  // the inspector's 値 (自分のシェーダー)
    float4 color;  // the inspector's 色 (linear)
};

// 0 .. 1, the same for the same point (no pattern to see).
float SkyHash(float3 p) {
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
// Smooth noise 0 .. 1 (value noise: SkyHash at the corners of a grid, blended).
float SkyNoise(float3 x) {
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3 - 2 * f);
    return lerp(lerp(lerp(SkyHash(i), SkyHash(i + float3(1, 0, 0)), f.x), lerp(SkyHash(i + float3(0, 1, 0)), SkyHash(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(SkyHash(i + float3(0, 0, 1)), SkyHash(i + float3(1, 0, 1)), f.x), lerp(SkyHash(i + float3(0, 1, 1)), SkyHash(i + float3(1, 1, 1)), f.x), f.y),
                f.z);
}
// Noise of several sizes added (clouds, nebulae): about 0 .. 1. octaves: 1 .. 8.
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
// A direction turned about the up axis (radians: + turns it to the left, seen from above).
float3 RotateY(float3 d, float a) {
    float c = cos(a), s = sin(a);
    return float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}
// A panorama (equirectangular: 360 x 180 degrees): its middle in front (-Z), its top straight up.
float3 SamplePanorama(Texture2D t, float3 d) {
    float2 uv = float2(0.5 + atan2(d.x, -d.z) / (2 * PI), acos(clamp(d.y, -1, 1)) / PI);
    float2 dx = ddx(uv), dy = ddy(uv);
    dx.x -= round(dx.x);  // across the seam behind: not a jump of the whole picture
    dy.x -= round(dy.x);
    return t.SampleGrad(skySmooth, uv, dx, dy).rgb;
}
// Six pictures, one for each side of a cube around the eye, as seen from inside (sky0 .. sky5: +X
// right, -X left, +Y up, -Y down, +Z behind, -Z in front; the up / down ones with the front at their
// bottom / top edge).
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

// The engine's part after the sky's code: one triangle over the whole picture, behind everything.
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

// 地球の空: the air's colours (worked out on the CPU from the hour and the weather: game_x_3d's Sky),
// the sun and its glow, the moon and the stars at night, clouds that drift, haze, lightning.
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
    // The air: the horizon's colour low, the zenith's high; the sunset's glow on the sun's side.
    float3 c = lerp(horizon, zenith, pow(up, 0.45));
    float toSun = dot(d, i.sun);
    c += sunLight * glow * pow(saturate(toSun * 0.5 + 0.5), 5) * exp(-up * 3.5) * 0.7;
    float clear = 1 - cover * 0.9;
    // The stars at night (each in its own cell of a grid around the eye; they twinkle).
    if (stars > 0.001) {
        float3 q = d * 260;
        float3 cell = floor(q);
        float h = SkyHash(cell);
        float3 at = cell + 0.5 + (float3(SkyHash(cell + 7.1), SkyHash(cell + 3.7), SkyHash(cell + 1.3)) - 0.5) * 0.7;
        float star = h > 0.993 ? saturate(1 - length(q - at) * 2.2) : 0;
        float twinkle = 0.7 + 0.3 * sin(i.time * (2 + h * 6) + h * 40);
        c += star * star * twinkle * stars * smoothstep(0, 0.12, d.y) * 1.6 * clear;
    }
    // The moon (full) and the light around it.
    float toMoon = dot(d, i.moon);
    float disk = smoothstep(0.99985, 0.99992, toMoon);
    c += (disk * float3(0.9, 0.92, 1.0) * (0.55 + 0.45 * SkyNoise(d * 900)) + float3(0.5, 0.6, 0.8) * pow(saturate(toMoon), 300) * 0.12) * moonLight * clear *
         smoothstep(-0.02, 0.02, d.y);
    // The sun: its disk and the light around it.
    float sunDisk = smoothstep(0.99990, 0.99996, toSun);
    c += sunLight * (sunDisk * 40 + pow(saturate(toSun), 600) * 1.5 + pow(saturate(toSun), 12) * 0.1) * (1 - cover * 0.85) * smoothstep(-0.02, 0.02, d.y);
    // Clouds: a layer high above, seen at a slant (thinner towards the horizon), drifting.
    if (d.y > 0 && cover > 0.001) {
        float2 p = d.xz / (d.y + 0.08) * 0.8 - skyParam[5].xy * 0.003;  // carried by the wind
        float n = SkyFbm(float3(p, i.time * 0.004), 6);
        float edge = lerp(0.78, 0.22, cover);
        float density = smoothstep(edge, edge + 0.18, n) * smoothstep(0.0, 0.1, d.y);
        float through = SkyFbm(float3(p + i.sun.xz * 0.06, i.time * 0.004), 4);  // nearer the sun: thinner
        float3 lit = sunLight * 0.85 + zenith * 0.7 + horizon * 0.35;
        float3 shade = (zenith + horizon) * 0.3;
        float3 cloud = lerp(lit, shade, saturate(dark + (through - n) * 1.5 + 0.25)) * (1 - dark * 0.6);
        cloud += flash * float3(0.8, 0.85, 1.0) * 2.5;  // lightning (when it flashes: the CPU says)
        c = lerp(c, cloud, density);
    }
    c += flash * float3(0.25, 0.27, 0.32) * up;  // the flash lights the whole sky a little
    // Haze: all towards the horizon's colour, most of all low (rain and snow).
    c = lerp(c, horizon, saturate(haze * (1 - up * 0.6)));
    // Below the horizon: the ground's colour.
    return lerp(c, ground, saturate(-d.y * 6));
}
)";

// 宇宙: the Milky Way across the sky, nebulae in two colours, stars of two sizes and colours, a sun.
constexpr char kSpace[] = R"(
float3 Sky(SkyInput i) {
    float3 d = i.dir;
    float amount = skyParam[0].x, nebula = skyParam[0].y, galaxy = skyParam[0].z, showSun = skyParam[0].w;
    float3 c = float3(0.0015, 0.0018, 0.003);
    // The Milky Way: a band across the sky, with dark dust in it.
    float band = exp(-pow(dot(d, normalize(float3(0.35, 1, 0.25))) / 0.2, 2));
    float dust = SkyFbm(d * 6, 5);
    c += galaxy * band * (0.4 + 0.6 * SkyFbm(d * 3 + 11, 4)) * smoothstep(0.3, 0.7, dust + 0.15) * float3(0.10, 0.09, 0.08);
    // Nebulae.
    float m = SkyFbm(d * 2.2 + 3, 6);
    float k = SkyFbm(d * 3.5 + 21, 5);
    c += nebula * (skyParam[1].rgb * pow(saturate(m * 1.8 - 0.65), 2) + skyParam[2].rgb * pow(saturate(k * 1.9 - 0.8), 2)) * 0.8;
    // Stars: many faint ones and a few bright ones (more in the band), each a little coloured.
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
    // The sun: a white disk and its glow.
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
        if (ch == '"' || ch == '\\' || ch == '\n' || ch == '\r') ch = '_';  // (inside the #line's quotes)
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
