#include "editor/widgets/Widgets.h"

#include "base/Utf.h"
#include "editor/widgets/Theme.h"

#include <algorithm>

namespace rm::editor::w {

using ui::Color;
using ui::Rect;

bool Button(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view label, ButtonKind kind, bool enabled) {
    auto& p = c.P();
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;

    Color bg{0, 0, 0, 0}, border{0, 0, 0, 0}, fg = theme::Text;
    switch (kind) {
        case ButtonKind::Primary:
            bg = down ? theme::AccentPressed : b.hovered ? theme::AccentHover : theme::Accent;
            fg = Color::Hex(0xFFFFFF);
            break;
        case ButtonKind::Secondary:
            bg = down ? theme::Pressed : b.hovered ? theme::Hover : theme::Panel;
            border = b.hovered ? theme::BorderHover : theme::Border;
            break;
        case ButtonKind::Ghost:
            if (b.hovered) bg = down ? theme::Pressed : theme::Hover;
            fg = b.hovered ? theme::Text : theme::TextDim;
            break;
    }
    if (!enabled) {
        bg = bg.Alpha(bg.a * 0.4f);
        border = border.Alpha(border.a * 0.4f);
        fg = theme::TextFaint;
    }
    if (bg.a > 0) p.FillRoundRect(r, theme::Radius, bg);
    if (border.a > 0) p.StrokeRoundRect(r, theme::Radius, border);
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.bold = kind == ButtonKind::Primary;
    ts.h = ui::HAlign::Center;
    ts.color = fg;
    p.Text(r.Inset(6, 0, 6, 0), label, ts);
    return enabled && b.clicked;
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
    const Color border = focused ? theme::Accent : hovered ? theme::BorderHover : theme::Border;
    p.FillRoundRect(r, theme::Radius, enabled ? theme::Field : theme::Field.Alpha(0.5f));
    p.StrokeRoundRect(r, theme::Radius, border);

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

Rect Panel(ui::Context& c, const Rect& r, std::wstring_view title) {
    auto& p = c.P();
    p.FillRoundRect(r, 6, theme::Panel);
    Rect body = r;
    const Rect header = ui::TakeTop(body, 30);
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
    p.FillRoundRect(thumb, 3, hot ? Color::Hex(0x6A6D75) : Color::Hex(0x45474E));
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

    p.FillRoundRect({box.x + 2, box.y + 4, box.w, box.h}, 6, theme::Shadow);
    p.FillRoundRect(box, 6, theme::PanelHeader);
    p.StrokeRoundRect(box, 6, theme::Border);

    int clicked = -1;
    float y = box.y + kPad;
    c.PushId("menu");
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.separator) {
            p.FillRect({box.x + 8, y + kSepH * 0.5f, box.w - 16, 1}, theme::Border);
            y += kSepH;
            continue;
        }
        const Rect row{box.x + kPad, y, box.w - kPad * 2, kItemH};
        c.PushId(static_cast<uint64_t>(i));
        const auto b = it.enabled ? c.ButtonBehavior(c.MakeId("item"), row) : ui::Context::ButtonResult{};
        c.PopId();
        if (b.hovered) p.FillRoundRect(row, 4, theme::Hover);
        ui::TextStyle ts;
        ts.size = theme::FontSize;
        ts.color = it.enabled ? theme::Text : theme::TextFaint;
        p.Text(row.Inset(10, 0, 10, 0), it.label, ts);
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

Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title) {
    auto& p = c.P();
    const Rect& vp = c.Viewport();
    p.FillRect(vp, Color::Hex(0x000000, 0.55f));
    width = std::min(width, vp.w - 32);
    height = std::min(height, vp.h - 32);
    const Rect box{vp.x + (vp.w - width) * 0.5f, vp.y + (vp.h - height) * 0.5f, width, height};
    p.FillRoundRect({box.x + 3, box.y + 6, box.w, box.h}, 10, theme::Shadow);
    p.FillRoundRect(box, 8, theme::Panel);
    p.StrokeRoundRect(box, 8, theme::Border);
    ui::TextStyle ts;
    ts.size = 17;
    ts.bold = true;
    ts.color = theme::Text;
    p.Text({box.x + 24, box.y + 14, box.w - 48, 32}, title, ts);
    return box.Inset(24, 58, 24, 20);
}

}  // namespace rm::editor::w
