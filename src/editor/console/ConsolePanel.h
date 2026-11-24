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

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "ui/Context.h"

#include <cstdint>
#include <string>
#include <unordered_map>

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

    // ウィンドウごと (パネルはウィンドウの間を動ける。スクロールの位置もウィンドウごと)。
    struct Follow {
        uint64_t seenSeq = ~0ull;  // 最後に見た一番新しい項目 (~0: まだ描いていない - 最後から始める)
        size_t shown = 0;          // そのとき見せた行の数
        bool atEnd = true;         // 前のフレームの終わりに一番下にいた
    };

    ConsoleSettings settings_;
    reflect::Unknowns unknowns_;
    std::string search_;           // UTF-8
    uint64_t clearedThrough_ = 0;  // この seq まで (含む) をクリアした
    std::unordered_map<const ui::Context*, Follow> follow_;
};

}  // namespace rm::editor
