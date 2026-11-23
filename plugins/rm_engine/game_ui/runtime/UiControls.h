#pragma once
// ゲーム UI - 入力の部品 (入力欄・スライダー・トグル・スクロール) の純粋な計算。描画もファイルも
// 扱わない: UI サービス (UiService) が毎フレームこれで値を変え、描く方 (UiRenderer) はこれで描く場所を
// 決める。だから描いた所がそのまま触れる所になる (憲法 3)。tests/TestGameUi.cpp でテストする。
//
//   入力欄     1 フレームの打鍵 (ui::TypedText) を文字に (TypeInto: 文字の種類・最大の文字数・選択・
//              コピーと貼り付け・Enter と Esc)。描く文字 (InputView: パスワードの ●・IME の変換中・案内)
//   スライダー 値を最小..最大と刻みに合わせる・マウスの所の値・キーの 1 歩・溝とつまみの箱
//   トグル     印 (チェックの箱 / スイッチ) の箱
//   スクロール 動かせる範囲 (子の箱を合わせた中身と枠から)・選んだ物を見せる・スクロールバー

#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "ui/Types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rm::game_ui {

// --- 入力欄 -----------------------------------------------------------------------------------
// 見える文字の数 (UTF-16 のサロゲートペアは 1 文字)。
size_t CountChars(std::wstring_view s);
// `typed` のうち `content` が入れてよい文字だけ (整数・数・英字と数字では、全角の英数字を半角にする)。
// `before` は入る前の文字 (選択を消した後)、`at` は入る所 (- は先頭だけ、. は 1 つだけ)。
std::wstring FilterTyped(UiInputContent content, std::wstring_view before, size_t at, std::wstring_view typed);

// コピーと貼り付け (UI サービスが Windows のクリップボードをつなぐ。テストは自分のもの)。空なら何もしない。
struct UiClipboard {
    std::function<std::wstring()> get;
    std::function<void(const std::wstring&)> set;
};
struct UiTypeResult {
    bool changed = false;    // 文字が変わった
    bool submitted = false;  // Enter (打つのをやめる)
    bool cancelled = false;  // Esc (打つのをやめ、文字を打ち始める前に戻す: それは呼ぶ側)
};
// 1 フレームの打鍵を入力欄の文字 `text` (UTF-16) に: キー (← → Home End・Shift で選ぶ・Ctrl で単語、
// Backspace Delete、Ctrl+A / C / X / V、Enter、Esc)、次に打った文字、次に IME の確定した文字。
// `caret` / `anchor` はその UTF-16 の位置 (文字の中に収める)。Enter / Esc の後のキーは見ない。
UiTypeResult TypeInto(std::wstring& text, int& caret, int& anchor, const UiInputField& field, const ui::TypedText& typed,
                      const UiClipboard& clip = {});

// 入力欄に描く文字と、その中の位置 (どれも `shown` の UTF-16 の位置)。打っていない空の欄は案内。
struct UiInputView {
    std::wstring shown;
    bool placeholder = false;
    uint32_t caret = 0;
    uint32_t selStart = 0, selEnd = 0;       // 選んでいる所 (同じなら無し)
    uint32_t compStart = 0, compLength = 0;  // IME で変換中の所 (下線)
};
UiInputView InputView(const UiInputField& f);

// --- スライダー -------------------------------------------------------------------------------
// `v` を最小..最大 (逆でもよい) に収め、刻みに合わせたもの。
float SliderSnap(const UiSlider& s, float v);
// 今の値の、伸びる向きの始まりからの割合 0..1。
float SliderFraction(const UiSlider& s);
bool SliderVertical(const UiSlider& s);
// 溝 (地)・中身・つまみ (どれもそのものの単位。`box` はその箱)。つまみの真ん中は溝の端から端まで動く。
UiBox SliderTrack(const UiSlider& s, const UiBox& box);
UiBox SliderFill(const UiSlider& s, const UiBox& box);
UiBox SliderHandle(const UiSlider& s, const UiBox& box);
// 点 `p` (そのものの単位) の所の値 (刻みに合わせる)。
float SliderAt(const UiSlider& s, const UiBox& box, Vec2 p);
// キー / パッドの向き (dx, dy: 画面 Y は下向き) がこのスライダーを動かすなら +1 (最大へ) / -1、
// 動かさない (選択がほかへ移る) なら 0。
int SliderKey(const UiSlider& s, int dx, int dy);
// キー 1 回分を `dir` (+1 最大へ / -1) へ動かした値。
float SliderNudge(const UiSlider& s, int dir);

// --- トグル -----------------------------------------------------------------------------------
// 印 (チェックの箱 / スイッチの溝) の箱: 枠の左、縦は真ん中 (そのものの単位)。
UiBox ToggleMark(const UiToggle& t, const UiBox& box);
// 見えているオンの量 0..1 (UI サービスが寄せている途中のもの。まだ無ければ on のとおり)。
float ToggleShown(const UiToggle& t);

// --- スクロール -------------------------------------------------------------------------------
// どれだけ動かせるか (offset の範囲)。動かない向きは 0..0。
struct UiScrollRange {
    Vec2 low{0, 0}, high{0, 0};
};
// 枠 `view` と、子の箱を合わせた中身 `content` (どちらも枠の親の単位: 動かす前)。
UiScrollRange ScrollRange(const UiScroll& s, const UiBox& view, const UiBox& content);
Vec2 ClampScroll(Vec2 offset, const UiScrollRange& r);
// `item` (中身の単位: 動かす前) が枠の中に見えるように動かした offset (見えていればそのまま)。
Vec2 ScrollToShow(Vec2 offset, const UiBox& view, const UiBox& item, const UiScrollRange& r);
// スクロールバー (枠の単位): 縦は右の縁、横は下の縁。動かせない向き / 出さないときは shown が false。
struct UiScrollBar {
    bool shown = false;
    UiBox track, thumb;
};
UiScrollBar ScrollBar(const UiScroll& s, const UiBox& view, const UiBox& content, Vec2 offset, bool vertical);
// つまみを `moved` (枠の単位) だけ動かしたときの offset の動き。
float ThumbDrag(const UiScrollBar& bar, float moved, float low, float high, bool vertical);

}  // namespace rm::game_ui
