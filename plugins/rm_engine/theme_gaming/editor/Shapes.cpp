#include "rm_engine/theme_gaming/editor/Shapes.h"

#include "ui/Shape.h"

#include <algorithm>
#include <cmath>

namespace rm::theme_gaming::shapes {

using ui::shape::AddBar;
using ui::shape::AddEllipse;
using ui::shape::AddPolygon;
using ui::shape::Random;
using ui::shape::Rotate;

ui::Color Spectrum(float t, float alpha) {
    t -= std::floor(t);
    const float h = t * 5.f;  // 300 度を 60 度ずつ 5 つに
    const float f = h - std::floor(h);
    switch (static_cast<int>(h)) {
        case 0: return {1.f, f, 0.f, alpha};        // 赤 → 黄
        case 1: return {1.f - f, 1.f, 0.f, alpha};  // 黄 → 緑
        case 2: return {0.f, 1.f, f, alpha};        // 緑 → 水色
        case 3: return {0.f, 1.f - f, 1.f, alpha};  // 水色 → 青
        default: return {f, 0.f, 1.f, alpha};       // 青 → 紫
    }
}

GridShapes NeonGrid(const ui::Rect& w, float horizon, float spacing) {
    GridShapes g;
    const float bottom = w.y + w.h, depth = bottom - horizon;
    if (depth <= 0) return g;
    spacing = std::max(spacing, 8.f);
    // 横の線: 地平線に近いほど詰まる (間が下へ 2 乗で広がる)。
    const int rows = std::clamp(static_cast<int>(depth / spacing), 3, 24);
    for (int k = 1; k <= rows; ++k) {
        const float s = static_cast<float>(k) / static_cast<float>(rows);
        const float y = horizon + depth * s * s;
        AddBar(g.lines, {w.x - 2.f, y}, {w.x + w.w + 2.f, y}, 1.f + s, 1.f + s);
    }
    // 縦の線: 地平線の真ん中から、下の端で spacing の 3 倍ずつ離れて広がる。
    const float cx = w.x + w.w * 0.5f, step = spacing * 3.f;
    const int cols = static_cast<int>(w.w * 0.5f / step * 1.6f) + 2;
    for (int i = -cols; i <= cols; ++i) {
        AddBar(g.lines, {cx + static_cast<float>(i) * step * 0.04f, horizon}, {cx + static_cast<float>(i) * step, bottom + 2.f}, 0.8f, 1.6f);
    }
    AddBar(g.horizonLine, {w.x - 2.f, horizon}, {w.x + w.w + 2.f, horizon}, 2.f, 2.f);
    return g;
}

ui::Path Pixels(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const float s = rnd.Next() < 0.7f ? size : 2.f * size;
        const float x = std::floor(area.x + area.w * rnd.Next()), y = std::floor(area.y + area.h * rnd.Next());
        const Vec2 q[4] = {{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}};
        AddPolygon(p, q);
    }
    return p;
}

ui::Path Controller(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    auto at = [&](float x, float y) {
        const Vec2 v = Rotate({x * r, y * r}, degreesCcw);
        return Vec2{center.x + v.x, center.y + v.y};
    };
    // 胴: 角の丸い横長の形 (上の辺は少しへこむ)。
    const Vec2 body[] = {at(-0.62f, -0.34f), at(-0.30f, -0.38f), at(0.f, -0.33f), at(0.30f, -0.38f), at(0.62f, -0.34f),
                         at(0.76f, -0.18f),  at(0.70f, 0.10f),   at(0.40f, 0.12f), at(0.f, 0.10f),    at(-0.40f, 0.12f),
                         at(-0.70f, 0.10f),  at(-0.76f, -0.18f)};
    AddPolygon(p, body);
    // 握り: 下へ開く 2 つの楕円。
    AddEllipse(p, at(-0.56f, 0.18f), 0.26f * r, 0.42f * r, degreesCcw - 28.f);
    AddEllipse(p, at(0.56f, 0.18f), 0.26f * r, 0.42f * r, degreesCcw + 28.f);
    return p;
}

}  // namespace rm::theme_gaming::shapes
