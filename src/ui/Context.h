#pragma once
// Immediate-mode UI context (the core UI foundation).
//
// The screen code is re-run every time something changes (input, resize, timer);
// nothing is redrawn while the user is idle. Widgets are identified by hashed
// string IDs (with an ID stack), never by global slot numbers - the previous
// engine's numbered slider slots collided (bug #17).
//
// Overlays (popup menus, dialogs) work by drawing the base screen with input
// disabled, then drawing the overlay with input enabled on top.

#include "ui/Painter.h"
#include "ui/TextEdit.h"
#include "ui/Types.h"

#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::ui {

using Id = uint64_t;

class Context {
public:
    void BeginFrame(Painter* painter, const Input* input, Rect viewport, double timeSec);
    void EndFrame();

    Painter& P() { return *painter_; }
    const Input& In() const { return *in_; }
    const Rect& Viewport() const { return viewport_; }
    double Time() const { return time_; }

    // --- IDs ---
    Id MakeId(std::string_view s) const;
    void PushId(std::string_view s);
    void PushId(uint64_t n);
    void PopId();

    // --- Input gating (overlays) ---
    void SetInputEnabled(bool on) { inputEnabled_ = on; }
    bool InputEnabled() const { return inputEnabled_; }

    // All of these return false while input is disabled.
    bool IsHovered(const Rect& r) const;
    bool MousePressed(Mouse b = Mouse::Left) const { return inputEnabled_ && in_->pressed[static_cast<int>(b)]; }
    bool MouseReleased(Mouse b = Mouse::Left) const { return inputEnabled_ && in_->released[static_cast<int>(b)]; }
    // Exact modifier match: KeyPressed(Key::S, true) is Ctrl+S, not Ctrl+Shift+S.
    bool KeyPressed(Key k, bool ctrl = false, bool shift = false) const;

    // --- Clickable behaviour ---
    struct ButtonResult {
        bool hovered = false;
        bool held = false;
        bool clicked = false;        // left press + release on the same item
        bool doubleClicked = false;  // reported on the second press
        bool rightClicked = false;
    };
    ButtonResult ButtonBehavior(Id id, const Rect& r);
    Id Active() const { return active_; }

    // --- Keyboard focus / text input ---
    Id Focused() const { return focused_; }
    void FocusText(Id id, std::wstring text, bool selectAll);
    void ClearFocus();
    // True during the frame the focus was given. The click that opened a dialog and focused
    // its first field in the same frame must not count as "clicked outside the field".
    bool FocusIsNew() const { return focusFrame_ == frame_; }
    TextEdit& FocusedEdit() { return edit_; }
    void MarkFocusAlive() { focusAlive_ = true; }
    void SetTextInput(const Rect& caret);
    bool WantsTextInput() const { return wantsText_; }
    const Rect& CaretRect() const { return caretRect_; }
    void ResetCaretBlink() { caretReset_ = time_; }
    bool CaretVisible() const;

    // --- Clip (also limits hovering) ---
    void PushClip(const Rect& r);
    void PopClip();
    const Rect& CurrentClip() const { return clips_.back(); }

    // --- Small per-widget state (scroll offsets ...) ---
    float& FloatState(Id id, float init = 0.f);

    // --- Frame requests ---
    void RequestRedraw() { redraw_ = true; }
    bool RedrawRequested() const { return redraw_; }
    void SetCursor(Cursor c) { cursor_ = c; }
    Cursor GetCursor() const { return cursor_; }

    // Set by the window (platform code), used by text fields.
    std::function<std::wstring()> getClipboardText;
    std::function<void(const std::wstring&)> setClipboardText;

private:
    Painter* painter_ = nullptr;
    const Input* in_ = nullptr;
    Rect viewport_;
    double time_ = 0;

    std::vector<Id> idStack_;
    std::vector<Rect> clips_;
    bool inputEnabled_ = true;

    Id active_ = 0;
    Id focused_ = 0;
    uint64_t frame_ = 0;
    uint64_t focusFrame_ = ~0ull;
    bool focusAlive_ = false;
    TextEdit edit_;
    bool textInputThisFrame_ = false;
    bool wantsText_ = false;
    Rect caretRect_;
    double caretReset_ = 0;

    std::unordered_map<Id, float> floats_;
    bool redraw_ = false;
    Cursor cursor_ = Cursor::Arrow;
};

}  // namespace rm::ui
