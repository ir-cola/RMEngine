#include "rm_engine/theme_gaming/editor/Gaming.h"

#include "editor/widgets/OrnamentPicture.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/theme_gaming/editor/Shapes.h"
#include "ui/Context.h"
#include "ui/Shape.h"

#include <algorithm>

namespace rm::theme_gaming {

namespace {

namespace theme = editor::theme;
using editor::w::OrnamentPicture;
using ui::Color;
using ui::Rect;

// 絵の色 (内容の後ろにうっすら敷く)。どのパレットでも同じ。左下の影はテーマのアクセント。
namespace look {
Color GridLine() { return Color::Hex(0xFF2BD6, 0.14f); }   // 格子 (マゼンタ)
Color GridFaint() { return Color::Hex(0xFF2BD6, 0.07f); }  // パネルのウィンドウの格子
Color Horizon() { return Color::Hex(0x2BE4FF, 0.30f); }    // 地平線 (水色)
Color Pixel() { return Color::Hex(0x2BE4FF, 0.18f); }      // ドットのかけら
constexpr float kBandAlpha = 0.85f;                        // 虹色の帯
constexpr float kEdgeAlpha = 0.55f;                        // パネルの下の縁の虹色の線
}  // namespace look

// 虹色の帯: `r` を横に `segments` 個に分け、左から赤 → 紫。(1 本の帯でも四角を塗るだけなので、形は覚えておかない)
void Rainbow(ui::Context& c, const Rect& r, int segments, float alpha) {
    if (r.w <= 0 || r.h <= 0) return;
    const float step = r.w / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        // (となりと 0.5 ピクセル重ねて、すき間を見せない)
        c.P().FillRect({r.x + step * static_cast<float>(i), r.y, step + 0.5f, r.h}, shapes::Spectrum((static_cast<float>(i) + 0.5f) / static_cast<float>(segments), alpha));
    }
}

OrnamentPicture Make(int, const Rect& w, bool floating, const Rect&) {
    OrnamentPicture pic;
    const float horizon = w.y + w.h * (floating ? 0.70f : 0.66f);
    shapes::GridShapes grid = shapes::NeonGrid(w, horizon, floating ? 18.f : 26.f);
    pic.push_back({std::move(grid.lines), floating ? look::GridFaint() : look::GridLine()});
    if (floating) return pic;
    pic.push_back({std::move(grid.horizonLine), look::Horizon()});
    const int pixels = std::max(8, static_cast<int>(w.w * w.h / 40000.f));
    pic.push_back({shapes::Pixels({w.x, w.y, w.w, horizon - w.y}, pixels, 4.f, 0x6A01u), look::Pixel()});
    pic.push_back({ui::shape::FitInto(shapes::Controller({0, 0}, 1.f, 8.f), editor::w::OrnamentCorner(w)), theme::OrnamentColor()});
    return pic;
}

void DrawWindow(ui::Context& c, const Rect& window, bool floating) {
    editor::w::DrawOrnamentPicture(c, &Make, 0, window, floating);
    Rainbow(c, {window.x, window.y, window.w, 3.f}, 48, look::kBandAlpha);
    if (!floating) Rainbow(c, {window.x, window.y + window.h - 2.f, window.w, 2.f}, 48, look::kBandAlpha);
}

// パネルの下の縁に、虹色の細い線 (丸い角の内側だけ)。
void Panel(ui::Context& c, const Rect& r, float corner) {
    const float inset = std::max(corner, 2.f);
    Rainbow(c, {r.x + inset, r.y + r.h - 2.f, r.w - 2.f * inset, 2.f}, 24, look::kEdgeAlpha);
}

constexpr editor::w::OrnamentInfo kGaming{"gaming", &DrawWindow, &Panel};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) { registry.Contribute(editor::w::kOrnamentPoint, kGaming); }

}  // namespace rm::theme_gaming
