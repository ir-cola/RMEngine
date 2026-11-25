#include "ui/Context.h"

#include <cmath>

namespace rm::ui {

namespace {
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
}  // namespace

void Context::BeginFrame(Painter* painter, const Input* input, Rect viewport, double timeSec) {
    screen_ = viewport;  // (ウィンドウが SetScreen で本当の画面を言う)
    painter_ = painter;
    in_ = input;
    viewport_ = viewport;
    time_ = timeSec;
    ++frame_;
    idStack_.clear();
    clips_.clear();
    clips_.push_back(viewport);
    inputEnabled_ = true;
    focusAlive_ = false;
    textInputThisFrame_ = false;
    redraw_ = false;
    animate_ = false;
    lockMouse_ = false;
    cursor_ = Cursor::Arrow;
}

void Context::EndFrame() {
    // 離されたボタンは、ウィジェットが消えていても、必ずドラッグ / 押下を終わらせる。
    if (in_->released[0]) active_ = 0;
    // このフレームで描かれなかったフォーカス中のテキスト欄は、フォーカスを失う。
    if (focused_ != 0 && !focusAlive_) {
        focused_ = 0;
        redraw_ = true;
    }
    wantsText_ = focused_ != 0 && textInputThisFrame_;
    clips_.clear();
    in_ = nullptr;
    painter_ = nullptr;
}

Id Context::MakeId(std::string_view s) const {
    uint64_t h = idStack_.empty() ? kFnvOffset : idStack_.back();
    for (const char c : s) {
        h ^= static_cast<unsigned char>(c);
        h *= kFnvPrime;
    }
    return h ? h : 1;
}

void Context::PushId(std::string_view s) { idStack_.push_back(MakeId(s)); }

void Context::PushId(uint64_t n) {
    uint64_t h = idStack_.empty() ? kFnvOffset : idStack_.back();
    for (int i = 0; i < 8; ++i) {
        h ^= (n >> (i * 8)) & 0xFF;
        h *= kFnvPrime;
    }
    idStack_.push_back(h ? h : 1);
}

void Context::PopId() {
    if (!idStack_.empty()) idStack_.pop_back();
}

bool Context::IsHovered(const Rect& r) const {
    if (!inputEnabled_ || !in_->mouseInside) return false;
    const float mx = in_->mouseX, my = in_->mouseY;
    return CurrentClip().Contains(mx, my) && r.Contains(mx, my);
}

bool Context::KeyPressed(Key k, bool ctrl, bool shift) const {
    if (!inputEnabled_) return false;
    for (const auto& e : in_->keys) {
        // (Alt を使うショートカットは無い: Ctrl+Alt は多くのキーボードで AltGr - 文字が入力され、Ctrl+S にはならない)
        if (e.key == k && e.ctrl == ctrl && e.shift == shift && !e.alt) return true;
    }
    return false;
}

Context::ButtonResult Context::ButtonBehavior(Id id, const Rect& r) {
    ButtonResult res;
    res.hovered = IsHovered(r);
    if (res.hovered && in_->pressed[0]) {
        active_ = id;
        res.doubleClicked = in_->doubleClicked;
        redraw_ = true;
    }
    if (active_ == id) {
        res.held = in_->down[0];
        if (in_->released[0]) {
            res.clicked = res.hovered;
            active_ = 0;
            redraw_ = true;
        }
    }
    if (res.hovered && in_->pressed[1]) res.rightClicked = true;
    return res;
}

void Context::FocusText(Id id, std::wstring text, bool selectAll) {
    focused_ = id;
    focusFrame_ = frame_;
    edit_.SetText(std::move(text));
    if (selectAll) edit_.SelectAll();
    caretReset_ = time_;
    redraw_ = true;
}

void Context::ClearFocus() {
    if (focused_ != 0) redraw_ = true;
    focused_ = 0;
}

void Context::SetTextInput(const Rect& caret) {
    textInputThisFrame_ = true;
    caretRect_ = caret;
}

bool Context::CaretVisible() const { return std::fmod(time_ - caretReset_, 1.0) < 0.5; }

void Context::PushClip(const Rect& r) {
    const Rect c = clips_.back().Intersect(r);
    clips_.push_back(c);
    painter_->PushClip(c);
}

void Context::RestoreDepths(size_t ids, size_t clips) {
    while (idStack_.size() > ids) idStack_.pop_back();
    while (clips_.size() > clips && clips_.size() > 1) PopClip();
}

void Context::PopClip() {
    if (clips_.size() <= 1) return;
    clips_.pop_back();
    painter_->PopClip();
}

void Context::RunOverlays() {
    while (!overlays_.empty()) {
        auto list = std::move(overlays_);
        overlays_.clear();
        for (auto& fn : list) fn();
    }
}

float& Context::FloatState(Id id, float init) {
    auto it = floats_.find(id);
    if (it == floats_.end()) it = floats_.emplace(id, init).first;
    return it->second;
}

}  // namespace rm::ui
