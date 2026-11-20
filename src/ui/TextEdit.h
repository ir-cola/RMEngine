#pragma once
// 1 行のテキスト編集の状態 (純粋): キャレット、選択、挿入 / 削除、
// サロゲートペア対応。TextField ウィジェットがこれを描く。このファイルは編集だけをする。

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

    // extend = アンカーを保つ (Shift を押している)。word = Ctrl を押している。
    void SetCaret(size_t pos, bool extend);
    void MoveLeft(bool extend, bool word);
    void MoveRight(bool extend, bool word);
    void Home(bool extend) { SetCaret(0, extend); }
    void End(bool extend) { SetCaret(text_.size(), extend); }
    void SelectAll();

    // すべて、テキストが変わったら true を返す。
    // Insert は選択を置き換える。改行は空白になり、ほかの制御文字は捨てる。
    bool Insert(std::wstring_view s);
    bool Backspace(bool word);
    bool Delete(bool word);

private:
    bool DeleteSelection();
    size_t PrevChar(size_t i) const;
    size_t NextChar(size_t i) const;
    size_t PrevWord(size_t i) const;
    size_t NextWord(size_t i) const;
    size_t Snap(size_t i) const;  // サロゲートペアの 2 つの半分の間には決して止まらない

    std::wstring text_;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    size_t maxLength_ = 1024;
};

}  // namespace rm::ui
