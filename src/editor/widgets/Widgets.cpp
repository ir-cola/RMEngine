#include "editor/widgets/Widgets.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "ui/Path.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace rm::editor::w {

using ui::Color;
using ui::Rect;

// ---------------------------------------------------------------------------
// Surfaces, icons, rows, tabs - the one drawing of each
// ---------------------------------------------------------------------------

namespace {

void FillGradient(ui::Painter& p, const Rect& r, float radius, const theme::Gradient& g) { p.FillRoundRectGradient(r, radius, g.top, g.bottom); }

// The light catching the top edge of a raised surface.
void TopLight(ui::Painter& p, const Rect& r, float radius, Color c) {
    const float inset = std::max(radius, 2.f);
    if (r.w > inset * 2) p.FillRect({r.x + inset, r.y + 1, r.w - inset * 2, 1}, c);
}

}  // namespace

void DrawSurface(ui::Context& c, const Rect& r, Surface s, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    auto& p = c.P();
    switch (s) {
        case Surface::Panel:
            DrawPanelFrame(c, r, ui::Corners::All(radius));
            break;
        case Surface::Raised:
            FillGradient(p, r, radius, theme::Raised(theme::PanelHeader));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
        case Surface::Hovered:
            FillGradient(p, r, radius, theme::Raised(theme::Hover));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::BorderHover);
            break;
        case Surface::Pressed:
            FillGradient(p, r, radius, theme::Raised(theme::Pressed).Flipped());
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
        case Surface::Sunken:
            FillGradient(p, r, radius, theme::Sunken(theme::Field));
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
        case Surface::Accent:
        case Surface::AccentHovered:
        case Surface::AccentPressed: {
            const Color base = s == Surface::Accent ? theme::Accent : s == Surface::AccentHovered ? theme::AccentHover : theme::AccentPressed;
            const theme::Gradient g = theme::AccentOf(base);
            FillGradient(p, r, radius, s == Surface::AccentPressed ? g.Flipped() : g);
            if (s != Surface::AccentPressed) TopLight(p, r, radius, theme::AccentHighlight());
            p.StrokeRoundRect(r, radius, theme::AccentPressed);
            break;
        }
        case Surface::Selected:
            FillGradient(p, r, radius, theme::Raised(theme::Selected));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::Accent.Alpha(0.35f));
            break;
        case Surface::Popup:
            p.Shadow(r, radius, theme::Shadow, 14);
            FillGradient(p, r, radius, theme::Raised(theme::PanelHeader));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
    }
}

namespace {

// Which windows are panel windows of their own (DrawBackdrop notes it; their picture is lighter).
std::unordered_map<const ui::Context*, bool>& FloatingWindows() {
    static std::unordered_map<const ui::Context*, bool> floating;
    return floating;
}

}  // namespace

void DrawBackdrop(ui::Context& c, bool floating) {
    FloatingWindows()[&c] = floating;
    if (const OrnamentInfo* o = theme::CurrentOrnament) o->window(c, c.Viewport(), floating);
}

void DrawBackdropOn(ui::Context& c, const Rect& r, float corner, bool panelThings) {
    const OrnamentInfo* o = theme::CurrentOrnament;
    if (!o || r.w <= 0 || r.h <= 0) return;
    const auto it = FloatingWindows().find(&c);
    c.P().PushClip(r);
    o->window(c, c.Viewport(), it != FloatingWindows().end() && it->second);
    c.P().PopClip();
    if (panelThings && o->panel) o->panel(c, r, corner);
}

void DrawPanelFrame(ui::Context& c, const Rect& r, ui::Corners k) {
    if (r.w <= 0 || r.h <= 0) return;
    auto& p = c.P();
    auto grow = [](float v) { return v > 0 ? v + 1.f : 0.f; };  // the outer line's corners: the same curve, 1px out
    const theme::Gradient fill = theme::Gentle(theme::Panel);
    const theme::Gradient inner = theme::EdgeInner();
    const Color outer = theme::EdgeOuter();
    p.StrokeRoundRectGradient(r.Inset(-1), {grow(k.tl), grow(k.tr), grow(k.br), grow(k.bl)}, outer, outer);
    p.FillRoundRectGradient(r, k, fill.top, fill.bottom);
    DrawBackdropOn(c, r, std::max(k.bl, k.br));  // the theme's picture, under the content
    p.StrokeRoundRectGradient(r, k, inner.top, inner.bottom);
}

void DrawBar(ui::Context& c, const Rect& r, Color base) {
    const theme::Gradient g = theme::Gentle(base);
    c.P().FillRectGradient(r, g.top, g.bottom);
    DrawBackdropOn(c, r, 0, false);  // the picture only: no snow piled on a thin strip
}

void DrawIcon(ui::Context& c, const Rect& r, Icon icon, Color color) {
    auto& p = c.P();
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    const float s = std::clamp(std::min(r.w, r.h) * 0.22f, 2.5f, 5.f);  // half the size of the mark
    constexpr float kW = 1.6f;
    switch (icon) {
        case Icon::Close:
            p.Line(cx - s, cy - s, cx + s, cy + s, color, kW);
            p.Line(cx - s, cy + s, cx + s, cy - s, color, kW);
            break;
        case Icon::Dots:
            for (int i = -1; i <= 1; ++i) p.FillCircle(cx + static_cast<float>(i) * s * 1.25f, cy, 1.6f, color);
            break;
        case Icon::DotsVertical:
            for (int i = -1; i <= 1; ++i) p.FillCircle(cx, cy + static_cast<float>(i) * s * 1.25f, 1.6f, color);
            break;
        case Icon::ChevronDown:
            p.Line(cx - s, cy - s * 0.5f, cx, cy + s * 0.5f, color, kW);
            p.Line(cx, cy + s * 0.5f, cx + s, cy - s * 0.5f, color, kW);
            break;
        case Icon::ChevronUp:
            p.Line(cx - s, cy + s * 0.5f, cx, cy - s * 0.5f, color, kW);
            p.Line(cx, cy - s * 0.5f, cx + s, cy + s * 0.5f, color, kW);
            break;
        case Icon::ChevronRight:
            p.Line(cx - s * 0.5f, cy - s, cx + s * 0.5f, cy, color, kW);
            p.Line(cx + s * 0.5f, cy, cx - s * 0.5f, cy + s, color, kW);
            break;
        case Icon::ChevronLeft:
            p.Line(cx + s * 0.5f, cy - s, cx - s * 0.5f, cy, color, kW);
            p.Line(cx - s * 0.5f, cy, cx + s * 0.5f, cy + s, color, kW);
            break;
        case Icon::Check:
            p.Line(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, color, 2.f);
            p.Line(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.8f, color, 2.f);
            break;
        case Icon::Plus:
            p.Line(cx - s, cy, cx + s, cy, color, kW);
            p.Line(cx, cy - s, cx, cy + s, color, kW);
            break;
        case Icon::Grip:  // something to hold and drag: three short lines
            for (int i = -1; i <= 1; ++i) p.Line(cx - s, cy + static_cast<float>(i) * s * 0.75f, cx + s, cy + static_cast<float>(i) * s * 0.75f, color, 1.3f);
            break;
        case Icon::Object: {
            const float rr = std::min(r.w, r.h) * 0.27f;
            p.StrokeRoundRect({cx - rr, cy - rr, rr * 2, rr * 2}, rr, color, 1.3f);
            break;
        }
        case Icon::Play: {  // a triangle pointing right
            ui::Path tri;
            tri.Begin({cx - s * 0.8f, cy - s * 1.1f});
            tri.Line({cx + s * 1.1f, cy});
            tri.Line({cx - s * 0.8f, cy + s * 1.1f});
            p.FillPath(tri, color);
            break;
        }
        case Icon::Pause:  // two bars
            p.FillRect({cx - s * 0.95f, cy - s * 1.05f, s * 0.7f, s * 2.1f}, color);
            p.FillRect({cx + s * 0.25f, cy - s * 1.05f, s * 0.7f, s * 2.1f}, color);
            break;
        case Icon::Step: {  // a smaller triangle and a bar: one frame on
            ui::Path tri;
            tri.Begin({cx - s * 1.0f, cy - s * 1.0f});
            tri.Line({cx + s * 0.5f, cy});
            tri.Line({cx - s * 1.0f, cy + s * 1.0f});
            p.FillPath(tri, color);
            p.FillRect({cx + s * 0.6f, cy - s * 1.0f, s * 0.5f, s * 2.0f}, color);
            break;
        }
        case Icon::Stop:  // a square
            p.FillRect({cx - s * 0.9f, cy - s * 0.9f, s * 1.8f, s * 1.8f}, color);
            break;
        case Icon::Folder: {
            // A folder from the side: its body and the tab on its upper left.
            const float h = std::min(r.w, r.h) * 0.34f;
            const float x0 = cx - h * 1.1f, x1 = cx + h * 1.1f, y0 = cy - h * 0.55f, y1 = cy + h * 0.8f;
            p.StrokeRoundRect({x0, y0, x1 - x0, y1 - y0}, 1.5f, color, 1.3f);
            p.Line(x0, y0 + 0.5f, x0 + h * 0.15f, y0 - h * 0.35f, color, 1.3f);
            p.Line(x0 + h * 0.15f, y0 - h * 0.35f, x0 + h * 0.85f, y0 - h * 0.35f, color, 1.3f);
            p.Line(x0 + h * 0.85f, y0 - h * 0.35f, x0 + h * 1.05f, y0, color, 1.3f);
            break;
        }
        case Icon::File:
        case Icon::Text: {
            // A page with its upper right corner folded (+ lines of text).
            const float h = std::min(r.w, r.h) * 0.36f;
            const float x0 = cx - h * 0.72f, x1 = cx + h * 0.72f, y0 = cy - h, y1 = cy + h, f = h * 0.5f;
            p.Line(x0, y0, x1 - f, y0, color, 1.3f);
            p.Line(x1 - f, y0, x1, y0 + f, color, 1.3f);
            p.Line(x1, y0 + f, x1, y1, color, 1.3f);
            p.Line(x1, y1, x0, y1, color, 1.3f);
            p.Line(x0, y1, x0, y0, color, 1.3f);
            p.Line(x1 - f, y0, x1 - f, y0 + f, color, 1.1f);
            p.Line(x1 - f, y0 + f, x1, y0 + f, color, 1.1f);
            if (icon == Icon::Text) {
                for (int i = 0; i < 3; ++i) {
                    const float ly = cy - h * 0.05f + static_cast<float>(i) * h * 0.38f;
                    p.Line(x0 + h * 0.3f, ly, x1 - h * 0.3f, ly, color, 1.1f);
                }
            }
            break;
        }
        case Icon::Image: {
            // A picture: its frame, a mountain and the sun.
            const float h = std::min(r.w, r.h) * 0.36f;
            const float x0 = cx - h * 1.05f, x1 = cx + h * 1.05f, y0 = cy - h * 0.8f, y1 = cy + h * 0.8f;
            p.StrokeRoundRect({x0, y0, x1 - x0, y1 - y0}, 1.5f, color, 1.3f);
            p.Line(x0 + h * 0.2f, y1 - h * 0.2f, cx - h * 0.25f, cy - h * 0.05f, color, 1.3f);
            p.Line(cx - h * 0.25f, cy - h * 0.05f, cx + h * 0.2f, y1 - h * 0.55f, color, 1.3f);
            p.Line(cx + h * 0.2f, y1 - h * 0.55f, cx + h * 0.45f, cy + h * 0.15f, color, 1.3f);
            p.Line(cx + h * 0.45f, cy + h * 0.15f, x1 - h * 0.2f, y1 - h * 0.2f, color, 1.3f);
            p.FillCircle(cx + h * 0.5f, cy - h * 0.35f, h * 0.17f, color);
            break;
        }
        case Icon::Audio: {
            // A note: its head, stem and flag.
            const float h = std::min(r.w, r.h) * 0.36f;
            const float sx = cx + h * 0.3f;
            p.FillCircle(cx - h * 0.05f, cy + h * 0.62f, h * 0.36f, color);
            p.Line(sx, cy + h * 0.6f, sx, cy - h, color, 1.3f);
            p.Line(sx, cy - h, sx + h * 0.55f, cy - h * 0.55f, color, 1.3f);
            break;
        }
        case Icon::Font: {
            // The letter A.
            const float h = std::min(r.w, r.h) * 0.36f;
            p.Line(cx - h * 0.75f, cy + h, cx, cy - h, color, 1.4f);
            p.Line(cx, cy - h, cx + h * 0.75f, cy + h, color, 1.4f);
            p.Line(cx - h * 0.4f, cy + h * 0.25f, cx + h * 0.4f, cy + h * 0.25f, color, 1.3f);
            break;
        }
        case Icon::Camera: {
            // A film camera from the side: the body and the lens's cone on its right.
            const float h = std::min(r.w, r.h) * 0.36f;
            const float bx = cx - h * 1.0f, bw = h * 1.25f, by = cy - h * 0.55f, bh = h * 1.1f;
            p.StrokeRoundRect({bx, by, bw, bh}, h * 0.15f, color, 1.3f);
            ui::Path lens;
            lens.Begin({bx + bw, cy - h * 0.18f});
            lens.Line({cx + h * 0.95f, cy - h * 0.55f});
            lens.Line({cx + h * 0.95f, cy + h * 0.55f});
            lens.Line({bx + bw, cy + h * 0.18f});
            p.FillPath(lens, color);
            break;
        }
        case Icon::Light: {
            // A sun: a round middle and eight short rays.
            const float h = std::min(r.w, r.h) * 0.38f;
            p.FillCircle(cx, cy, h * 0.42f, color);
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 0.785398f;
                const float ca = std::cos(a), sa = std::sin(a);
                p.Line(cx + ca * h * 0.66f, cy + sa * h * 0.66f, cx + ca * h, cy + sa * h, color, 1.3f);
            }
            break;
        }
        case Icon::Square: {
            // A thing on a plane: a square, a little turned (not a box of the UI).
            const float h = std::min(r.w, r.h) * 0.36f;
            const float x[4] = {cx, cx + h, cx, cx - h};
            const float y[4] = {cy - h, cy, cy + h, cy};
            for (int i = 0; i < 4; ++i) p.Line(x[i], y[i], x[(i + 1) % 4], y[(i + 1) % 4], color, 1.3f);
            p.FillCircle(cx, cy, h * 0.18f, color);
            break;
        }
        case Icon::Cube: {
            // Seen from above a corner: the outline (a hexagon) and the three edges of the near corner.
            const float h = std::min(r.w, r.h) * 0.36f, k = h * 0.866f;
            const float x[6] = {cx, cx + k, cx + k, cx, cx - k, cx - k};
            const float y[6] = {cy - h, cy - h * 0.5f, cy + h * 0.5f, cy + h, cy + h * 0.5f, cy - h * 0.5f};
            for (int i = 0; i < 6; ++i) p.Line(x[i], y[i], x[(i + 1) % 6], y[(i + 1) % 6], color, 1.3f);
            p.Line(cx, cy, x[1], y[1], color, 1.3f);
            p.Line(cx, cy, x[5], y[5], color, 1.3f);
            p.Line(cx, cy, x[3], y[3], color, 1.3f);
            break;
        }
    }
}

bool Button(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view label, ButtonKind kind, bool enabled) {
    auto& p = c.P();
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;

    Color fg = theme::Text;
    switch (kind) {
        case ButtonKind::Primary:
            DrawSurface(c, r, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent);
            fg = theme::OnAccent();
            break;
        case ButtonKind::Secondary:
            DrawSurface(c, r, down ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised);
            break;
        case ButtonKind::Ghost:
            if (b.hovered) DrawSurface(c, r, down ? Surface::Pressed : Surface::Hovered);
            fg = b.hovered ? theme::Text : theme::TextDim;
            break;
    }
    if (!enabled) {
        // A disabled button is the same box, faded over with the background.
        p.FillRoundRect(r, theme::Radius, theme::Bg.Alpha(0.55f));
        fg = theme::TextFaint;
    }
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.bold = kind == ButtonKind::Primary;
    ts.h = ui::HAlign::Center;
    ts.color = fg;
    p.Text(r.Inset(6, down ? 1.f : 0.f, 6, down ? -1.f : 0.f), label, ts);
    return enabled && b.clicked;
}

int Segmented(ui::Context& c, std::string_view idStr, const Rect& r, std::span<const std::wstring_view> labels, int selected,
              std::span<const std::wstring_view> tooltips) {
    if (labels.empty()) return -1;
    DrawSurface(c, r, Surface::Popup);
    const float w = (r.w - 4.f) / static_cast<float>(labels.size());
    int clicked = -1;
    for (size_t i = 0; i < labels.size(); ++i) {
        const Rect seg{r.x + 2.f + w * static_cast<float>(i), r.y + 2.f, w, r.h - 4.f};
        const auto b = c.ButtonBehavior(c.MakeId(std::string(idStr) + "#" + std::to_string(i)), seg);
        const bool on = static_cast<int>(i) == selected;
        const bool down = b.held && b.hovered;
        if (on) {
            DrawSurface(c, seg, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent, theme::Radius - 1.f);
        } else if (b.hovered) {
            DrawSurface(c, seg, down ? Surface::Pressed : Surface::Hovered, theme::Radius - 1.f);
        }
        ui::TextStyle ts;
        ts.size = 12;
        ts.bold = on;
        ts.h = ui::HAlign::Center;
        ts.color = on ? theme::OnAccent() : b.hovered ? theme::Text : theme::TextDim;
        c.P().Text(seg.Inset(4, 0, 4, 0), labels[i], ts);
        if (b.hovered && i < tooltips.size() && !tooltips[i].empty()) Tooltip(c, tooltips[i]);
        if (b.clicked) clicked = static_cast<int>(i);
    }
    return clicked;
}

bool IconButton(ui::Context& c, std::string_view idStr, const Rect& r, Icon icon, bool enabled) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;
    if (b.hovered) DrawSurface(c, r, down ? Surface::Pressed : Surface::Hovered);
    DrawIcon(c, r, icon, !enabled ? theme::TextFaint : b.hovered ? theme::Text : theme::TextDim);
    return enabled && b.clicked;
}

bool IconToggle(ui::Context& c, std::string_view idStr, const Rect& r, Icon icon, bool on, bool enabled, std::wstring_view tooltip) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;
    if (on) {
        DrawSurface(c, r, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent);
    } else {
        DrawSurface(c, r, down ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised);
    }
    DrawIcon(c, r, icon, on ? theme::OnAccent() : !enabled ? theme::TextFaint : b.hovered ? theme::Text : theme::TextDim);
    if (b.hovered && !tooltip.empty()) Tooltip(c, tooltip);
    return enabled && b.clicked;
}

void DrawRow(ui::Context& c, const Rect& r, RowLook look) {
    auto& p = c.P();
    const float radius = look.flat ? 0.f : 5.f;
    if (look.selected) {
        const theme::Gradient g = theme::Raised(look.focused ? theme::Selected : theme::Pressed);
        p.FillRoundRectGradient(r, radius, g.top, g.bottom);
        TopLight(p, r, radius, theme::Highlight());
        if (look.focused) {
            // The accent bar: on the edge of a flat row, inside a rounded one.
            const theme::Gradient a = theme::AccentOf(theme::Accent);
            const Rect bar = look.flat ? Rect{r.x, r.y, 3, r.h} : Rect{r.x, r.y + 4, 3, r.h - 8};
            p.FillRoundRectGradient(bar, look.flat ? 0.f : 1.5f, a.top, a.bottom);
        }
    } else if (look.hovered) {
        const theme::Gradient g = theme::Gentle(theme::Hover);
        p.FillRoundRectGradient(r, radius, g.top, g.bottom);
    }
}

ui::Context::ButtonResult Selectable(ui::Context& c, std::string_view idStr, const Rect& r, bool selected, bool flat) {
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    RowLook look;
    look.selected = selected;
    look.hovered = b.hovered;
    look.flat = flat;
    DrawRow(c, r, look);
    return b;
}

namespace {
// A tiny editor window in a palette's own colors (not the palette in use): title bar, sidebar with
// a selected item, a panel with lines of text and an accent button.
void ThemePreview(ui::Painter& p, const Rect& r, const theme::Palette& k) {
    constexpr float R = 5.f, barH = 12.f;
    p.FillRoundRect(r, R, k.bg);
    p.FillRoundRectGradient({r.x, r.y, r.w, barH}, ui::Corners{R, R, 0, 0}, k.sidebar, k.sidebar);
    const float sideW = std::round(r.w * 0.26f);
    const Rect side{r.x, r.y + barH, sideW, r.h - barH};
    p.FillRoundRectGradient(side, ui::Corners{0, 0, 0, R}, k.sidebar, k.sidebar);
    float sy = side.y + 10;
    for (int i = 0; i < 3; ++i) {
        const Rect item{side.x + 6, sy, side.w - 12, 9};
        if (i == 1) {
            p.FillRoundRect(item, 2, k.selected);
            p.FillRect({item.x, item.y, 2, item.h}, k.accent);
        }
        p.FillRoundRect({item.x + 5, item.y + 3, (item.w - 10) * (i == 1 ? 0.8f : 0.6f), 3}, 1.5f, i == 1 ? k.text : k.textDim);
        sy += 15;
    }
    const Rect panel{side.Right() + 8, r.y + barH + 8, r.Right() - side.Right() - 16, r.h - barH - 16};
    if (panel.w <= 0 || panel.h <= 0) return;
    p.FillRoundRect(panel, 4, k.panel);
    p.StrokeRoundRect(panel, 4, k.border);
    p.FillRoundRect({panel.x + 8, panel.y + 9, panel.w * 0.55f, 4}, 2, k.text);
    p.FillRoundRect({panel.x + 8, panel.y + 19, panel.w * 0.42f, 3}, 1.5f, k.textDim);
    p.FillRoundRect({panel.x + 8, panel.y + 27, panel.w * 0.34f, 3}, 1.5f, k.textFaint);
    p.FillRoundRect({panel.x + 8, panel.Bottom() - 18, std::min(44.f, panel.w - 16), 11}, 3, k.accent);
}
}  // namespace

ui::Context::ButtonResult ThemeCard(ui::Context& c, std::string_view idStr, const Rect& r, const theme::Palette* palette,
                                    std::wstring_view name, bool selected) {
    auto& p = c.P();
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    DrawSurface(c, r, b.hovered && !selected ? Surface::Hovered : Surface::Panel, 8.f);
    const Rect pr{r.x + 10, r.y + 10, r.w - 20, r.h - 20 - 30};
    if (palette) {
        ThemePreview(p, pr, *palette);
    } else {
        // Follows Windows: dark on the left, light on the right.
        const float half = std::round(pr.w * 0.5f);
        p.PushClip({pr.x, pr.y, half, pr.h});
        ThemePreview(p, pr, theme::kDark);
        p.PopClip();
        p.PushClip({pr.x + half, pr.y, pr.w - half, pr.h});
        ThemePreview(p, pr, theme::kLight);
        p.PopClip();
    }
    ui::TextStyle ts;
    ts.size = 13;
    ts.bold = selected;
    ts.color = theme::Text;
    const Rect label{r.x + 12, r.Bottom() - 34, r.w - 24 - 20, 28};
    p.Text(label, name, ts);
    if (selected) {
        DrawIcon(c, {r.Right() - 12 - 16, label.y + 6, 16, 16}, Icon::Check, theme::Accent);
        p.StrokeRoundRect(r.Inset(-1), 9.f, theme::Accent, 2.f);
    }
    return b;
}

bool NavItem(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view label, bool selected) {
    const auto b = Selectable(c, idStr, r, selected, true);
    ui::TextStyle ts;
    ts.size = 15;
    ts.bold = selected;
    ts.color = selected || b.hovered ? theme::Text : theme::TextDim;
    c.P().Text(r.Inset(24, 0, 12, 0), label, ts);
    return b.clicked;
}

void DrawTab(ui::Context& c, const Rect& r, bool selected, bool hovered) {
    auto& p = c.P();
    if (selected) {
        const theme::Gradient g = theme::Raised(theme::PanelHeader);
        p.FillRoundRectGradient(r, theme::Radius, g.top, g.bottom);
        TopLight(p, r, theme::Radius, theme::Highlight());
        const theme::Gradient a = theme::AccentOf(theme::Accent);
        p.FillRoundRectGradient({r.x + 8, r.Bottom() - 2.5f, r.w - 16, 2.5f}, 1.25f, a.top, a.bottom);
    } else if (hovered) {
        const theme::Gradient g = theme::Gentle(theme::Hover);
        p.FillRoundRectGradient(r, theme::Radius, g.top, g.bottom);
    }
}

bool CollapsingHeader(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view title, bool& open, float reserveRight) {
    const Rect hit{r.x, r.y, r.w - reserveRight, r.h};
    const auto b = c.ButtonBehavior(c.MakeId(idStr), hit);
    DrawSurface(c, r, b.held && b.hovered ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised, 5.f);
    DrawIcon(c, {r.x + 4, r.y, 18, r.h}, open ? Icon::ChevronDown : Icon::ChevronRight, b.hovered ? theme::Text : theme::TextDim);
    ui::TextStyle ts;
    ts.size = 13;
    ts.bold = true;
    ts.color = theme::Text;
    c.P().Text({r.x + 26, r.y, r.w - 26 - reserveRight - 6, r.h}, title, ts);
    if (b.clicked) {
        open = !open;
        return true;
    }
    return false;
}

ui::Context::ButtonResult ComboBox(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view text, bool enabled,
                                   std::optional<Icon> icon) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    DrawSurface(c, r, b.hovered ? Surface::Hovered : Surface::Raised);
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.color = enabled ? theme::Text : theme::TextFaint;
    float left = 8;
    if (icon) {
        DrawIcon(c, {r.x + 5, r.y, 18, r.h}, *icon, *icon == Icon::Folder ? theme::Folder : theme::TextDim);
        left = 26;
    }
    c.P().Text(r.Inset(left, 0, 24, 0), text, ts);
    DrawIcon(c, {r.Right() - 22, r.y, 18, r.h}, Icon::ChevronDown, b.hovered ? theme::Text : theme::TextDim);
    return b;
}

void Separator(ui::Context& c, float x, float y, float width) { c.P().FillRect({x, y, width, 1}, theme::Border); }

void Tooltip(ui::Context& c, std::wstring_view text) {
    const std::wstring tip(text);
    c.AddOverlay([&c, tip] {
        auto& p = c.P();
        const float wdt = std::min(420.f, p.TextWidth(tip, 12) + 20);
        const Rect r{c.In().mouseX + 14, c.In().mouseY + 18, wdt, 28};
        DrawSurface(c, r, Surface::Popup, 5.f);
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::Text;
        p.Text(r.Inset(10, 0, 10, 0), tip, ts);
    });
}

bool TextField(ui::Context& c, std::string_view idStr, const Rect& r, std::string& value, std::wstring_view placeholder,
               bool enabled, bool selectAllOnFocus) {
    auto& p = c.P();
    const auto& in = c.In();
    const ui::Id id = c.MakeId(idStr);
    const float fs = theme::FontSize;
    const Rect inner = r.Inset(8, 0, 8, 0);
    float& scroll = c.FloatState(id ^ 0x5C5C5C5C5C5C5C5Cull);

    const bool hovered = enabled && c.IsHovered(r);
    if (hovered) c.SetCursor(ui::Cursor::IBeam);
    bool focused = c.Focused() == id;
    if (focused && (!c.InputEnabled() || !enabled)) {
        c.ClearFocus();  // an overlay opened above this field
        focused = false;
    }

    // 1) Typed text first: a click that moves the focus away in this same frame must not
    //    drop what was typed (or committed by the IME) just before it.
    bool changed = false;
    if (focused) {
        auto& e = c.FocusedEdit();
        bool edited = false;
        for (const auto& k : in.keys) {
            switch (k.key) {
                case ui::Key::Left: e.MoveLeft(k.shift, k.ctrl); break;
                case ui::Key::Right: e.MoveRight(k.shift, k.ctrl); break;
                case ui::Key::Home: e.Home(k.shift); break;
                case ui::Key::End: e.End(k.shift); break;
                case ui::Key::Backspace: edited |= e.Backspace(k.ctrl); break;
                case ui::Key::Delete: edited |= e.Delete(k.ctrl); break;
                case ui::Key::A:
                    if (k.ctrl) e.SelectAll();
                    break;
                case ui::Key::C:
                    if (k.ctrl && e.HasSelection() && c.setClipboardText) c.setClipboardText(e.SelectedText());
                    break;
                case ui::Key::X:
                    if (k.ctrl && e.HasSelection()) {
                        if (c.setClipboardText) c.setClipboardText(e.SelectedText());
                        edited |= e.Insert(L"");
                    }
                    break;
                case ui::Key::V:
                    if (k.ctrl && c.getClipboardText) edited |= e.Insert(c.getClipboardText());
                    break;
                default: break;
            }
            c.ResetCaretBlink();
        }
        if (!in.chars.empty()) edited |= e.Insert(in.chars);
        if (!in.imeResult.empty()) edited |= e.Insert(in.imeResult);
        if (edited) {
            c.ResetCaretBlink();
            std::string nv = WideToUtf8(e.Text());
            if (nv != value) {
                value = std::move(nv);
                changed = true;
            }
        }
    }

    // 2) Clicks: focus / caret placement, or losing the focus. While the left button is held after a
    //    press in the field, the selection follows the mouse (from where it was pressed).
    //    drag: 0 = none, 1 = selecting, 2 = pressed while entering the field (select all on release
    //    unless the mouse moved: selectAllOnFocus).
    float& drag = c.FloatState(id ^ 0xD1A6D1A6D1A6D1A6ull);
    float& pressX = c.FloatState(id ^ 0x9E55E59E55E59E55ull);
    if (hovered && c.MousePressed()) {
        const bool entering = !focused;
        if (!focused) {
            c.FocusText(id, Utf8ToWide(value), false);
            focused = true;
        }
        auto& e = c.FocusedEdit();
        e.SetCaret(p.HitTest(e.Text(), in.mouseX - inner.x + scroll, fs), in.shift);
        if (in.doubleClicked) e.SelectAll();
        drag = entering && selectAllOnFocus && !in.shift && !in.doubleClicked ? 2.f : in.doubleClicked ? 0.f : 1.f;
        pressX = in.mouseX;
        if (drag == 2.f) e.SelectAll();  // shown at once; a drag replaces it with what it covers
        c.ResetCaretBlink();
    } else if (focused && !c.FocusIsNew() && (in.pressed[0] || in.pressed[1]) && !r.Contains(in.mouseX, in.mouseY)) {
        c.ClearFocus();
        focused = false;
        drag = 0;
    } else if (focused && drag != 0 && in.down[0]) {
        if (drag == 1.f || std::fabs(in.mouseX - pressX) >= 3.f) {
            auto& e = c.FocusedEdit();
            if (drag == 2.f) e.SetCaret(p.HitTest(e.Text(), pressX - inner.x + scroll, fs), false);  // the drag starts where it was pressed
            drag = 1.f;
            e.SetCaret(p.HitTest(e.Text(), in.mouseX - inner.x + scroll, fs), true);
            c.ResetCaretBlink();
        }
    }
    if (drag != 0 && (!in.down[0] || !focused)) drag = 0;

    // 3) What to draw.
    std::wstring display;
    size_t caret = 0, selA = 0, selB = 0, compStart = 0, compLen = 0;
    if (focused) {
        c.MarkFocusAlive();
        auto& e = c.FocusedEdit();
        display = e.Text();
        caret = e.Caret();
        selA = e.SelStart();
        selB = e.SelEnd();
        if (!in.imeComposition.empty()) {
            compStart = caret;
            compLen = in.imeComposition.size();
            display.insert(caret, in.imeComposition);
            caret = compStart + std::clamp<size_t>(static_cast<size_t>(std::max(0, in.imeCursor)), 0, compLen);
            selA = selB = 0;
        }
    } else {
        display = Utf8ToWide(value);
    }

    // Frame
    if (focused) p.StrokeRoundRect(r.Inset(-2), theme::Radius + 2, theme::Accent.Alpha(0.25f), 2.f);  // a soft ring
    DrawSurface(c, r, Surface::Sunken);
    if (focused || hovered) p.StrokeRoundRect(r, theme::Radius, focused ? theme::Accent : theme::BorderHover);
    if (!enabled) p.FillRoundRect(r, theme::Radius, theme::Bg.Alpha(0.45f));

    // Keep the caret visible.
    const float caretX = p.CaretX(display, caret, fs);
    const float textW = p.TextWidth(display, fs);
    if (focused) {
        if (caretX - scroll > inner.w - 2) scroll = caretX - inner.w + 2;
        if (caretX - scroll < 0) scroll = caretX;
        scroll = std::clamp(scroll, 0.f, std::max(0.f, textW - inner.w + 2));
    } else {
        scroll = 0;
    }

    c.PushClip(inner);
    if (display.empty()) {
        ui::TextStyle ps;
        ps.size = fs;
        ps.color = theme::TextFaint;
        p.Text(inner, placeholder, ps);
    } else {
        if (focused && selA != selB) {
            const float x0 = p.CaretX(display, selA, fs), x1 = p.CaretX(display, selB, fs);
            p.FillRect({inner.x - scroll + x0, r.y + 6, x1 - x0, r.h - 12}, theme::Accent.Alpha(0.45f));
        }
        ui::TextStyle ts;
        ts.size = fs;
        ts.ellipsis = !focused;
        ts.color = enabled ? theme::Text : theme::TextDim;
        const Rect tr = focused ? Rect{inner.x - scroll, r.y, std::max(inner.w + scroll, textW + 4), r.h} : inner;
        p.Text(tr, display, ts);
        if (compLen > 0) {
            const float x0 = p.CaretX(display, compStart, fs), x1 = p.CaretX(display, compStart + compLen, fs);
            p.FillRect({inner.x - scroll + x0, r.Bottom() - 8, x1 - x0, 1.5f}, theme::Text);
        }
    }
    if (focused) {
        const Rect cr{inner.x - scroll + caretX, r.y + 7, 1.5f, r.h - 14};
        if (c.CaretVisible()) p.FillRect(cr, theme::Text);
        c.SetTextInput(cr);
    }
    c.PopClip();
    return changed;
}

bool Checkbox(ui::Context& c, std::string_view idStr, const Rect& r, bool& value, std::wstring_view label) {
    auto& p = c.P();
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    const Rect box{r.x, r.y + (r.h - 16) * 0.5f, 16, 16};
    DrawSurface(c, box, value ? (b.hovered ? Surface::AccentHovered : Surface::Accent) : Surface::Sunken, 3.f);
    if (!value && b.hovered) p.StrokeRoundRect(box, 3, theme::BorderHover);
    if (value) DrawIcon(c, box, Icon::Check, theme::OnAccent());
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.color = theme::Text;
    p.Text({box.Right() + 8, r.y, r.w - 24, r.h}, label, ts);
    if (b.clicked) {
        value = !value;
        return true;
    }
    return false;
}

namespace {
constexpr float kTrackW = 40.f, kLabelGap = 8.f;
}  // namespace

float SwitchWidth(ui::Context& c, std::wstring_view offLabel, std::wstring_view onLabel) {
    float w = kTrackW;
    if (!offLabel.empty()) w += c.P().TextWidth(offLabel, theme::FontSize) + kLabelGap;
    if (!onLabel.empty()) w += c.P().TextWidth(onLabel, theme::FontSize) + kLabelGap;
    return w;
}

bool Switch(ui::Context& c, std::string_view idStr, float x, float y, bool& on, std::wstring_view offLabel, std::wstring_view onLabel) {
    auto& p = c.P();
    const ui::Id id = c.MakeId(idStr);
    const Rect all{x, y, SwitchWidth(c, offLabel, onLabel), kSwitchH};
    const auto b = c.ButtonBehavior(id, all);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    if (b.clicked) on = !on;

    // Where the knob is: 0 = left (off) .. 1 = right (on). It slides there over a few frames.
    float& pos = c.FloatState(id ^ 0x5717C85717C85717ull, on ? 1.f : 0.f);
    const float target = on ? 1.f : 0.f;
    if (pos != target) {
        pos = target > pos ? std::min(target, pos + 0.2f) : std::max(target, pos - 0.2f);
        c.RequestAnimation();
    }

    ui::TextStyle ts;
    ts.size = theme::FontSize;
    float cx = x;
    if (!offLabel.empty()) {
        const float tw = p.TextWidth(offLabel, ts.size);
        ts.color = !on ? theme::Text : theme::TextFaint;
        ts.bold = !on;
        p.Text({cx, y, tw + 2, kSwitchH}, offLabel, ts);
        cx += tw + kLabelGap;
    }
    const Rect track{cx, y, kTrackW, kSwitchH};
    const float r = kSwitchH * 0.5f;
    // The track turns from grey to the accent as the knob goes right.
    p.FillRoundRect(track, r, Mix(theme::SwitchOff(), b.hovered ? theme::AccentHover : theme::Accent, pos));
    if (pos < 1.f) p.StrokeRoundRect(track, r, theme::Border.Alpha(1.f - pos));
    const float knobR = r - 2.f;
    const float kx = track.x + r + (track.w - 2.f * r) * pos;
    p.FillCircle(kx, track.y + r + 0.8f, knobR, theme::Shadow.Alpha(0.35f));  // a soft shade under it
    p.FillCircle(kx, track.y + r, knobR, theme::SwitchKnob());
    cx += kTrackW + kLabelGap;
    if (!onLabel.empty()) {
        const float tw = p.TextWidth(onLabel, ts.size);
        ts.color = on ? theme::Text : theme::TextFaint;
        ts.bold = on;
        p.Text({cx, y, tw + 2, kSwitchH}, onLabel, ts);
    }
    return b.clicked;
}

Rect Panel(ui::Context& c, const Rect& r, std::wstring_view title) {
    auto& p = c.P();
    DrawSurface(c, r, Surface::Panel, 6.f);
    Rect body = r;
    const Rect header = ui::TakeTop(body, 30);
    const theme::Gradient hg = theme::Raised(theme::PanelHeader);
    p.FillRoundRectGradient(header.Inset(1, 1, 1, 0), 5.f, hg.top, hg.bottom);
    ui::TextStyle ts;
    ts.size = 12;
    ts.bold = true;
    ts.color = theme::TextDim;
    p.Text(header.Inset(12, 0, 12, 0), title, ts);
    p.FillRect({r.x, header.Bottom() - 1, r.w, 1}, theme::Border);
    return body;
}

float BeginScroll(ui::Context& c, std::string_view idStr, const Rect& view, float contentHeight) {
    float& off = c.FloatState(c.MakeId(idStr));
    const float maxOff = std::max(0.f, contentHeight - view.h);
    if (c.IsHovered(view) && c.In().wheel != 0.f) off -= c.In().wheel * 60.f;
    off = std::clamp(off, 0.f, maxOff);
    c.PushClip(view);
    return off;
}

void EndScroll(ui::Context& c, std::string_view idStr, const Rect& view, float contentHeight) {
    c.PopClip();
    if (contentHeight <= view.h) return;
    auto& p = c.P();
    const ui::Id id = c.MakeId(idStr);
    float& off = c.FloatState(id);
    const float maxOff = contentHeight - view.h;
    const Rect track{view.Right() - 9, view.y + 3, 6, view.h - 6};
    const float thumbH = std::max(28.f, track.h * view.h / contentHeight);
    const float travel = std::max(1.f, track.h - thumbH);
    const Rect thumb{track.x, track.y + travel * (off / maxOff), track.w, thumbH};

    const ui::Id tid = id ^ 0x7475786275756D74ull;
    float& grab = c.FloatState(tid);
    const auto b = c.ButtonBehavior(tid, thumb.Inset(-3));
    if (b.hovered && c.MousePressed()) grab = c.In().mouseY - thumb.y;
    if (c.Active() == tid && c.In().down[0]) {
        const float nv = std::clamp((c.In().mouseY - grab - track.y) / travel * maxOff, 0.f, maxOff);
        if (nv != off) {
            off = nv;
            c.RequestRedraw();
        }
    }
    const bool hot = b.hovered || c.Active() == tid;
    const theme::Gradient tg = theme::Raised(hot ? theme::ScrollThumbHot : theme::ScrollThumb);
    p.FillRoundRectGradient(thumb, 3, tg.top, tg.bottom);
}

void ScrollToEnd(ui::Context& c, std::string_view idStr) { c.FloatState(c.MakeId(idStr)) = 1e9f; }

namespace {
constexpr float kItemH = 28.f;
constexpr float kSepH = 9.f;
constexpr float kPad = 5.f;
}  // namespace

Rect MenuRect(const ui::Context& c, const Popup& popup, const std::vector<MenuItem>& items, float width) {
    float h = kPad * 2;
    for (const auto& it : items) h += it.separator ? kSepH : kItemH;
    const Rect& vp = c.Viewport();
    const float x = std::clamp(popup.x, vp.x + 4, std::max(vp.x + 4, vp.Right() - width - 4));
    const float y = std::clamp(popup.y, vp.y + 4, std::max(vp.y + 4, vp.Bottom() - h - 4));
    return {x, y, width, h};
}

int MenuPopup(ui::Context& c, Popup& popup, const std::vector<MenuItem>& items, float width) {
    if (!popup.open) return -1;
    auto& p = c.P();
    const auto& in = c.In();
    const Rect box = MenuRect(c, popup, items, width);

    const bool fresh = popup.fresh;
    popup.fresh = false;
    if (!fresh && (in.pressed[0] || in.pressed[1]) && !box.Contains(in.mouseX, in.mouseY)) {
        popup.open = false;
        c.RequestRedraw();
        return -1;
    }
    if (c.KeyPressed(ui::Key::Escape)) {
        popup.open = false;
        c.RequestRedraw();
        return -1;
    }

    DrawSurface(c, box, Surface::Popup, 6.f);

    int clicked = -1;
    float y = box.y + kPad;
    const bool checks = std::any_of(items.begin(), items.end(), [](const MenuItem& it) { return it.checked; });
    c.PushId("menu");
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.separator) {
            Separator(c, box.x + 8, y + kSepH * 0.5f, box.w - 16);
            y += kSepH;
            continue;
        }
        const Rect row{box.x + kPad, y, box.w - kPad * 2, kItemH};
        c.PushId(static_cast<uint64_t>(i));
        const auto b = it.enabled ? c.ButtonBehavior(c.MakeId("item"), row) : ui::Context::ButtonResult{};
        c.PopId();
        DrawRow(c, row, {false, b.hovered, true});
        ui::TextStyle ts;
        ts.size = theme::FontSize;
        ts.color = it.enabled ? theme::Text : theme::TextFaint;
        if (it.checked) DrawIcon(c, {row.x + 2, row.y, 24, row.h}, Icon::Check, ts.color);
        p.Text(row.Inset(checks ? 28.f : 10.f, 0, 10, 0), it.label, ts);
        if (!it.shortcut.empty()) {
            ts.h = ui::HAlign::Right;
            ts.color = theme::TextFaint;
            p.Text(row.Inset(10, 0, 10, 0), it.shortcut, ts);
        }
        if (b.clicked) clicked = static_cast<int>(i);
        y += kItemH;
    }
    c.PopId();
    if (clicked >= 0) {
        popup.open = false;
        c.RequestRedraw();
    }
    return clicked;
}

namespace {
const wchar_t* KeyName(ui::Key k) {
    switch (k) {
        case ui::Key::Delete: return L"Delete";
        case ui::Key::Backspace: return L"Backspace";
        case ui::Key::Enter: return L"Enter";
        case ui::Key::F2: return L"F2";
        case ui::Key::F5: return L"F5";
        case ui::Key::A: return L"A";
        case ui::Key::C: return L"C";
        case ui::Key::D: return L"D";
        case ui::Key::E: return L"E";
        case ui::Key::F: return L"F";
        case ui::Key::Q: return L"Q";
        case ui::Key::W: return L"W";
        case ui::Key::N: return L"N";
        case ui::Key::O: return L"O";
        case ui::Key::P: return L"P";
        case ui::Key::S: return L"S";
        case ui::Key::V: return L"V";
        case ui::Key::X: return L"X";
        case ui::Key::Y: return L"Y";
        case ui::Key::Z: return L"Z";
        default: return L"";
    }
}
}  // namespace

std::wstring ShortcutText(const Action& a) {
    if (a.key == ui::Key::None) return {};
    std::wstring s;
    if (a.ctrl) s += L"Ctrl+";
    if (a.shift) s += L"Shift+";
    return s + KeyName(a.key);
}

void ActionMenu(ui::Context& c, Popup& popup, const std::vector<Action>& actions) {
    if (!popup.open) return;
    Popup* pp = &popup;
    const std::vector<Action>* list = &actions;
    c.AddOverlay([&c, pp, list] {
        std::vector<MenuItem> rows;
        float width = 220;
        for (const Action& a : *list) {
            MenuItem mi;
            mi.label = a.label;
            mi.shortcut = ShortcutText(a);
            mi.enabled = a.enabled && static_cast<bool>(a.run);
            mi.separator = a.separator;
            width = std::max(width, c.P().TextWidth(mi.label, theme::FontSize) + c.P().TextWidth(mi.shortcut, theme::FontSize) + 72);
            rows.push_back(std::move(mi));
        }
        const bool was = c.InputEnabled();
        c.SetInputEnabled(true);
        const int picked = MenuPopup(c, *pp, rows, width);
        c.SetInputEnabled(was);
        if (picked >= 0 && static_cast<size_t>(picked) < list->size()) {
            const auto run = (*list)[static_cast<size_t>(picked)].run;  // a copy: running may rebuild the list
            if (run) run();
        }
    });
}

int PickerPopup(ui::Context& c, Popup& popup, std::string_view idStr, std::string& search, std::span<const PickRow> rows, float width) {
    if (!popup.open) return -1;
    auto& p = c.P();
    const auto& in = c.In();
    constexpr float kSearchH = 38.f, kRowH = 26.f;
    const float listH = kRowH * static_cast<float>(std::clamp<size_t>(rows.size(), 1, 10));
    const float h = kPad * 2 + kSearchH + listH;
    const Rect& vp = c.Viewport();
    const Rect box{std::clamp(popup.x, vp.x + 4, std::max(vp.x + 4, vp.Right() - width - 4)),
                   std::clamp(popup.y, vp.y + 4, std::max(vp.y + 4, vp.Bottom() - h - 4)), width, h};
    c.PushId(idStr);
    const ui::Id searchId = c.MakeId("search");
    auto close = [&] {
        popup.open = false;
        if (c.Focused() == searchId) c.ClearFocus();
        c.RequestRedraw();
    };
    const bool fresh = popup.fresh;
    popup.fresh = false;
    if (fresh) c.FocusText(searchId, Utf8ToWide(search), true);
    if ((!fresh && (in.pressed[0] || in.pressed[1]) && !box.Contains(in.mouseX, in.mouseY)) || c.KeyPressed(ui::Key::Escape)) {
        close();
        c.PopId();
        return -1;
    }
    const bool enter = c.KeyPressed(ui::Key::Enter);

    DrawSurface(c, box, Surface::Popup, 6.f);
    TextField(c, "search", {box.x + kPad + 2, box.y + kPad + 4, box.w - kPad * 2 - 4, kSearchH - 10}, search, Tr(L"検索"));
    const Rect list{box.x + kPad, box.y + kPad + kSearchH, box.w - kPad * 2, listH};
    int clicked = -1;
    if (rows.empty()) {
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextFaint;
        p.Text(list.Inset(10, 0, 10, 0), Tr(L"見つかりません。"), ts);
    }
    const float content = kRowH * static_cast<float>(rows.size());
    const float off = BeginScroll(c, "list", list, content);
    for (size_t i = 0; i < rows.size(); ++i) {
        const Rect row{list.x, list.y + kRowH * static_cast<float>(i) - off, list.w - (content > list.h ? 12.f : 0.f), kRowH};
        if (row.Bottom() < list.y || row.y > list.Bottom()) continue;
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("row"), row);
        c.PopId();
        DrawRow(c, row, {false, b.hovered, true});
        const PickRow& it = rows[i];
        float x = row.x + 8;
        if (it.hasIcon) {
            DrawIcon(c, {x, row.y, 18, row.h}, it.icon, it.icon == Icon::Folder ? theme::Folder : theme::TextDim);
            x += 22;
        }
        ui::TextStyle ts;
        ts.size = theme::FontSize;
        ts.color = theme::Text;
        const float detailW = it.detail.empty() ? 0.f : std::min(row.w * 0.45f, p.TextWidth(it.detail, 11) + 8);
        p.Text({x, row.y, row.Right() - x - detailW - 6, row.h}, it.name, ts);
        if (detailW > 0) {
            ts.size = 11;
            ts.color = theme::TextFaint;
            ts.h = ui::HAlign::Right;
            p.Text({row.Right() - detailW - 6, row.y, detailW, row.h}, it.detail, ts);
        }
        if (b.clicked) clicked = static_cast<int>(i);
    }
    EndScroll(c, "list", list, content);
    if (clicked < 0 && enter && !rows.empty()) clicked = 0;
    if (clicked >= 0) close();
    c.PopId();
    return clicked;
}

namespace {
struct DragState {
    bool active = false;
    DragThing thing;
    bool accepted = false;
};
DragState& Drag() {
    static DragState d;
    return d;
}
}  // namespace

void SetDragThing(DragThing thing) {
    Drag().active = true;
    Drag().thing = std::move(thing);
}

void EndDragThing(ui::Context& c) {
    c.AddOverlay([] {
        Drag().active = false;
        Drag().thing = {};
    });
}

const DragThing* DraggedThing() { return Drag().active ? &Drag().thing : nullptr; }

const DragThing* DropTarget(ui::Context& c, const Rect& r, const std::function<bool(const DragThing&)>& accepts) {
    const DragThing* t = DraggedThing();
    const bool released = c.MouseReleased();
    // (only while the button is held or just let go: a drag whose panel went away leaves nothing behind)
    if (!t || (!c.In().down[0] && !released) || !c.IsHovered(r) || (accepts && !accepts(*t))) return nullptr;
    Drag().accepted = true;
    c.P().StrokeRoundRect(r.Inset(-1), theme::Radius, theme::Accent, 2.f);
    c.RequestRedraw();
    return released ? t : nullptr;  // let go over it
}

bool TakeDragAccepted() {
    const bool a = Drag().accepted;
    Drag().accepted = false;
    return a;
}

Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title) {
    auto& p = c.P();
    const Rect& vp = c.Viewport();
    p.FillRect(vp, theme::Dim);
    width = std::min(width, vp.w - 32);
    height = std::min(height, vp.h - 32);
    const Rect box{vp.x + (vp.w - width) * 0.5f, vp.y + (vp.h - height) * 0.5f, width, height};
    p.Shadow(box, 8, theme::Shadow, 28);
    DrawSurface(c, box, Surface::Panel, 8.f);
    // The title on a raised band, like a window's own title bar.
    const theme::Gradient tg = theme::Raised(theme::PanelHeader);
    p.FillRoundRectGradient({box.x + 1, box.y + 1, box.w - 2, 48}, 7.f, tg.top, tg.bottom);
    p.FillRect({box.x + 1, box.y + 30, box.w - 2, 19}, tg.bottom);  // square bottom corners of the band
    TopLight(p, box, 8.f, theme::Highlight());
    Separator(c, box.x + 1, box.y + 49, box.w - 2);
    ui::TextStyle ts;
    ts.size = 17;
    ts.bold = true;
    ts.color = theme::Text;
    p.Text({box.x + 24, box.y + 9, box.w - 48, 32}, title, ts);
    return box.Inset(24, 58, 24, 20);
}

}  // namespace rm::editor::w
