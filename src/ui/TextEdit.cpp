#include "ui/TextEdit.h"

namespace rm::ui {

namespace {
bool IsHigh(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLow(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\x3000'; }
}  // namespace

void TextEdit::SetText(std::wstring text) {
    text_ = std::move(text);
    caret_ = anchor_ = text_.size();
}

size_t TextEdit::Snap(size_t i) const {
    if (i > text_.size()) i = text_.size();
    if (i > 0 && i < text_.size() && IsLow(text_[i]) && IsHigh(text_[i - 1])) --i;
    return i;
}

size_t TextEdit::PrevChar(size_t i) const {
    if (i == 0) return 0;
    --i;
    if (i > 0 && IsLow(text_[i]) && IsHigh(text_[i - 1])) --i;
    return i;
}

size_t TextEdit::NextChar(size_t i) const {
    if (i >= text_.size()) return text_.size();
    if (IsHigh(text_[i]) && i + 1 < text_.size() && IsLow(text_[i + 1])) return i + 2;
    return i + 1;
}

size_t TextEdit::PrevWord(size_t i) const {
    while (i > 0 && IsSpace(text_[i - 1])) --i;
    while (i > 0 && !IsSpace(text_[i - 1])) --i;
    return i;
}

size_t TextEdit::NextWord(size_t i) const {
    const size_t n = text_.size();
    while (i < n && !IsSpace(text_[i])) ++i;
    while (i < n && IsSpace(text_[i])) ++i;
    return i;
}

void TextEdit::SetCaret(size_t pos, bool extend) {
    caret_ = Snap(pos);
    if (!extend) anchor_ = caret_;
}

void TextEdit::MoveLeft(bool extend, bool word) {
    if (HasSelection() && !extend) {
        caret_ = anchor_ = SelStart();
        return;
    }
    SetCaret(word ? PrevWord(caret_) : PrevChar(caret_), extend);
}

void TextEdit::MoveRight(bool extend, bool word) {
    if (HasSelection() && !extend) {
        caret_ = anchor_ = SelEnd();
        return;
    }
    SetCaret(word ? NextWord(caret_) : NextChar(caret_), extend);
}

void TextEdit::SelectAll() {
    anchor_ = 0;
    caret_ = text_.size();
}

bool TextEdit::DeleteSelection() {
    if (!HasSelection()) return false;
    const size_t a = SelStart();
    text_.erase(a, SelEnd() - a);
    caret_ = anchor_ = a;
    return true;
}

bool TextEdit::Insert(std::wstring_view s) {
    std::wstring clean;
    clean.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c == L'\r' || c == L'\n' || c == L'\t') {
            if (c == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') continue;  // CRLF -> 空白 1 つ
            clean.push_back(L' ');
        } else if (c >= 0x20 && c != 0x7F) {
            clean.push_back(c);
        }
    }
    bool changed = DeleteSelection();
    const size_t room = text_.size() < maxLength_ ? maxLength_ - text_.size() : 0;
    if (clean.size() > room) {
        clean.resize(room);
        if (!clean.empty() && IsHigh(clean.back())) clean.pop_back();
    }
    if (clean.empty()) return changed;
    text_.insert(caret_, clean);
    caret_ += clean.size();
    anchor_ = caret_;
    return true;
}

bool TextEdit::Backspace(bool word) {
    if (DeleteSelection()) return true;
    if (caret_ == 0) return false;
    const size_t from = word ? PrevWord(caret_) : PrevChar(caret_);
    text_.erase(from, caret_ - from);
    caret_ = anchor_ = from;
    return true;
}

bool TextEdit::Delete(bool word) {
    if (DeleteSelection()) return true;
    if (caret_ >= text_.size()) return false;
    const size_t to = word ? NextWord(caret_) : NextChar(caret_);
    text_.erase(caret_, to - caret_);
    anchor_ = caret_;
    return true;
}

}  // namespace rm::ui
