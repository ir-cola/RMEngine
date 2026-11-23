#include "rm_engine/game_ui/runtime/UiControls.h"

#include "base/Utf.h"
#include "ui/TextEdit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rm::game_ui {

namespace {

bool IsHigh(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLow(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool IsLetter(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }

// 全角の英数字と記号 (！..～) を半角に。
wchar_t Narrow(wchar_t c) { return c >= 0xFF01 && c <= 0xFF5E ? static_cast<wchar_t>(c - 0xFF01 + 0x21) : c; }

// `s` の UTF-16 の `units` までにある見える文字の数。
size_t CharsBefore(std::wstring_view s, size_t units) { return CountChars(s.substr(0, std::min(units, s.size()))); }

// 見える文字 `n` 個分の頭 (サロゲートペアを分けない)。
std::wstring_view FirstChars(std::wstring_view s, size_t n) {
    size_t i = 0;
    for (size_t c = 0; c < n && i < s.size(); ++c) i += (IsHigh(s[i]) && i + 1 < s.size() && IsLow(s[i + 1])) ? 2 : 1;
    return s.substr(0, i);
}

}  // namespace

// --- 入力欄 -----------------------------------------------------------------------------------

size_t CountChars(std::wstring_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i, ++n) {
        if (IsHigh(s[i]) && i + 1 < s.size() && IsLow(s[i + 1])) ++i;
    }
    return n;
}

std::wstring FilterTyped(UiInputContent content, std::wstring_view before, size_t at, std::wstring_view typed) {
    if (content == UiInputContent::Any) return std::wstring(typed);
    // 1 文字ずつ入れてみる (- と . は、それまでに入れたものも数える)。
    std::wstring cur(before);
    size_t pos = std::min(at, cur.size());
    std::wstring out;
    for (const wchar_t raw : typed) {
        const wchar_t c = Narrow(raw);
        const bool minusFirst = !cur.empty() && cur[0] == L'-';
        bool ok = false;
        switch (content) {
            case UiInputContent::Integer:
                ok = (IsDigit(c) && !(pos == 0 && minusFirst)) || (c == L'-' && pos == 0 && !minusFirst);
                break;
            case UiInputContent::Decimal:
                ok = (IsDigit(c) && !(pos == 0 && minusFirst)) || (c == L'-' && pos == 0 && !minusFirst) ||
                     (c == L'.' && cur.find(L'.') == std::wstring::npos && !(pos == 0 && minusFirst));
                break;
            case UiInputContent::Alphanumeric: ok = IsDigit(c) || IsLetter(c); break;
            case UiInputContent::Any: ok = true; break;
        }
        if (!ok) continue;
        out.push_back(c);
        cur.insert(pos, 1, c);
        ++pos;
    }
    return out;
}

UiTypeResult TypeInto(std::wstring& text, int& caret, int& anchor, const UiInputField& field, const ui::TypedText& typed, const UiClipboard& clip) {
    UiTypeResult r;
    ui::TextEdit edit;
    edit.SetMaxLength(std::numeric_limits<size_t>::max() / 2);  // (最大の文字数は見える文字で、下で数える)
    edit.SetText(text);
    const auto fit = [&](int i) { return static_cast<size_t>(std::clamp(i, 0, static_cast<int>(text.size()))); };
    edit.SetCaret(fit(anchor), false);
    edit.SetCaret(fit(caret), true);
    // 選んでいる所を置き換えて入れる: 入れてよい文字だけ、最大の文字数まで。
    auto insert = [&](std::wstring_view s) {
        std::wstring rest = edit.Text();
        rest.erase(edit.SelStart(), edit.SelEnd() - edit.SelStart());
        std::wstring add = FilterTyped(field.content, rest, edit.SelStart(), s);
        if (field.maxLength > 0) {
            const size_t have = CountChars(rest), most = static_cast<size_t>(field.maxLength);
            add = std::wstring(FirstChars(add, have >= most ? 0 : most - have));
        }
        if (add.empty()) return;  // (入らない文字だけ: 選んだ所も消さない)
        if (edit.Insert(add)) r.changed = true;
    };
    for (const ui::KeyEvent& k : typed.keys) {
        switch (k.key) {
            case ui::Key::Left: edit.MoveLeft(k.shift, k.ctrl); break;
            case ui::Key::Right: edit.MoveRight(k.shift, k.ctrl); break;
            case ui::Key::Home: edit.Home(k.shift); break;
            case ui::Key::End: edit.End(k.shift); break;
            case ui::Key::Backspace: r.changed = edit.Backspace(k.ctrl) || r.changed; break;
            case ui::Key::Delete: r.changed = edit.Delete(k.ctrl) || r.changed; break;
            case ui::Key::Enter: r.submitted = true; break;
            case ui::Key::Escape: r.cancelled = true; break;
            case ui::Key::A:
                if (k.ctrl) edit.SelectAll();
                break;
            case ui::Key::C:
            case ui::Key::X:
                // パスワードはコピーさせない。
                if (k.ctrl && !field.password && edit.HasSelection() && clip.set) {
                    clip.set(edit.SelectedText());
                    if (k.key == ui::Key::X) r.changed = edit.Backspace(false) || r.changed;
                }
                break;
            case ui::Key::V:
                if (k.ctrl && clip.get) insert(clip.get());
                break;
            default: break;
        }
        if (r.submitted || r.cancelled) break;
    }
    if (!r.submitted && !r.cancelled) {
        if (!typed.chars.empty()) insert(typed.chars);
        if (!typed.imeResult.empty()) insert(typed.imeResult);
    }
    text = edit.Text();
    caret = static_cast<int>(edit.Caret());
    anchor = static_cast<int>(edit.Anchor());
    return r;
}

UiInputView InputView(const UiInputField& f) {
    UiInputView v;
    const std::wstring text = Utf8ToWide(f.text);
    const std::wstring comp = f.typing ? Utf8ToWide(f.composing) : std::wstring();
    if (text.empty() && comp.empty()) {
        v.shown = Utf8ToWide(f.placeholder);
        v.placeholder = true;
        return v;
    }
    const auto fit = [&](int i) { return static_cast<size_t>(std::clamp(i, 0, static_cast<int>(text.size()))); };
    // パスワード: 見える 1 文字ごとに ● 1 つ (位置も文字の数に直す)。
    const auto shownAt = [&](std::wstring_view s, size_t units) { return f.password ? CharsBefore(s, units) : std::min(units, s.size()); };
    const std::wstring body = f.password ? std::wstring(CountChars(text), L'\x25CF') : text;
    const size_t caret = shownAt(text, fit(f.caret)), anchor = shownAt(text, fit(f.anchor));
    if (comp.empty()) {
        v.shown = body;
        v.caret = static_cast<uint32_t>(caret);
        v.selStart = static_cast<uint32_t>(std::min(caret, anchor));
        v.selEnd = static_cast<uint32_t>(std::max(caret, anchor));
        return v;
    }
    // 変換中: カーソルの所に差し込み、下線を引く (選択は見せない)。
    const std::wstring compShown = f.password ? std::wstring(CountChars(comp), L'\x25CF') : comp;
    v.shown = body.substr(0, caret) + compShown + body.substr(caret);
    v.compStart = static_cast<uint32_t>(caret);
    v.compLength = static_cast<uint32_t>(compShown.size());
    v.caret = static_cast<uint32_t>(caret + shownAt(comp, static_cast<size_t>(std::clamp(f.composingCaret, 0, static_cast<int>(comp.size())))));
    v.selStart = v.selEnd = v.caret;
    return v;
}

// --- スライダー -------------------------------------------------------------------------------

namespace {

float Finite(float v, float otherwise) { return std::isfinite(v) ? v : otherwise; }

}  // namespace

float SliderSnap(const UiSlider& s, float v) {
    const float a = Finite(s.min, 0), b = Finite(s.max, 1);
    const float lo = std::min(a, b), hi = std::max(a, b);
    v = std::clamp(Finite(v, lo), lo, hi);
    if (s.step > 0 && std::isfinite(s.step)) {
        v = lo + std::round((v - lo) / s.step) * s.step;
        v = std::clamp(v, lo, hi);
    }
    return v;
}

float SliderFraction(const UiSlider& s) {
    const float a = Finite(s.min, 0), b = Finite(s.max, 1);
    if (std::abs(b - a) < 1e-12f) return 0;
    return std::clamp((SliderSnap(s, s.value) - a) / (b - a), 0.f, 1.f);
}

bool SliderVertical(const UiSlider& s) { return s.direction == UiBarDirection::BottomToTop || s.direction == UiBarDirection::TopToBottom; }

namespace {

// つまみの真ん中が動く線: 始まり (割合 0) と終わり (割合 1) の点。
void SliderLine(const UiSlider& s, const UiBox& box, Vec2& from, Vec2& to) {
    const bool vertical = SliderVertical(s);
    const float along = vertical ? box.h : box.w;
    const float margin = std::min(std::max(s.handleSize, 0.f) * 0.5f, along * 0.5f);
    const float cx = box.x + box.w * 0.5f, cy = box.y + box.h * 0.5f;
    switch (s.direction) {
        case UiBarDirection::LeftToRight: from = {box.x + margin, cy}, to = {box.Right() - margin, cy}; break;
        case UiBarDirection::RightToLeft: from = {box.Right() - margin, cy}, to = {box.x + margin, cy}; break;
        case UiBarDirection::TopToBottom: from = {cx, box.y + margin}, to = {cx, box.Bottom() - margin}; break;
        case UiBarDirection::BottomToTop: from = {cx, box.Bottom() - margin}, to = {cx, box.y + margin}; break;
    }
}

UiBox Span(Vec2 a, Vec2 b, float thick, bool vertical) {
    const float t = std::max(thick, 0.f);
    if (vertical) return {a.x - t * 0.5f, std::min(a.y, b.y), t, std::abs(b.y - a.y)};
    return {std::min(a.x, b.x), a.y - t * 0.5f, std::abs(b.x - a.x), t};
}

}  // namespace

UiBox SliderTrack(const UiSlider& s, const UiBox& box) {
    Vec2 a, b;
    SliderLine(s, box, a, b);
    const bool vertical = SliderVertical(s);
    return Span(a, b, std::min(s.trackWidth, vertical ? box.w : box.h), vertical);
}

UiBox SliderFill(const UiSlider& s, const UiBox& box) {
    Vec2 a, b;
    SliderLine(s, box, a, b);
    const float f = SliderFraction(s);
    const Vec2 at{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
    const bool vertical = SliderVertical(s);
    return Span(a, at, std::min(s.trackWidth, vertical ? box.w : box.h), vertical);
}

UiBox SliderHandle(const UiSlider& s, const UiBox& box) {
    Vec2 a, b;
    SliderLine(s, box, a, b);
    const float f = SliderFraction(s);
    const float d = std::max(s.handleSize, 0.f);
    return {a.x + (b.x - a.x) * f - d * 0.5f, a.y + (b.y - a.y) * f - d * 0.5f, d, d};
}

float SliderAt(const UiSlider& s, const UiBox& box, Vec2 p) {
    Vec2 a, b;
    SliderLine(s, box, a, b);
    const float len2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
    const float f = len2 > 1e-6f ? std::clamp(((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / len2, 0.f, 1.f) : 0.f;
    const float lo = Finite(s.min, 0), hi = Finite(s.max, 1);
    return SliderSnap(s, lo + (hi - lo) * f);
}

int SliderKey(const UiSlider& s, int dx, int dy) {
    switch (s.direction) {
        case UiBarDirection::LeftToRight: return dy == 0 ? dx : 0;
        case UiBarDirection::RightToLeft: return dy == 0 ? -dx : 0;
        case UiBarDirection::TopToBottom: return dx == 0 ? dy : 0;
        case UiBarDirection::BottomToTop: return dx == 0 ? -dy : 0;
    }
    return 0;
}

float SliderNudge(const UiSlider& s, int dir) {
    const float lo = Finite(s.min, 0), hi = Finite(s.max, 1);
    float amount = s.keyStep > 0 ? s.keyStep : s.step > 0 ? s.step : std::abs(hi - lo) / 20.f;
    if (!std::isfinite(amount)) amount = 0;
    const float toward = hi >= lo ? 1.f : -1.f;
    return SliderSnap(s, SliderSnap(s, s.value) + static_cast<float>(dir) * amount * toward);
}

// --- トグル -----------------------------------------------------------------------------------

UiBox ToggleMark(const UiToggle& t, const UiBox& box) {
    float h = t.markSize > 0 ? std::min(t.markSize, std::max(box.h, 0.f)) : std::max(box.h, 0.f);
    float w = t.look == UiToggleLook::Switch ? h * 1.8f : h;
    if (w > box.w && w > 0) {
        const float k = std::max(box.w, 0.f) / w;
        w *= k, h *= k;
    }
    return {box.x, box.y + (box.h - h) * 0.5f, w, h};
}

float ToggleShown(const UiToggle& t) { return t.ready ? std::clamp(t.shown, 0.f, 1.f) : (t.on ? 1.f : 0.f); }

// --- スクロール -------------------------------------------------------------------------------

UiScrollRange ScrollRange(const UiScroll& s, const UiBox& view, const UiBox& content) {
    UiScrollRange r;
    if (s.horizontal) {
        r.low.x = std::min(0.f, content.x - view.x);
        r.high.x = std::max(0.f, content.Right() - view.Right());
    }
    if (s.vertical) {
        r.low.y = std::min(0.f, content.y - view.y);
        r.high.y = std::max(0.f, content.Bottom() - view.Bottom());
    }
    return r;
}

Vec2 ClampScroll(Vec2 offset, const UiScrollRange& r) {
    return {std::clamp(Finite(offset.x, 0), r.low.x, r.high.x), std::clamp(Finite(offset.y, 0), r.low.y, r.high.y)};
}

Vec2 ScrollToShow(Vec2 offset, const UiBox& view, const UiBox& item, const UiScrollRange& r) {
    // 1 つの軸: 見えている所 [view + offset] の外にはみ出した分だけ動かす (大きい物は始まりを見せる)。
    auto axis = [](float off, float viewStart, float viewLen, float itemStart, float itemLen) {
        if (itemStart + itemLen > viewStart + off + viewLen) off = itemStart + itemLen - viewStart - viewLen;
        if (itemStart < viewStart + off) off = itemStart - viewStart;
        return off;
    };
    return ClampScroll({axis(offset.x, view.x, view.w, item.x, item.w), axis(offset.y, view.y, view.h, item.y, item.h)}, r);
}

UiScrollBar ScrollBar(const UiScroll& s, const UiBox& view, const UiBox& content, Vec2 offset, bool vertical) {
    UiScrollBar bar;
    const UiScrollRange r = ScrollRange(s, view, content);
    const float span = vertical ? r.high.y - r.low.y : r.high.x - r.low.x;
    if (!s.scrollbar || span <= 0.5f) return bar;
    const float bw = std::clamp(s.barWidth, 1.f, std::max(1.f, vertical ? view.w : view.h));
    // 縦と横の両方が出るときは、角を空けて重ならないように。
    const bool other = s.scrollbar && (vertical ? r.high.x - r.low.x : r.high.y - r.low.y) > 0.5f;
    const float corner = other ? bw : 0.f;
    if (vertical) {
        bar.track = {view.Right() - bw, view.y, bw, std::max(0.f, view.h - corner)};
    } else {
        bar.track = {view.x, view.Bottom() - bw, std::max(0.f, view.w - corner), bw};
    }
    const float trackLen = vertical ? bar.track.h : bar.track.w;
    const float viewLen = vertical ? view.h : view.w;
    const float thumbLen = std::min(trackLen, std::max(bw * 2.f, trackLen * viewLen / (viewLen + span)));
    const float f = std::clamp(((vertical ? offset.y : offset.x) - (vertical ? r.low.y : r.low.x)) / span, 0.f, 1.f);
    const float at = (trackLen - thumbLen) * f;
    bar.thumb = vertical ? UiBox{bar.track.x, bar.track.y + at, bw, thumbLen} : UiBox{bar.track.x + at, bar.track.y, thumbLen, bw};
    bar.shown = true;
    return bar;
}

float ThumbDrag(const UiScrollBar& bar, float moved, float low, float high, bool vertical) {
    const float room = vertical ? bar.track.h - bar.thumb.h : bar.track.w - bar.thumb.w;
    if (!bar.shown || room <= 1e-3f) return 0;
    return moved * (high - low) / room;
}

}  // namespace rm::game_ui
