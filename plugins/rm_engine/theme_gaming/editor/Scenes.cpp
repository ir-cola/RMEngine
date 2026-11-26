#include "rm_engine/theme_gaming/editor/Scenes.h"

#include "rm_engine/theme_gaming/editor/Motion.h"

#include <algorithm>

namespace rm::theme_gaming::scenes {

namespace {

using editor::w::BackdropScene;
using editor::w::SpriteShape;

// ================================================================================================
// ゲーミング: 紫の空に縞のある太陽と、縁がネオンで光る遠い山、手前へ流れるネオンの格子の床 (光がにじむ)、
// 色がゆっくり変わる地平線の照り、ウィンドウの縁をぐるりと流れる虹。粒 = 漂う色とりどりの光。
// ================================================================================================
constexpr char kGamingHlsl[] = R"(
// look[0] = (地平線の y, 太陽と山を出すなら 1, 0, 0)、look[1] = (縁の虹の一回りの長さ, 流れる速さ, 0, 0)。
// 空・太陽の光・地平線の照り (粗い場)。
float3 Field(float2 p) {
    float t = time;
    float horizon = look[0].x;
    float H = size.y;
    float above = saturate((horizon - p.y) / horizon);  // 地平線 0 .. 上の端 1
    // 空: 上は深い紫、地平線に近いほど赤紫に光る
    float3 col = lerp(float3(0.16, 0.015, 0.16), float3(0.020, 0.004, 0.045), pow(above, 0.55)) * step(p.y, horizon + 1.0);
    // 太陽のにじみ (地平線の真ん中の上)
    float2 sunAt = float2(size.x * 0.5, horizon - H * 0.10);
    float2 ds = (p - sunAt) / H;
    if (look[0].y > 0.5) col += float3(0.45, 0.08, 0.30) * exp(-dot(ds, ds) * 9.0) * 0.45;
    // 地平線の照り (色がゆっくり変わる)
    float dh = (p.y - horizon) / H;
    float3 glowCol = lerp(float3(1.0, 0.15, 0.75), Hue(t * 0.02 + 0.55), 0.35);
    col += glowCol * exp(-abs(dh) * 22.0) * 0.30;
    // 床の上の、遠くのもや
    col += float3(0.10, 0.02, 0.14) * exp(-max(dh, 0.0) * 6.0) * step(horizon, p.y) * 0.6;
    return col;
}
// 縞のある太陽 (下ほど縞が太い)。
float3 Sun(float2 p, float horizon, float3 col) {
    float H = size.y;
    float R = H * 0.17;
    float2 c = float2(size.x * 0.5, horizon - H * 0.10);
    float2 d = (p - c) / R;
    float r = length(d);
    float aa = 1.0 / (pixelScale * R);
    float disc = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);
    float v = saturate(d.y * 0.5 + 0.5);  // 上 0 .. 下 1
    // 横の縞: 上の半分の中ほどから下へ、だんだん太く (ゆっくり下へ流れる)
    float band = saturate((d.y + 0.55) / 0.80);
    float lines = frac(d.y * 8.0 - time * 0.12);
    float gap = 0.62 * band;
    float cut = d.y > -0.55 ? smoothstep(gap - 0.05, gap + 0.01, lines) : 1.0;
    float3 sun = lerp(float3(1.0, 0.86, 0.35), float3(1.0, 0.22, 0.62), smoothstep(0.15, 0.85, v)) * 0.62;
    sun += float3(0.25, 0.20, 0.05) * exp(-r * r * 2.0) * (1.0 - v);  // 真ん中の上は明るく
    return lerp(col, sun, disc * cut * step(p.y, horizon));
}
// 遠い山のかげ (地平線の上。縁がネオンで光る)。
float3 Hills(float2 p, float horizon, float3 col) {
    float H = size.y;
    float u = p.x / H;
    float side = saturate(abs(p.x - size.x * 0.5) / (size.x * 0.5));  // 真ん中は低く (太陽が見えるように)
    float top = horizon - H * (0.012 + 0.11 * side * side * (0.6 + 0.8 * Fbm(float2(u * 3.0, 1.7), 4)));
    float aa = 0.8 / pixelScale;
    float inside = smoothstep(top - aa, top + aa, p.y) * step(p.y, horizon);
    float rim = exp(-abs(p.y - top) * pixelScale * 0.6) * step(p.y, horizon + 0.5);
    col = lerp(col, float3(0.025, 0.005, 0.045), inside);
    return col + float3(0.2, 0.85, 1.0) * rim * 0.45 * side;
}
// 床のネオンの格子 (遠近。手前へ流れる)・地平線の線・ウィンドウの縁の虹 (画面の細かさ)。
float3 Sharp(float2 p, float3 col) {
    float t = time;
    float H = size.y;
    float horizon = look[0].x;
    if (look[0].y > 0.5) {
        col = Sun(p, horizon, col);
        col = Hills(p, horizon, col);
    }
    // 格子: 地平線からの高さで奥行きを決める (下の端が手前 = 1)。微分は分かれ道の外で取る。
    float k = max((p.y - horizon) / (H - horizon), 1e-3);
    float z = 1.0 / k;                                 // 奥行き
    float gx = (p.x - size.x * 0.5) / (H - horizon) * z * 2.2;
    float gz = z * 1.6 + t * 0.9;                      // 手前へ流れる
    float2 g = float2(gx, gz);
    float2 fw = max(fwidth(g), 1e-4);
    float2 dist = abs(frac(g - 0.5) - 0.5) / fw;       // 線からの距離 (ピクセル)
    float d = min(dist.x, dist.y);
    float core = saturate(1.2 - d * 0.8);
    float glow = exp(-d * 0.22);
    float fade = exp(-z * 0.10) * smoothstep(0.0, 0.03, k);  // 遠いほど (細かすぎて) 消す
    float3 lineCol = Hue(0.83 + 0.10 * sin(gz * 0.35 - t * 0.2) + 0.0 * gx);  // 赤紫のまわりで、奥から手前へ色が流れる
    lineCol = lerp(lineCol, float3(1.0, 0.25, 0.85), 0.35);
    float floorOn = step(horizon, p.y);
    col += lineCol * (core * 0.75 + glow * 0.30) * fade * floorOn;
    // 地平線の明るい線
    float dh = abs(p.y - horizon) * pixelScale;
    col += float3(0.35, 0.90, 1.0) * (saturate(1.3 - dh * 0.7) * 0.55 + exp(-dh * 0.18) * 0.22);
    // ウィンドウの縁の虹: 左上から時計回りの長さで色を決め、時間で流す。
    float W = size.x;
    float dl = p.x, dr = W - p.x, dtop = p.y, db = H - p.y;
    float edge = min(min(dl, dr), min(dtop, db));
    float s = edge == dtop ? p.x : (edge == dr ? W + p.y : (edge == db ? W + H + (W - p.x) : 2.0 * W + H + (H - p.y)));
    float3 rainbow = Hue(s / look[1].x - t * look[1].y);
    float e = edge * pixelScale;
    col += rainbow * (saturate(2.5 - e) * 0.85 + exp(-e * 0.09) * 0.30);
    return col;
}
)";

void GamingFill(BackdropScene& scene, const ui::Rect& w, double t, bool floating) {
    scene.look[0] = w.y + w.h * (floating ? 0.70f : 0.64f);
    scene.look[1] = floating ? 0.f : 1.f;
    scene.look[4] = kRainbowPeriod;
    scene.look[5] = kRainbowSpeed;
    const uint32_t count = std::clamp(static_cast<uint32_t>(w.w * w.h / 9000.f), 12u, 260u);
    scene.sprites.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const motion::Mote m = motion::MoteAt(w, i, t, 0x6A01u);
        scene.sprites.push_back({m.at, m.radius, motion::Rainbow(m.hue, 0.45f * m.alpha), SpriteShape::Glow});
    }
}

}  // namespace

const editor::w::MovingLook kGaming{"gaming", kGamingHlsl, 0.5f, &GamingFill};

}  // namespace rm::theme_gaming::scenes
