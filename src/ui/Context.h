#pragma once
// イミディエイトモードの UI コンテキスト (核の UI の土台)。
//
// 画面のコードは、何かが変わるたび (入力、リサイズ、タイマー) にもう一度走らせる。
// ユーザーが何もしていない間は、何も描き直さない。ウィジェットは、ハッシュした
// 文字列の ID (ID スタックつき) で見分け、グローバルなスロット番号は決して使わない -
// 前のエンジンの、番号つきのスライダースロットは衝突した (バグ #17)。
//
// オーバーレイ (ポップアップメニュー、ダイアログ) は、入力を止めた土台の画面を描き、
// そのあとで、入力を有効にしてオーバーレイを上に描くことで動く。

#include "ui/Painter.h"
#include "ui/TextEdit.h"
#include "ui/Types.h"

#include <functional>
#include <span>
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
    // このウィンドウがある画面 (モニターの作業領域。タスクバーを除く) を、このウィンドウの DIP の座標で
    // (季節の絵: 画面の右下に置く物は、ウィンドウが画面の右端まで無いと見えない)。知らないときは Viewport。
    const Rect& Screen() const { return screen_; }
    void SetScreen(const Rect& r) { screen_ = r; }
    double Time() const { return time_; }

    // --- ID ---
    Id MakeId(std::string_view s) const;
    void PushId(std::string_view s);
    void PushId(uint64_t n);
    void PopId();

    // --- 入力の制御 (オーバーレイ) ---
    void SetInputEnabled(bool on) { inputEnabled_ = on; }
    bool InputEnabled() const { return inputEnabled_; }

    // これらはすべて、入力が止まっている間は false を返す。
    bool IsHovered(const Rect& r) const;
    bool MousePressed(Mouse b = Mouse::Left) const { return inputEnabled_ && in_->pressed[static_cast<int>(b)]; }
    bool MouseReleased(Mouse b = Mouse::Left) const { return inputEnabled_ && in_->released[static_cast<int>(b)]; }
    // 修飾キーは完全一致: KeyPressed(Key::S, true) は Ctrl+S で、Ctrl+Shift+S ではない。
    bool KeyPressed(Key k, bool ctrl = false, bool shift = false) const;
    // 今押されている (例: カメラを飛ばしている間の W)。入力が止まっている間は false。
    bool KeyDown(Key k) const { return inputEnabled_ && k != Key::None && k < Key::Count && in_->keyDown[static_cast<int>(k)]; }

    // --- クリックできるものの挙動 ---
    struct ButtonResult {
        bool hovered = false;
        bool held = false;
        bool clicked = false;        // 同じ項目の上で左ボタンを押して離した
        bool doubleClicked = false;  // 2 回目の押下で報告する
        bool rightClicked = false;
    };
    ButtonResult ButtonBehavior(Id id, const Rect& r);
    Id Active() const { return active_; }

    // --- キーボードフォーカス / テキスト入力 ---
    Id Focused() const { return focused_; }
    void FocusText(Id id, std::wstring text, bool selectAll);
    void ClearFocus();
    // フォーカスが与えられたフレームの間は true。ダイアログを開いたクリックが、同じフレームで
    // 最初の欄にフォーカスを与えても、「欄の外をクリックした」と数えてはいけない。
    bool FocusIsNew() const { return focusFrame_ == frame_; }
    TextEdit& FocusedEdit() { return edit_; }
    void MarkFocusAlive() { focusAlive_ = true; }
    void SetTextInput(const Rect& caret);
    bool WantsTextInput() const { return wantsText_; }
    const Rect& CaretRect() const { return caretRect_; }
    void ResetCaretBlink() { caretReset_ = time_; }
    bool CaretVisible() const;

    // --- クリップ (ホバーも制限する) ---
    void PushClip(const Rect& r);
    void PopClip();
    const Rect& CurrentClip() const { return clips_.back(); }
    // id とクリップのスタックの深さと、それをその深さに戻すこと (push して pop しなかった部分 -
    // 自分の早期 return - があると、あとの部分すべての id とホバーが変わってしまう)。
    size_t IdDepth() const { return idStack_.size(); }
    size_t ClipDepth() const { return clips_.size(); }
    void RestoreDepths(size_t ids, size_t clips);

    // --- ウィジェットごとの小さな状態 (スクロール位置など) ---
    float& FloatState(Id id, float init = 0.f);

    // --- フレームの要求 ---
    void RequestRedraw() { redraw_ = true; }
    bool RedrawRequested() const { return redraw_; }
    // 何かが自分で動いている (キーを押している間に飛ぶカメラ): 次のフレームをできるだけ早く描く。
    // 毎フレーム求められている間ずっと。これが無いと、画面は入力があったときだけ描く。
    void RequestAnimation() { animate_ = true; }
    bool AnimationRequested() const { return animate_; }
    // 回り続ける / 滑り続けるもの (カメラ) をドラッグしている間: カーソルを隠してドラッグを始めた所に
    // 留めるので、画面の端で止まらない。In().mouseDX / mouseDY がマウスの動いた量を伝える。
    // ドラッグ中は毎フレーム求める。求められなくなったら、カーソルは戻る。
    void LockMouse() { lockMouse_ = true; }
    bool MouseLockRequested() const { return lockMouse_; }
    void SetCursor(Cursor c) { cursor_ = c; }
    Cursor GetCursor() const { return cursor_; }

    // フレームの最後、ほかのすべてのあとに、求めたパネルのクリップなしで描く
    // (パネルの中のドロップダウンリストが、パネルの縁で切られてはいけない)。
    void AddOverlay(std::function<void()> fn) { overlays_.push_back(std::move(fn)); }
    void RunOverlays();

    // ウィンドウ (プラットフォームのコード) が設定し、テキスト欄が使う。
    std::function<std::wstring()> getClipboardText;
    std::function<void(const std::wstring&)> setClipboardText;

private:
    Painter* painter_ = nullptr;
    const Input* in_ = nullptr;
    Rect viewport_;
    Rect screen_;
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
    std::vector<std::function<void()>> overlays_;
    bool redraw_ = false;
    bool animate_ = false;
    bool lockMouse_ = false;
    Cursor cursor_ = Cursor::Arrow;
};

}  // namespace rm::ui
