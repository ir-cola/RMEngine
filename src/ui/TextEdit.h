#pragma once
// Single-line text editing state (pure): caret, selection, insert / delete,
// surrogate-pair aware. The TextField widget draws it; this file only edits.

#include <string>
#include <string_view>

namespace rm::ui {

class TextEdit {
public:
    void SetText(std::wstring text);
    const std::wstring& Text() const { return text_; }

    size_t Caret() const { return caret_; }
    size_t Anchor() const { return anchor_; }
    bool HasSelection() const { return caret_ != anchor_; }
    size_t SelStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
    size_t SelEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
    std::wstring SelectedText() const { return text_.substr(SelStart(), SelEnd() - SelStart()); }

    void SetMaxLength(size_t n) { maxLength_ = n; }

    // extend = keep the anchor (Shift held). word = Ctrl held.
    void SetCaret(size_t pos, bool extend);
    void MoveLeft(bool extend, bool word);
    void MoveRight(bool extend, bool word);
    void Home(bool extend) { SetCaret(0, extend); }
    void End(bool extend) { SetCaret(text_.size(), extend); }
    void SelectAll();

    // All return true when the text changed.
    // Insert replaces the selection. Line breaks become spaces, other control chars are dropped.
    bool Insert(std::wstring_view s);
    bool Backspace(bool word);
    bool Delete(bool word);

private:
    bool DeleteSelection();
    size_t PrevChar(size_t i) const;
    size_t NextChar(size_t i) const;
    size_t PrevWord(size_t i) const;
    size_t NextWord(size_t i) const;
    size_t Snap(size_t i) const;  // never stop between the two halves of a surrogate pair

    std::wstring text_;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    size_t maxLength_ = 1024;
};

}  // namespace rm::ui
