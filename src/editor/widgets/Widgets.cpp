#include "editor/widgets/Widgets.h"

#include "base/Utf.h"
#include "editor/widgets/Theme.h"

#include <algorithm>

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
            FillGradient(p, r, radius, theme::Gentle(theme::Panel));
            p.StrokeRoundRect(r, radius, theme::Border);
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

void DrawBar(ui::Context& c, const Rect& r, Color base) {
    const theme::Gradient g = theme::Gentle(base);
    c.P().FillRectGradient(r, g.top, g.bottom);
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
        case Icon::Check:
            p.Line(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, color, 2.f);
            p.Line(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.8f, color, 2.f);
            break;
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

bool IconButton(ui::Context& c, std::string_view idStr, const Rect& r, Icon icon, bool enabled) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;
    if (b.hovered) DrawSurface(c, r, down ? Surface::Pressed : Surface::Hovered);
    DrawIcon(c, r, icon, !enabled ? theme::TextFaint : b.hovered ? theme::Text : theme::TextDim);
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

ui::Context::ButtonResult ComboBox(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view text, bool enabled) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    DrawSurface(c, r, b.hovered ? Surface::Hovered : Surface::Raised);
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.color = enabled ? theme::Text : theme::TextFaint;
    c.P().Text(r.Inset(8, 0, 24, 0), text, ts);
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
               bool enabled) {
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

    // 2) Clicks: focus / caret placement, or losing the focus.
    if (hovered && c.MousePressed()) {
        if (!focused) {
            c.FocusText(id, Utf8ToWide(value), false);
            focused = true;
        }
        auto& e = c.FocusedEdit();
        e.SetCaret(p.HitTest(e.Text(), in.mouseX - inner.x + scroll, fs), in.shift);
        if (in.doubleClicked) e.SelectAll();
        c.ResetCaretBlink();
    } else if (focused && !c.FocusIsNew() && (in.pressed[0] || in.pressed[1]) && !r.Contains(in.mouseX, in.mouseY)) {
        c.ClearFocus();
        focused = false;
    }

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
