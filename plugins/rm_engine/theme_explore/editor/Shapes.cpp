#include "rm_engine/theme_explore/editor/Shapes.h"

#include "ui/Shape.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

namespace rm::theme_explore::shapes {

namespace {

using ui::shape::AddBar;
using ui::shape::AddCircle;
using ui::shape::AddEllipse;
using ui::shape::AddPolygon;
using ui::shape::Random;
using ui::shape::Rotate;

constexpr float kPi = std::numbers::pi_v<float>;

Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }

// 半径 1 の向きで作った点を、`center` に `radius` 倍・`degreesCcw` 回して置く。
struct Place {
    Vec2 center;
    float radius, degrees;
    Vec2 operator()(float x, float y) const { return Add(center, Rotate({x * radius, y * radius}, degrees)); }
};

// しずく形 (先が上): 高さの半分 `h`、太さ `w`、`deg` 回す。中心 `c` はしずくの真ん中。
void AddTeardrop(ui::Path& p, const Place& at, Vec2 c, float h, float w, float deg) {
    constexpr int kSteps = 40;
    std::vector<Vec2> pts;
    for (int i = 0; i < kSteps; ++i) {
        const float a = 2.f * kPi * static_cast<float>(i) / kSteps;
        const Vec2 v = Rotate({w * std::sin(a) * std::sin(a * 0.5f), -h * std::cos(a)}, deg);
        pts.push_back(at(c.x + v.x, c.y + v.y));
    }
    AddPolygon(p, pts);
}

// 楕円の帯 (外の楕円と内の楕円の間) の、角度 a0 .. a1 (ラジアン。0 = 右、π/2 = 下) の部分。
void AddRingArc(ui::Path& p, Vec2 c, float rxOut, float ryOut, float rxIn, float ryIn, float a0, float a1, float deg) {
    constexpr int kSteps = 32;
    std::vector<Vec2> pts;
    for (int i = 0; i <= kSteps; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / kSteps;
        pts.push_back(Add(c, Rotate({rxOut * std::cos(a), ryOut * std::sin(a)}, deg)));
    }
    for (int i = kSteps; i >= 0; --i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / kSteps;
        pts.push_back(Add(c, Rotate({rxIn * std::cos(a), ryIn * std::sin(a)}, deg)));
    }
    AddPolygon(p, pts);
}

// 揺れながら細くなる帯: `from` から `to` へ `steps` 本の棒で。`wave` = 横に揺れる幅、`turns` = 揺れの数。
void AddWavyBar(ui::Path& p, Vec2 from, Vec2 to, float w0, float w1, float wave, float turns, float phase, int steps = 16) {
    const float dx = to.x - from.x, dy = to.y - from.y, len = std::hypot(dx, dy);
    if (len <= 0) return;
    const Vec2 n{-dy / len, dx / len};
    auto at = [&](float s) {
        const float off = wave * std::sin(s * turns * 2.f * kPi + phase) * std::min(1.f, s * 3.f);
        return Vec2{from.x + dx * s + n.x * off, from.y + dy * s + n.y * off};
    };
    for (int k = 0; k < steps; ++k) {
        const float s0 = static_cast<float>(k) / static_cast<float>(steps), s1 = static_cast<float>(k + 1) / static_cast<float>(steps);
        // (つなぎ目にすき間ができないよう、少し先まで伸ばす)
        AddBar(p, at(s0), at(std::min(1.f, s1 + 0.01f)), w0 + (w1 - w0) * s0, w0 + (w1 - w0) * s1);
    }
}

}  // namespace

// --- 宇宙 ---------------------------------------------------------------------------------

ui::Path Sparkles(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const Vec2 c{area.x + area.w * rnd.Next(), area.y + area.h * rnd.Next()};
        const float s = size * (0.5f + 0.5f * rnd.Next());
        ui::shape::AddStar(p, c, 4, 0.5f * s, 0.11f * s);
    }
    return p;
}

std::array<ui::Path, 3> Nebula(Vec2 center, float size, uint32_t seed) {
    std::array<ui::Path, 3> layers;
    constexpr float kScale[3] = {1.f, 0.66f, 0.36f};
    for (size_t k = 0; k < 3; ++k) {
        Random rnd{seed + static_cast<uint32_t>(k) * 7919u};
        const float f = kScale[k] * size;
        for (int i = 0; i < 7; ++i) {
            const Vec2 c{center.x + (rnd.Next() - 0.5f) * 1.2f * f, center.y + (rnd.Next() - 0.5f) * 0.7f * f};
            const float rx = f * (0.40f + 0.35f * rnd.Next());
            AddEllipse(layers[k], c, rx, rx * (0.45f + 0.30f * rnd.Next()), -20.f + 40.f * rnd.Next());
        }
    }
    return layers;
}

PlanetShapes RingedPlanet(Vec2 c, float r, float tilt) {
    PlanetShapes s;
    AddCircle(s.body, c, r);
    // 奥の半分 = 上 (角度 π .. 2π)、手前の半分 = 下 (0 .. π)。
    AddRingArc(s.ringBack, c, 2.2f * r, 0.60f * r, 1.45f * r, 0.36f * r, kPi, 2.f * kPi, tilt);
    AddRingArc(s.ringFront, c, 2.2f * r, 0.60f * r, 1.45f * r, 0.36f * r, 0.f, kPi, tilt);
    return s;
}

ui::Path ShootingStar(Vec2 head, float length, float degreesCcw) {
    ui::Path p;
    const float w = std::max(2.f, 0.03f * length);
    const Vec2 dir = Rotate({0, -1}, degreesCcw);
    AddBar(p, head, {head.x + dir.x * length, head.y + dir.y * length}, w, 0.05f);
    AddCircle(p, head, 0.9f * w);
    return p;
}

ui::Path Rocket(Vec2 center, float radius, float degreesCcw) {
    ui::Path p;
    const Place at{center, radius, degreesCcw};
    const Vec2 body[] = {at(0, -1),       at(0.18f, -0.78f), at(0.26f, -0.50f), at(0.27f, 0.20f),  at(0.22f, 0.45f),
                         at(-0.22f, 0.45f), at(-0.27f, 0.20f), at(-0.26f, -0.50f), at(-0.18f, -0.78f)};
    AddPolygon(p, body);
    for (const float side : {1.f, -1.f}) {
        const Vec2 fin[] = {at(0.24f * side, -0.02f), at(0.52f * side, 0.36f), at(0.52f * side, 0.62f), at(0.22f * side, 0.42f)};
        AddPolygon(p, fin);
    }
    const Vec2 nozzle[] = {at(0.16f, 0.44f), at(0.12f, 0.56f), at(-0.12f, 0.56f), at(-0.16f, 0.44f)};
    AddPolygon(p, nozzle);
    AddTeardrop(p, at, {0, 0.76f}, -0.26f, 0.16f, 0.f);  // 噴射の炎 (先が下。半径 1 の少し先まで)
    return p;
}

// --- 深海 ---------------------------------------------------------------------------------

std::vector<ui::Path> LightRays(const ui::Rect& w, int count, uint32_t seed) {
    std::vector<ui::Path> rays;
    Random rnd{seed};
    count = std::max(count, 1);
    for (int i = 0; i < count; ++i) {
        const float x = w.x + w.w * (0.12f + 0.72f * (static_cast<float>(i) + 0.6f * rnd.Next()) / static_cast<float>(count));
        const float top = w.y - 2.f;
        const float tw = w.w * (0.025f + 0.03f * rnd.Next());
        const float len = w.h * (0.6f + 0.3f * rnd.Next());
        const float slant = 0.30f * len;  // 右下へ (光は左上から)
        const float bw = tw * 2.8f;
        const Vec2 quad[4] = {{x - 0.5f * tw, top}, {x + 0.5f * tw, top}, {x + slant + 0.5f * bw, top + len}, {x + slant - 0.5f * bw, top + len}};
        ui::Path p;
        AddPolygon(p, quad);
        rays.push_back(std::move(p));
    }
    return rays;
}

BubbleShapes Bubbles(const ui::Rect& area, int count, float size, uint32_t seed) {
    BubbleShapes b;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const float v = rnd.Next();  // 0 = 上 .. 1 = 下
        const Vec2 c{area.x + area.w * rnd.Next(), area.y + area.h * v};
        const float r = 0.5f * size * (0.4f + 0.6f * rnd.Next()) * (1.f - 0.55f * v);
        AddCircle(b.body, c, r);
        AddEllipse(b.shine, {c.x - 0.38f * r, c.y - 0.38f * r}, 0.30f * r, 0.16f * r, 45.f);
    }
    return b;
}

ui::Path Kelp(Vec2 base, float height, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    const int n = 3 + static_cast<int>(seed % 3u);
    for (int i = 0; i < n; ++i) {
        const float off = (static_cast<float>(i) - 0.5f * static_cast<float>(n - 1)) * height * 0.07f + (rnd.Next() - 0.5f) * height * 0.03f;
        const float h = height * (0.6f + 0.4f * rnd.Next());
        const Vec2 from{base.x + off, base.y + 3.f};
        const Vec2 to{from.x + (rnd.Next() - 0.5f) * h * 0.2f, base.y - h};
        AddWavyBar(p, from, to, height * 0.045f + 1.5f, 1.2f, height * 0.05f, 1.3f + 0.5f * rnd.Next(), rnd.Next() * 6.f, 24);
    }
    return p;
}

ui::Path FishSchool(Vec2 center, float spread, int count, float length, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const Vec2 c{center.x + (rnd.Next() - 0.5f) * spread, center.y + (rnd.Next() - 0.5f) * spread * 0.5f};
        const float l = length * (0.7f + 0.5f * rnd.Next());
        AddEllipse(p, c, 0.5f * l, 0.2f * l, 0);
        const Vec2 tail[3] = {{c.x + 0.40f * l, c.y}, {c.x + 0.72f * l, c.y - 0.2f * l}, {c.x + 0.72f * l, c.y + 0.2f * l}};
        AddPolygon(p, tail);
    }
    return p;
}

ui::Path Jellyfish(Vec2 center, float radius, float degreesCcw) {
    ui::Path p;
    const Place at{center, radius, degreesCcw};
    // 傘: 上半分の楕円 (てっぺんが半径 1 の所) と、少し波打つ下の縁。
    std::vector<Vec2> bell;
    constexpr int kArc = 28;
    for (int i = 0; i <= kArc; ++i) {
        const float a = kPi + kPi * static_cast<float>(i) / kArc;  // 左 → 上 → 右
        bell.push_back(at(0.64f * std::cos(a), -0.36f + 0.64f * std::sin(a)));
    }
    constexpr int kEdge = 20;
    for (int i = 1; i < kEdge; ++i) {
        const float s = static_cast<float>(i) / kEdge;  // 右 → 左
        bell.push_back(at(0.64f - 1.28f * s, -0.36f + 0.07f * std::fabs(std::sin(s * 5.f * kPi))));
    }
    AddPolygon(p, bell);
    // 足 5 本: 傘の下の縁から垂れる。
    for (int i = 0; i < 5; ++i) {
        const float x = -0.40f + 0.20f * static_cast<float>(i);
        const float len = 0.95f + 0.25f * static_cast<float>((i * 3) % 5) / 4.f;
        AddWavyBar(p, at(x, -0.40f), at(x, -0.40f + len), 0.07f * radius, 0.02f * radius, 0.06f * radius, 1.5f, static_cast<float>(i), 18);
    }
    return p;
}

// --- 火山 ---------------------------------------------------------------------------------

VolcanoShapes Volcano(Vec2 base, float h, uint32_t seed) {
    VolcanoShapes v;
    Random rnd{seed};
    auto at = [&](float x, float y) { return Vec2{base.x + x * h, base.y - y * h}; };
    const Vec2 mountain[] = {at(-1.75f, -0.02f), at(-1.25f, 0.17f), at(-0.75f, 0.48f), at(-0.38f, 0.84f), at(-0.2f, 1.f),
                             at(-0.1f, 0.93f),   at(0.08f, 0.94f),  at(0.2f, 0.99f),   at(0.45f, 0.72f),  at(0.9f, 0.42f),
                             at(1.4f, 0.13f),    at(1.85f, -0.02f)};
    AddPolygon(v.mountain, mountain);
    v.crater = at(0, 0.95f);
    // 溶岩: 火口の縁から斜面に沿って流れ下る 3 本。
    struct Stream {
        Vec2 from, to;
    };
    for (const Stream& s : {Stream{at(-0.14f, 0.95f), at(-0.85f, 0.33f)}, Stream{at(0.f, 0.93f), at(0.12f, 0.40f)},
                            Stream{at(0.15f, 0.96f), at(0.95f, 0.30f)}}) {
        AddWavyBar(v.lava, s.from, s.to, 0.055f * h, 0.012f * h, 0.03f * h, 1.4f, rnd.Next() * 6.f, 20);
    }
    const Vec2 glow{v.crater.x, v.crater.y - 0.06f * h};
    AddCircle(v.glow[0], glow, 0.50f * h);
    AddCircle(v.glow[1], glow, 0.32f * h);
    AddCircle(v.glow[2], glow, 0.18f * h);
    return v;
}

ui::Path Smoke(Vec2 from, float height, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    constexpr int kPuffs = 10;
    for (int k = 0; k < kPuffs; ++k) {
        const float t = static_cast<float>(k) / (kPuffs - 1);
        const Vec2 c{from.x - 0.45f * height * std::pow(t, 1.3f), from.y - height * t};
        const float r = height * (0.05f + 0.13f * t);
        AddCircle(p, c, r);
        AddCircle(p, {c.x + (rnd.Next() - 0.5f) * r, c.y + (rnd.Next() - 0.5f) * 0.6f * r}, r * (0.6f + 0.3f * rnd.Next()));
    }
    return p;
}

ui::Path Embers(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const float v = std::sqrt(rnd.Next());  // 下ほど多い
        const Vec2 c{area.x + area.w * rnd.Next(), area.y + area.h * v};
        const float s = 0.5f * size * (0.5f + 0.5f * rnd.Next());
        const float deg = (rnd.Next() - 0.5f) * 50.f;
        const Vec2 d[4] = {Add(c, Rotate({0, -s}, deg)), Add(c, Rotate({0.45f * s, 0}, deg)), Add(c, Rotate({0, s}, deg)),
                           Add(c, Rotate({-0.45f * s, 0}, deg))};
        AddPolygon(p, d);
    }
    return p;
}

ui::Path Flame(Vec2 center, float radius, float degreesCcw) {
    ui::Path p;
    const Place at{center, radius, degreesCcw};
    AddTeardrop(p, at, {0, -0.1f}, 0.9f, 0.56f, 0.f);
    AddTeardrop(p, at, {0.44f, 0.30f}, 0.42f, 0.30f, -28.f);
    AddTeardrop(p, at, {-0.44f, 0.30f}, 0.42f, 0.30f, 28.f);
    return p;
}

}  // namespace rm::theme_explore::shapes
