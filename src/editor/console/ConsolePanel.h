#pragma once
// 核のパネル「コンソール」: ログ (base/Log) の行と、その上の 1 行のツールバー
//   [クリア] [✓ 自動スクロール] [✓ 情報 N] [✓ 警告 N] [✓ エラー N] 🔍[検索…]
// 見せる行と数を決めるのは ConsoleModel (純粋・テストあり)。ここは描くことと入力だけ。
//
// - クリア: このコンソールの見え方だけを空にする (それより後のログだけを見せて数える)。ログ自身・
//   ログのファイル (editor.log)・デバッグの窓の数は消さない。どのウィンドウのコンソールも同じ。
//   起動し直すと、残っているログがまた見える
// - 自動スクロール: オンの間、新しい行が来たら一番下へ動く。ただしユーザーが上へスクロールして
//   いる間は追わず (オンのまま)、一番下に戻るとまた追う。オフのときは動かない
// - 情報 / 警告 / エラー: そのレベルの行を見せるか。数はクリアより後のそのレベルの全部
//   (隠していても・検索に合わなくても数える。ログが残すのは新しい 2000 件まで)
// - 検索: 本文に含む行だけ (英字の大文字と小文字は区別しない)。Esc で空に。覚えない
// 自動スクロールと 3 つの表示は <project>/UserSettings/Layout.json の "console" に残る
// (下の RM_STRUCT。知らないキーは書き戻す)。どのソフトでも同じ 1 つ。
//
// 行: [時刻 HH:MM:SS.mmm] [印 (警告 / エラーの絵。情報は空けておく)] [本文]
// - 選ぶ: クリック / Ctrl+クリック (足す・外す) / Shift+クリック (範囲) / ドラッグ (範囲) /
//   Ctrl+A (見せている行の全部) / 何もない所のクリック・Esc (外す)。クリアでも外れる。
//   選んだ行は seq で覚える (ConsoleModel の Selection) ので、新しい行が来ても変わらない
// - コピー: Ctrl+C (選んだ行を "HH:MM:SS.mmm [WARN] 本文" で 1 行ずつ)。右クリックのメニューにも
//   コピー / メッセージだけをコピー / すべて選択
// キーが効くのは、最後のクリックがこのパネルで、どの入力欄もフォーカスを持っていないときだけ。

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "editor/console/ConsoleModel.h"
#include "editor/widgets/Widgets.h"
#include "ui/Context.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::editor {

RM_STRUCT()
struct ConsoleSettings {
    RM_PROPERTY() bool autoScroll = true;
    RM_PROPERTY() bool showInfo = true;
    RM_PROPERTY() bool showWarn = true;
    RM_PROPERTY() bool showError = true;
};

class ConsolePanel {
public:
    void Draw(ui::Context& c, const ui::Rect& r);

    json::Value SaveState() const;
    void LoadState(const json::Value& v);

private:
    void DrawToolbar(ui::Context& c, const ui::Rect& bar, size_t info, size_t warn, size_t error, uint64_t lastSeq);
    // 今の絞り込みで見せている行のうち、選んだものの文字をクリップボードへ。
    void CopySelected(ui::Context& c, bool messagesOnly) const;
    void SelectAllShown();
    std::vector<w::Action> MenuActions(ui::Context& c, bool anyShown);

    // ウィンドウごと (パネルはウィンドウの間を動ける。スクロールの位置もウィンドウごと)。
    struct Follow {
        uint64_t seenSeq = ~0ull;  // 最後に見た一番新しい項目 (~0: まだ描いていない - 最後から始める)
        size_t shown = 0;          // そのとき見せた行の数
        bool atEnd = true;         // 前のフレームの終わりに一番下にいた
        bool hasKeys = false;      // 最後のクリックがこのパネルだった (Ctrl+A / Ctrl+C / Esc が効く)
        // ドラッグで選んでいる間: 押した行 (範囲の起点) と、押す前の選択 (Ctrl+ドラッグはそれに足す)。
        bool dragging = false, dragMoved = false, dragAdd = false;
        uint64_t dragFrom = 0;
        console::Selection dragBase;
        w::Popup menu;                   // 右クリックのメニュー
        std::vector<w::Action> actions;  // (メニューが閉じるまで持っておく)
    };

    ConsoleSettings settings_;
    reflect::Unknowns unknowns_;
    std::string search_;           // UTF-8
    uint64_t clearedThrough_ = 0;  // この seq まで (含む) をクリアした
    console::Selection selection_;  // 選んだ行 (どのウィンドウでも同じ)
    std::unordered_map<const ui::Context*, Follow> follow_;
};

}  // namespace rm::editor
