#include "rm_engine/theme_explore/editor/Scenes.h"

#include "rm_engine/theme_explore/editor/Motion.h"

#include <algorithm>
#include <cmath>

namespace rm::theme_explore::scenes {

namespace {

using editor::w::BackdropScene;
using editor::w::BackdropSprite;
using editor::w::SpriteShape;

// 面積に比べた数 (少なくとも `least`、多くても `most`)。
uint32_t CountFor(const ui::Rect& w, float perArea, uint32_t least, uint32_t most) {
    return std::clamp(static_cast<uint32_t>(w.w * w.h / perArea), least, most);
}

// ================================================================================================
// 宇宙: 歪めたノイズの星雲 (ガスのもやと暗い塵の筋)、淡い天の川の帯、光と影と大気のある惑星。
// 粒 = 3 つの深さの星 (流れる速さが違う・またたく・明るい星は光の筋)、ときどき流れ星。
// look[0] = 惑星 (x, y, 半径, 出すなら 1)。
// ================================================================================================
constexpr char kSpaceHlsl[] = R"(
// 筋の立つノイズ (山の稜線のように細い明るい筋: 星雲のフィラメント)。だいたい 0 .. 1。
float Ridged(float2 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    const float2x2 turn = float2x2(0.80, 0.60, -0.60, 0.80);
    for (int i = 0; i < octaves; ++i) {
        float r = 1.0 - abs(Noise(p) * 2.0 - 1.0);
        sum += amp * r * r;
        norm += amp;
        p = mul(turn, p) * 2.07 + 31.7;
        amp *= 0.5;
    }
    return sum / norm;
}
// 星雲と、星の光の霧 (粗い場)。
float3 Field(float2 p) {
    float s = min(size.x, size.y);
    float2 q = p / s;
    float t = time;
    float3 col = float3(0, 0, 0);
    // 星雲のある所: 左上から右下へゆるく曲がる大きな雲の帯 (残りは暗い星空)。
    float2 c = q - float2(0.40, 0.48) * size / s;
    float bend = c.y - 0.28 * c.x - 0.30 * c.x * c.x;
    float region = exp(-bend * bend * 5.0) * smoothstep(1.30, 0.05, length(c * float2(0.55, 1.0)));
    // 少しだけ歪めたノイズ (歪めすぎると大理石の模様になる)。ゆっくり流れ、形を変える。
    float2 n = q * 1.5 + float2(t * 0.0016, -t * 0.0006);
    float2 w1 = float2(Fbm(n * 0.8 + float2(1.7, 9.2) + t * 0.0025, 4), Fbm(n * 0.8 + float2(8.3, 2.8) - t * 0.0020, 4)) - 0.5;
    float2 m = n + 0.9 * w1;
    float big = Fbm(m * 0.9, 5);
    float fine = Fbm(m * 3.1 + 7.0, 6);
    float fil = Ridged(m * 2.2 + 3.0, 5);
    float body = region * smoothstep(0.32, 0.74, big);
    float density = body * (0.25 + 1.9 * fine * fine);
    float strands = body * smoothstep(0.55, 0.95, fil);
    float dust = smoothstep(0.52, 0.70, Fbm(m * 2.0 + 21.0 + 0.6 * w1, 6)) * smoothstep(0.2, 0.55, big);
    // 色: 水素の赤紫、酸素の青緑、反射の青
    float3 hydrogen = float3(0.70, 0.13, 0.42);
    float3 oxygen = float3(0.07, 0.42, 0.55);
    float3 reflectBlue = float3(0.15, 0.22, 0.70);
    float mixA = smoothstep(0.38, 0.66, Fbm(m * 0.6 + 40.0, 3));
    float mixB = smoothstep(0.52, 0.78, Fbm(m * 1.2 + 55.0, 3));
    float3 tint = lerp(lerp(reflectBlue, hydrogen, mixA), oxygen, mixB * 0.65);
    col += tint * density * 0.58;
    col += lerp(tint, float3(1.0, 0.80, 0.85), 0.45) * strands * 0.34;
    col += float3(1.0, 0.85, 0.82) * pow(saturate(density * 1.25), 5.0) * 0.20;  // 明るい芯
    col *= 1.0 - 0.70 * dust;
    // 全体に、ごく淡い光の霧 (遠い星の集まり)
    col += float3(0.018, 0.020, 0.042) * smoothstep(0.3, 0.8, Fbm(q * 2.5 + 11.0, 4)) * (0.4 + region);
    return col;
}
// 惑星の輪の濃さ (rr = 輪の面の上の、球の半径を 1 とした距離。e = ぼかす幅)。すき間のある数本の帯。
float Ring(float rr, float e) {
    float a = smoothstep(1.38 - e, 1.42 + e, rr) * smoothstep(2.25 + e, 2.18 - e, rr);
    a *= 0.50 + 0.50 * Noise(float2(rr * 26.0, 0.5));
    a *= 1.0 - 0.85 * smoothstep(0.035 + e, 0.0, abs(rr - 1.86));  // すき間
    return a;
}
// 惑星 (look[0] = x, y, 半径, 出すなら 1): 光と影と大気、傾いた輪 (球の影が輪に、輪の影が球に落ちる)。
float3 Sharp(float2 p, float3 col) {
    if (look[0].w < 0.5) return col;
    float R = look[0].z;
    float2 d = (p - look[0].xy) / R;
    if (abs(d.x) > 2.6 || abs(d.y) > 2.0) return col;
    float t = time;
    float r2 = dot(d, d), r = sqrt(r2);
    float e = 0.7 / (pixelScale * R);  // 1 ピクセルの幅 (半径を 1 として)
    float cover = 1.0 - smoothstep(1.0 - e, 1.0 + e, r);
    float3 L = normalize(float3(-0.80, -0.38, 0.46));
    // 輪の面: 画面の上で傾け (ca, sa)、奥へ寝かせた円盤。見える形はひしゃげた楕円 (縦が flat 倍)。
    // 回した座標で、点 (x, y) の奥行き z = y / flat * lean。y > 0 (下) が手前。
    const float ca = cos(-0.33), sa = sin(-0.33), flat = 0.26, lean = sqrt(1.0 - flat * flat);
    float2 u = float2(ca * d.x - sa * d.y, sa * d.x + ca * d.y);
    float3 Lr = float3(ca * L.x - sa * L.y, sa * L.x + ca * L.y, L.z);
    float rr = length(float2(u.x, u.y / flat));
    float ringA = Ring(rr, e / flat) * 0.80;
    float3 ringCol = float3(0.60, 0.53, 0.44) * (0.65 + 0.35 * Noise(float2(rr * 9.0, 3.3)));
    // 球の影が輪に落ちる: 輪の点から光の方へ進むと球に当たるか。
    float3 P = float3(u, u.y / flat * lean);
    float b = dot(P, Lr), disc = b * b - (dot(P, P) - 1.0);
    float shade = b < 0.0 ? smoothstep(-0.02, 0.05, disc) : 0.0;
    ringCol *= 1.0 - 0.92 * shade;
    bool front = u.y > 0.0;
    if (!front) col = lerp(col, ringCol, ringA);
    // 球: 帯のあるガスの星 (輪と同じ傾きの軸のまわりに、ゆっくり回る)
    float3 sn = float3(d, sqrt(saturate(1.0 - r2)));
    float lighting = dot(sn, L);
    float day = smoothstep(-0.12, 0.55, lighting);
    float lat = sn.y * ca + sn.x * sa;
    float lon = atan2(sn.x * ca - sn.y * sa, sn.z);
    float turb = Fbm(float2(lon * 2.2 + t * 0.012, lat * 6.0), 5);
    float bands = 0.5 + 0.5 * sin(lat * 19.0 + 2.2 * turb + 1.1 * sin(lat * 6.0));
    float fineBands = 0.5 + 0.5 * sin(lat * 55.0 + 3.0 * turb);
    float3 surf = lerp(float3(0.66, 0.52, 0.39), float3(0.86, 0.78, 0.65), bands * 0.8 + fineBands * 0.2);
    surf = lerp(surf, float3(0.56, 0.38, 0.28), smoothstep(0.58, 0.85, Fbm(float2(lon * 3.0 + t * 0.016, lat * 12.0), 5)) * 0.35);
    float limb = pow(saturate(sn.z), 0.50);
    // 輪の影が球に落ちる: 球の点から光の方へ進み、輪の面に当たった所の輪の濃さ。
    float3 S = float3(ca * sn.x - sa * sn.y, sa * sn.x + ca * sn.y, sn.z);
    float3 Nr = float3(0.0, lean / flat, -1.0);
    float k = -dot(Nr, S) / dot(Nr, Lr);
    float3 H = S + k * Lr;
    float ringShadow = k > 0.0 ? Ring(length(float2(H.x, H.y / flat)), 0.03) : 0.0;
    float3 planet = surf * day * limb * 0.58 * (1.0 - 0.70 * ringShadow);
    float rim = pow(1.0 - saturate(sn.z), 3.0) * saturate(lighting + 0.25);
    planet += float3(0.30, 0.48, 0.90) * rim * 0.20;
    col = col * (1.0 - cover) + planet * cover;
    if (front) col = lerp(col, ringCol, ringA);
    // 大気のにじみ (光の側)
    float outside = max(r - 1.0, 0.0);
    float facing = saturate(dot(normalize(d + 1e-4), normalize(L.xy)) * 0.8 + 0.25);
    col += float3(0.22, 0.40, 0.95) * exp(-outside * 20.0) * (1.0 - cover) * facing * 0.18;
    return col;
}
)";

void SpaceFill(BackdropScene& scene, const ui::Rect& w, double t, bool floating) {
    const float m = std::min(w.w, w.h);
    const float radius = std::clamp(m * 0.085f, 34.f, 105.f);
    const Vec2 planet{w.x + w.w - std::max(w.w * 0.16f, 2.6f * radius), w.y + std::max(w.h * 0.25f, 1.9f * radius)};
    const bool showPlanet = !floating && w.w > 5.f * radius && w.h > 4.f * radius;
    scene.look[0] = planet.x, scene.look[1] = planet.y, scene.look[2] = radius, scene.look[3] = showPlanet ? 1.f : 0.f;

    const uint32_t stars = CountFor(w, floating ? 1100.f : 650.f, 80, 4000);
    scene.sprites.reserve(stars + 1);
    for (uint32_t i = 0; i < stars; ++i) {
        const motion::Star s = motion::StarAt(w, i, t, 0x5A01u);
        if (showPlanet) {  // 惑星の後ろの星は見えない
            const float dx = s.at.x - planet.x, dy = s.at.y - planet.y;
            if (dx * dx + dy * dy < radius * radius * 1.02f) continue;
        }
        BackdropSprite b;
        b.at = s.at;
        b.radius = s.radius;
        b.color = motion::StarColor(s.temperature).Alpha(s.brightness * (s.layer == 0 ? 0.85f : 1.f));
        b.shape = SpriteShape::Star;
        b.spikes = s.layer == 2 && s.brightness > 0.7f ? (s.brightness - 0.7f) * 2.5f : 0.f;
        scene.sprites.push_back(b);
    }
    if (const motion::Meteor me = motion::MeteorAt(w, t, 0x5A02u); me.visible && !floating) {
        BackdropSprite b;
        b.at = me.head;
        b.tail = me.tail;
        b.radius = 1.1f;
        b.color = Color{0.85f, 0.92f, 1.f, me.brightness};
        b.shape = SpriteShape::Streak;
        scene.sprites.push_back(b);
    }
}

// ================================================================================================
// 深海: 上から届く光が深さで消えていく水、ゆらめく光の筋 (god rays)、水面近くのコースティクス (光の網)、
// 漂うにごり、海の底の岩のかげ。粒 = マリンスノー、海の底から上る泡、ゆっくり明滅する生き物の光。
// look[0] = (光の元の x, 海の底を出すなら 1, 0, 0)。
// ================================================================================================
constexpr char kDeepSeaHlsl[] = R"(
// 水面を通った光の網 (よく知られた、波の式を重ねる作り方)。
float Caustic(float2 uv, float t) {
    float2 p = uv * 6.28318 - 250.0;
    float2 i = p;
    float c = 1.0;
    const float inten = 0.005;
    [unroll] for (int n = 0; n < 4; ++n) {
        float tt = t * (1.0 - 3.5 / float(n + 1));
        i = p + float2(cos(tt - i.x) + sin(tt + i.y), sin(tt - i.y) + cos(tt + i.x));
        c += 1.0 / length(float2(p.x / (sin(i.x + tt) / inten), p.y / (cos(i.y + tt) / inten)));
    }
    c /= 4.0;
    c = 1.17 - pow(c, 1.4);
    return pow(abs(c), 8.0);
}
// 水・光の筋・光の網・にごり (粗い場)。
float3 Field(float2 p) {
    float t = time;
    float y = p.y / size.y;
    float2 q = p / size.y;
    // 水: 上ほど明るい青緑、深いほど暗い青、底はほとんど黒。
    float3 col = float3(0.010, 0.135, 0.165) * exp(-y * 2.9) + float3(0.000, 0.020, 0.045) * exp(-y * 0.9);
    // 水面の明るさ (上の端。光の元の上ほど明るく、波でゆらめく)
    float sx = (p.x - look[0].x) / size.y;
    float surface = exp(-y * 14.0) * (0.55 + 0.45 * Noise(float2(q.x * 6.0 + t * 0.15, t * 0.25))) * exp(-sx * sx * 1.2);
    col += float3(0.10, 0.32, 0.34) * surface;
    // 光の筋: ウィンドウの上の外にある光の元から、少し広がって差しこむ。
    float2 src = float2(look[0].x, -size.y * 0.75);
    float2 d = p - src;
    float ang = atan2(d.x, d.y);
    float shafts = 0.62 * Noise(float2(ang * 15.0, t * 0.10)) + 0.38 * Noise(float2(ang * 41.0 + 3.1, t * 0.19));
    shafts = smoothstep(0.42, 0.92, shafts);
    float shimmer = 0.70 + 0.30 * Noise(float2(ang * 90.0, t * 0.8 - y * 2.0));
    float fade = exp(-y * 2.2) * (0.35 + 0.65 * smoothstep(-0.2, 0.25, y));
    col += float3(0.20, 0.48, 0.50) * shafts * shimmer * fade * 0.40;
    // 水面の近くの光の網
    float cs = Caustic(q * 1.3 + float2(t * 0.006, 0.0), t * 0.30);
    col += float3(0.22, 0.50, 0.50) * cs * exp(-y * 6.5) * 0.22 * (0.5 + 0.5 * shafts);
    // 漂うにごり (大きなもや)
    float haze = Fbm(q * 2.2 + float2(t * 0.010, -t * 0.003), 5);
    col *= 0.70 + 0.55 * haze;
    return col;
}
// 海藻 1 本のかげの濃さ: 根もと x0、高さ h (DIP)、太さ wd、揺れの位相 ph。葉が左右に交互に付く。
float Kelp(float2 p, float x0, float h, float wd, float ph) {
    float up = (size.y - p.y) / h;  // 根もと 0 .. 先 1
    if (up < 0.0 || up > 1.0) return 0.0;
    float sway = (sin(time * 0.50 + ph + up * 2.0) * 0.6 + sin(time * 0.29 + ph * 1.7 + up * 3.3) * 0.4) * 0.10 * h * up * up;
    float cx = x0 + sway + 0.03 * h * sin(up * 4.0 + ph);
    float dx = p.x - cx;
    float leaf = pow(abs(sin(up * 16.0 + ph)), 3.0);  // 葉: 茎から左右に交互に出る
    float side = sin(up * 16.0 + ph) > 0.0 ? 1.0 : -1.0;
    float stem = lerp(wd * 0.35, wd * 0.12, up);
    float blade = wd * leaf * (1.0 - up * 0.6) * (dx * side > 0.0 ? 1.0 : 0.25);
    float wdt = stem + blade;
    float aa = 0.8 / pixelScale;
    return (1.0 - smoothstep(wdt - aa, wdt + aa, abs(dx))) * smoothstep(1.0, 0.92, up);
}
// 海の底の岩 (遠い列と近い列) と、揺れる海藻のかげ (画面の細かさ)。look[0].y = 出すなら 1。
float3 Sharp(float2 p, float3 col) {
    if (look[0].y < 0.5) return col;
    float y = p.y / size.y;
    if (y < 0.45) return col;
    float x = p.x / size.y;
    float aa = 0.8 / (pixelScale * size.y);
    // 遠い岩 (にごりで薄い)
    float far = 1.0 - (0.10 + 0.42 * saturate(Fbm(float2(x * 1.1 + 5.0, 1.7), 4) - 0.38));
    float farRock = smoothstep(far - aa, far + aa, y);
    // 近い岩
    float ground = 1.0 - (0.035 + 0.30 * saturate(Fbm(float2(x * 1.9, 3.7), 4) - 0.40) + 0.025 * Fbm(float2(x * 9.0, 1.3), 3));
    float rock = smoothstep(ground - aa, ground + aa, y);
    float lip = exp(-max(y - ground, 0.0) * 160.0) * rock;  // 岩の上の縁は、上からの光でかすかに明るい
    float shadow = farRock * 0.45;
    // 海藻: 遠い群れ (薄い) と近い群れ (濃い)
    float farKelp = 0.0, nearKelp = 0.0;
    [unroll] for (int k = 0; k < 6; ++k) {
        float fk = (float)k;
        float r = frac(fk * 0.618 + 0.3);
        if (k < 3) {
            farKelp = max(farKelp, Kelp(p, (0.08 + 0.09 * fk) * size.x, size.y * (0.22 + 0.14 * r), 9.0, fk * 1.9));
        } else {
            nearKelp = max(nearKelp, Kelp(p, (0.74 + 0.07 * (fk - 3.0)) * size.x, size.y * (0.34 + 0.18 * r), 15.0, fk * 1.9));
        }
    }
    shadow = max(shadow, farKelp * 0.55);
    shadow = max(shadow, max(rock, nearKelp) * 0.92);
    col = col * (1.0 - shadow) + float3(0.02, 0.08, 0.08) * lip * 0.7;
    return col;
}
)";

void DeepSeaFill(BackdropScene& scene, const ui::Rect& w, double t, bool floating) {
    scene.look[0] = w.x + w.w * 0.62f;
    scene.look[1] = floating ? 0.f : 1.f;
    const uint32_t snow = CountFor(w, 5500.f, 30, 600);
    const uint32_t bubbles = floating ? CountFor(w, 60000.f, 3, 12) : CountFor(w, 30000.f, 8, 60);
    const uint32_t glows = floating ? 0 : CountFor(w, 45000.f, 6, 40);
    scene.sprites.reserve(snow + bubbles + glows);
    for (uint32_t i = 0; i < snow; ++i) {
        const motion::Mote m = motion::SnowAt(w, i, t, 0x5E01u);
        scene.sprites.push_back({m.at, m.radius, Color{0.75f, 0.92f, 0.95f, m.alpha}, SpriteShape::Glow});
    }
    for (uint32_t i = 0; i < glows; ++i) {
        const motion::Mote m = motion::GlowAt(w, i, t, 0x5E02u);
        const Color c = i % 3 == 0 ? Color{0.30f, 0.65f, 1.f, 0.f} : Color{0.25f, 1.f, 0.85f, 0.f};
        scene.sprites.push_back({m.at, m.radius, c.Alpha(0.55f * m.alpha), SpriteShape::Glow});
    }
    for (uint32_t i = 0; i < bubbles; ++i) {
        const motion::Bubble b = motion::BubbleAt(w, i, t, 0x5E03u);
        scene.sprites.push_back({b.at, b.radius, Color{0.70f, 0.95f, 1.f, 0.60f * b.alpha}, SpriteShape::Bubble});
    }
}

// ================================================================================================
// 火山: 下から赤く照らされて渦を巻く煙の空、右の火山のかげ (火口の照り・噴き上がる煙・斜面を流れ下る溶岩)、
// 下の端の溶岩の照り、熱で揺らぐ空気。粒 = 舞い上がって冷めていく火の粉 (速いものは尾を引く)。
// look[0] = 火山 (真ん中の x, 高さ, 裾の半分の幅, 出すなら 1)、look[1] = 火口 (x, y)。
// ================================================================================================
constexpr char kVolcanoHlsl[] = R"(
// 山の上の面の高さ (y、DIP)。cx = 真ん中、h = 高さ、hw = 裾の半分の幅。左右で少し違う、ごつごつした成層火山。
float Mountain(float x, float cx, float h, float hw) {
    float dx = (x - cx) / hw;
    float a = abs(dx) * (dx < 0.0 ? 0.90 : 1.10);  // (左の斜面が少しなだらか)
    const float crater = 0.085;
    float cone = pow(saturate(1.0 - max(a - crater, 0.0) / (1.0 - crater)), 1.9);
    cone += 0.10 * exp(-pow((dx + 0.42) / 0.16, 2.0)) * saturate(1.0 - a);  // 左の肩 (古い火口)
    float top = 1.0 - 0.045 * smoothstep(crater, 0.0, a);  // 火口のくぼみ
    float ridges = (Fbm(float2(x / hw * 2.6, 7.1), 4) - 0.5) * 0.22 + (Noise(float2(x / hw * 18.0, 1.3)) - 0.5) * 0.04;
    return size.y - h * saturate(cone * top + ridges * saturate(a * 2.5) * (1.0 - a * 0.6));
}
// 手前の低い丘 (ウィンドウの下の端に沿って)。
float Foothills(float x) {
    float u = x / size.y;
    return size.y * (1.0 - 0.035 - 0.10 * saturate(Fbm(float2(u * 2.4, 4.4), 4) - 0.35));
}
float2 Crater() { return look[1].xy; }  // (火口の底。C++ が Mountain と同じ式で計算して入れる)
// 煙の空・噴煙・照り (粗い場)。
float3 Field(float2 p) {
    float t = time;
    float H = size.y;
    float y = p.y / H;
    float2 q = p / H;
    // 熱でゆらぐ空気: 下ほど強く、座標を少し揺らす。
    float2 shimmer = float2(Noise(q * 9.0 + float2(0.0, t * 1.3)), Noise(q * 9.0 + float2(5.2, t * 1.1))) - 0.5;
    float2 hq = q + shimmer * 0.012 * smoothstep(0.35, 1.0, y);
    // 空の煙: 風に流れ、渦を巻く。下からの溶岩の光で赤く照らされる (上ほど暗い灰)。
    float2 sq = hq * 1.4 + float2(t * 0.012, t * 0.004);
    float2 w = float2(Fbm(sq + float2(3.1, 1.7) + t * 0.010, 4), Fbm(sq + float2(7.3, 4.4) - t * 0.008, 4));
    float smoke = Fbm(sq + 2.0 * w, 6);
    float clouds = smoothstep(0.35, 0.80, smoke);
    float under = exp(-(1.0 - y) * 1.8);
    float3 col = float3(0.32, 0.065, 0.020) * under * (0.25 + 1.2 * clouds * clouds);
    col += float3(0.050, 0.038, 0.036) * clouds * (1.0 - y) * 0.8;
    // 下の端: 遠くの溶岩の照り (ゆっくり揺れる)
    float lavaGlow = exp(-(1.0 - y) * 6.0) * (0.70 + 0.30 * Fbm(float2(q.x * 2.5 + t * 0.04, t * 0.07), 3));
    col += float3(0.62, 0.15, 0.025) * lavaGlow * 0.55;
    if (look[0].w > 0.5) {
        float2 crater = Crater();
        // 噴煙: 火口から上へ、上るほど広がり、風で左へ流される。もくもくと湧き上がる。
        float2 r = (hq * H - crater) / H;
        float up = max(-r.y, 0.0);
        float bend = r.x + up * up * 0.65 + up * 0.08;
        float wdt = 0.030 + up * 0.36;
        float plume = exp(-bend * bend / (wdt * wdt)) * smoothstep(-0.005, 0.03, up);
        float2 pq = float2(bend * 4.0, (r.y + t * 0.035) * 4.0);
        float puff = Fbm(pq + 1.2 * w + float2(0.0, t * 0.05), 6);
        float dens = saturate(plume * smoothstep(0.22, 0.62, puff) * 1.3);
        float litFrom = exp(-up * 3.5);
        float3 ash = float3(0.045, 0.038, 0.036) * (0.6 + 0.8 * puff);
        col = col * (1.0 - dens * 0.85) + dens * (ash + float3(0.75, 0.22, 0.05) * litFrom * (0.6 + puff));
        // 火口の照り (ゆっくり明滅)
        float2 dc = (p - crater) / H;
        float beat = 0.80 + 0.20 * Noise(float2(t * 0.9, 1.0));
        col += float3(1.0, 0.40, 0.08) * exp(-dot(dc, dc) / 0.0030) * 0.55 * beat;
    }
    return col;
}
// 山のかげと、斜面を流れ下る溶岩、手前の丘 (画面の細かさ)。look[0] = 火山 (真ん中の x, 高さ, 裾の半分の幅, 出すなら 1)。
float3 Sharp(float2 p, float3 col) {
    float t = time;
    float H = size.y;
    float aa = 0.8 / pixelScale;
    float y = p.y / H;
    if (y < 0.35) return col;
    if (look[0].w > 0.5) {
        float cx = look[0].x, h = look[0].y, hw = look[0].z;
        float top = size.y * 2.0;  // (山の外: 計算しない。HLSL の ?: は両方を計算するので if で分ける)
        if (abs(p.x - cx) < hw * 1.2 && p.y > size.y - h * 1.15) top = Mountain(p.x, cx, h, hw);
        float inside = smoothstep(top - aa, top + aa, p.y);
        if (inside > 0.0) {
            float2 crater = Crater();
            float below = (p.y - top) / h;
            float3 rock = float3(0.020, 0.010, 0.008) * (0.8 + 0.6 * Fbm(p / H * 14.0, 3));
            // 溶岩: 火口から斜面を下る数本 (くねり、下で分かれる)。光る筋の中を、模様がゆっくり下へ流れる。
            float lava = 0.0, halo = 0.0;
            float fall = saturate((p.y - crater.y) / h);
            [unroll] for (int k = 0; k < 4; ++k) {
                float side = (k == 0 ? -0.42 : (k == 1 ? -0.12 : (k == 2 ? 0.22 : 0.50)));
                float startAt = (k == 1 || k == 3) ? 0.22 : 0.0;  // (2 本は途中から分かれる)
                float path = cx + side * hw * pow(fall, 0.85) + (Noise(float2(fall * 5.0, k * 7.3)) - 0.5) * hw * 0.22 * fall;
                float wdt = (0.9 + 3.2 * fall) * (0.55 + 0.9 * Noise(float2(fall * 14.0, k * 3.7)));
                float dx = (p.x - path) / wdt;
                float flow = Fbm(float2(dx * 0.35 + k * 3.0, (p.y - t * 7.0) / 14.0), 4);
                float alive = smoothstep(startAt, startAt + 0.08, fall) * smoothstep(0.95, 0.55, fall + 0.25 * Noise(float2(k * 2.1, 0.5)));
                float crust = smoothstep(0.30, 0.55, flow);  // 冷えて黒くなった皮のすき間から光る
                lava = max(lava, exp(-dx * dx) * (0.35 + 0.9 * flow) * crust * alive);
                halo = max(halo, exp(-dx * dx * 0.06) * alive);
            }
            lava = saturate(lava);
            float3 hot = lerp(float3(0.70, 0.09, 0.01), float3(1.0, 0.66, 0.22), smoothstep(0.55, 1.0, lava));
            float rimLight = exp(-below * 50.0) * 0.08;  // 面の縁は、空の照り返しでかすかに赤い
            float3 mountain = rock + float3(0.60, 0.15, 0.03) * rimLight + hot * lava;
            mountain += float3(0.30, 0.06, 0.012) * halo * 0.25;  // 溶岩のまわりの岩も、その光で照らされる
            col = lerp(col, mountain, inside);
        }
    }
    // 手前の丘 (ほとんど黒。上の縁だけ赤く照る)
    if (y < 0.80) return col;
    float hill = Foothills(p.x);
    float near = smoothstep(hill - aa, hill + aa, p.y);
    float edge = exp(-max(p.y - hill, 0.0) / 3.0) * near;
    col = lerp(col, float3(0.012, 0.006, 0.005) + float3(0.35, 0.08, 0.02) * edge * 0.35, near);
    return col;
}
)";

void VolcanoFill(BackdropScene& scene, const ui::Rect& w, double t, bool floating) {
    const float h = std::clamp(w.h * 0.46f, 160.f, 420.f);
    const float cx = w.x + w.w - std::max(w.w * 0.20f, 1.25f * h);
    const bool showMountain = !floating && w.w > 2.2f * h;
    scene.look[0] = cx, scene.look[1] = h, scene.look[2] = 1.35f * h, scene.look[3] = showMountain ? 1.f : 0.f;
    const Vec2 crater{cx, w.y + w.h - h * 0.955f};  // (火口のくぼみの底。シェーダーの Mountain の真ん中の高さ)
    scene.look[4] = crater.x, scene.look[5] = crater.y + 3.f;
    const uint32_t embers = floating ? CountFor(w, 9000.f, 12, 160) : CountFor(w, 4200.f, 30, 520);
    scene.sprites.reserve(embers);
    for (uint32_t i = 0; i < embers; ++i) {
        const motion::Ember e = motion::EmberAt(w, i, t, 0x7A01u, crater, showMountain ? 0.30f : 0.f);
        if (e.alpha <= 0.01f) continue;
        BackdropSprite b;
        b.at = e.at;
        b.radius = e.radius;
        b.color = motion::EmberColor(e.heat).Alpha(e.alpha * (0.35f + 0.65f * e.heat));
        const float speed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.y * e.velocity.y);
        if (speed > 60.f && e.heat > 0.5f) {  // 速くて熱い粒は、動いた跡が尾に見える
            b.shape = SpriteShape::Streak;
            b.tail = {-e.velocity.x * 0.06f, -e.velocity.y * 0.06f};
            b.radius *= 0.8f;
        } else {
            b.shape = SpriteShape::Glow;
        }
        scene.sprites.push_back(b);
    }
}

}  // namespace

const editor::w::MovingLook kSpace{"space", kSpaceHlsl, 0.35f, &SpaceFill};
const editor::w::MovingLook kDeepSea{"deep_sea", kDeepSeaHlsl, 0.4f, &DeepSeaFill};
const editor::w::MovingLook kVolcano{"volcano", kVolcanoHlsl, 0.35f, &VolcanoFill};

}  // namespace rm::theme_explore::scenes
