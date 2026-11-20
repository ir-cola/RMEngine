#pragma once
// フォルダー分けした一覧の行 (インスペクターの 部品を足す パネル): フォルダー ("3D",
// "UI", "物理/2D" - フォルダーの中のフォルダーは "/" で区切る) 付きの項目、開閉するフォルダー、
// 一致するものだけを残す検索。フォルダー分けしたメニューのうち 1 つのメニューの行も
// (階層の 追加 ▸ 3D オブジェクト ▸ 箱: w::ActionMenu)。純粋: 描くものはここから作り、
// テストがそれを確かめる (TestPickTree)。

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {

struct PickItem {
    std::wstring label;
    std::wstring folder;  // 空: 最上位で、どのフォルダーの外でもない
    bool separator = false;  // 行の間の線 (メニューだけ)
};

struct PickTreeRow {
    bool isFolder = false;
    std::wstring label;   // 項目の、またはフォルダー自身の名前 (その最後の部分)
    std::wstring path;    // フォルダー: そのパス全体 ("物理/2D")。項目: そのフォルダー
    int depth = 0;        // 最上位が 0
    int item = -1;        // 項目: 渡された一覧での添字
    bool open = false;    // フォルダー: 開いて表示している (その行が続く)
    size_t count = 0;     // フォルダー: 中の項目の数 (中のフォルダーの分も)。検索のあと
};

// 上から下に見せる行。どのフォルダーの外にもある項目が先、次にフォルダー。順番は、一覧で
// そのフォルダーの最初の項目が出てくる順。フォルダーの中は、中のフォルダーが先、次に項目 (一覧の順)。
// `open`: 今開いているフォルダー (パスで) - ほかは閉じていて、その行だけ。
// `search` (空でない): ラベルかフォルダーにそれを含む項目 (大文字小文字は区別しない) と、それを
// 持つフォルダーだけ。フォルダーはすべて開く (見つかったものが、閉じたフォルダーに隠れることはない)。
std::vector<PickTreeRow> BuildPickTree(std::span<const PickItem> items, std::wstring_view search, const std::set<std::wstring>& open);

// `text` が `search` を含むか (ラテン文字の大文字小文字は区別しない。全角の文字は
// 半角と同じに数えるので、"ｕｉ" で "UI" が見つかる)。
bool PickMatches(std::wstring_view text, std::wstring_view search);

// フォルダー分けしたメニューのうち 1 つのメニューの 1 行: 項目、線、またはフォルダー (次の
// メニューを開く行: その中にあるもの)。
struct MenuLevelRow {
    bool isFolder = false;
    bool separator = false;
    std::wstring label;  // フォルダー: 自身の名前 (その最後の部分)
    std::wstring path;   // フォルダー: そのパス全体 ("追加/3D オブジェクト")
    int item = -1;       // 項目か線: 渡された一覧での添字
};

// `folder` を出すメニュー ("" = 最初のメニュー) の行を、一覧の順に: そのメニュー自身の項目と線、
// それと - そのフォルダーの中の最初の項目が出てくる所に - そのフォルダーの行 (1 回だけ)。
// 最初や最後の線と、線の次の線は出さない (フォルダーがその間にあったものを持っていった)。
std::vector<MenuLevelRow> MenuLevel(std::span<const PickItem> items, std::wstring_view folder);

}  // namespace rm::editor::w
