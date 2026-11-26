#include "ui/Shape.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace rm::ui::shape {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;

Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 Scale(Vec2 a, float s) { return {a.x * s, a.y * s}; }

// 画面上で時計回り (y は下向き) = この座標では面積が正。
float SignedArea(std::span<const Vec2> pts) {
    float a = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2 p = pts[i], q = pts[(i + 1) % pts.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

}  // namespace

Vec2 Rotate(Vec2 v, float degreesCcw) {
    const float a = degreesCcw * kDeg, cs = std::cos(a), sn = std::sin(a);
    return {v.x * cs + v.y * sn, v.y * cs - v.x * sn};
}

float Random::Next() {
    // xorshift32: 小さく速く、同じ種ならどこでも同じ数になる。
    if (state == 0) state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
}

Fit FitOf(const Path& path, const Rect& box) {
    Fit f;
    if (path.steps.empty()) return f;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    auto grow = [&](Vec2 v) { x0 = std::min(x0, v.x), y0 = std::min(y0, v.y), x1 = std::max(x1, v.x), y1 = std::max(y1, v.y); };
    for (const auto& st : path.steps) {
        grow(st.a);
        if (st.op == Path::Op::Cubic) grow(st.b), grow(st.c);
    }
    const float w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return f;
    f.scale = std::min(box.w / w, box.h / h);
    f.from = {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    f.to = {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
    return f;
}

Path Transformed(const Path& path, const Fit& fit) {
    Path out = path;
    for (auto& st : out.steps) {
        st.a = fit(st.a);
        st.b = fit(st.b);
        st.c = fit(st.c);
    }
    return out;
}

Path FitInto(const Path& path, const Rect& box) { return Transformed(path, FitOf(path, box)); }

void AddEllipse(Path& p, Vec2 c, float rx, float ry, float degrees) {
    // 3 次曲線 4 本 (おなじみの 0.5523 のハンドル)。画面上で時計回り: 右、下、左、上。
    constexpr float k = 0.5522847f;
    auto at = [&](float x, float y) { return Add(c, Rotate({x, y}, degrees)); };
    p.Begin(at(rx, 0));
    p.Cubic(at(rx, ry * k), at(rx * k, ry), at(0, ry));
    p.Cubic(at(-rx * k, ry), at(-rx, ry * k), at(-rx, 0));
    p.Cubic(at(-rx, -ry * k), at(-rx * k, -ry), at(0, -ry));
    p.Cubic(at(rx * k, -ry), at(rx, -ry * k), at(rx, 0));
}

void AddCircle(Path& p, Vec2 c, float r) { AddEllipse(p, c, r, r, 0); }

void AddPolygon(Path& p, std::span<const Vec2> pts) {
    if (pts.size() < 3) return;
    const bool clockwise = SignedArea(pts) >= 0;
    p.Begin(clockwise ? pts.front() : pts.back());
    for (size_t i = 1; i < pts.size(); ++i) p.Line(clockwise ? pts[i] : pts[pts.size() - 1 - i]);
}

void AddBar(Path& p, Vec2 a, Vec2 b, float wa, float wb) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len <= 0) return;
    const Vec2 n{-dy / len, dx / len};  // 棒を横切る向き
    const Vec2 quad[4] = {Add(a, Scale(n, wa * 0.5f)), Add(b, Scale(n, wb * 0.5f)), Add(b, Scale(n, -wb * 0.5f)),
                          Add(a, Scale(n, -wa * 0.5f))};
    AddPolygon(p, quad);
}

void AddStar(Path& p, Vec2 center, int points, float outer, float inner, float degreesCcw) {
    if (points < 2) return;
    std::vector<Vec2> pts;
    pts.reserve(static_cast<size_t>(points) * 2);
    for (int i = 0; i < points * 2; ++i) {
        const float r = (i % 2 == 0) ? outer : inner;
        pts.push_back(Add(center, Rotate({0, -r}, degreesCcw - 180.f * static_cast<float>(i) / static_cast<float>(points))));
    }
    AddPolygon(p, pts);
}

Path Dots(const Rect& area, int count, float size, uint32_t seed) {
    Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const Vec2 c{area.x + area.w * rnd.Next(), area.y + area.h * rnd.Next()};
        AddCircle(p, c, size * (0.35f + 0.65f * rnd.Next()) * 0.5f);
    }
    return p;
}

Path WavyBottom(const Rect& r, float depth, float phase) {
    // 左端からのピクセル l の所の高さ: 長いうねりと短いさざ波。
    auto height = [&](float l) {
        const float u = 0.5f + 0.25f * std::sin(l * 0.011f + phase) + 0.15f * std::sin(l * 0.037f + 1.7f * phase) +
                        0.10f * std::sin(l * 0.083f + 0.6f + phase);
        return depth * (0.45f + 0.55f * std::clamp(u, 0.f, 1.f));
    };
    std::vector<Vec2> pts;
    const float bottom = r.y + r.h + 2.f, right = r.x + r.w + 2.f;
    pts.push_back({r.x - 2.f, bottom});
    for (float l = -2.f;; l += 6.f) {
        const float x = std::min(r.x + l, right);
        pts.push_back({x, r.y + r.h - height(x - r.x)});
        if (x >= right) break;
    }
    pts.push_back({right, bottom});
    Path p;
    AddPolygon(p, pts);
    return p;
}

}  // namespace rm::ui::shape
