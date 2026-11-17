#pragma once
// The rows of a list in folders (the インスペクター's 部品を足す panel): items with a folder ("3D",
// "UI", "物理/2D" - "/" between folders inside folders), folders that open and close, and a search that
// keeps only what matches. Also the rows of one menu of a menu in folders (the 階層's 追加 ▸ 3D
// オブジェクト ▸ 箱: w::ActionMenu). Pure: what is drawn comes from this, and the tests check it
// (TestPickTree).

#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {

struct PickItem {
    std::wstring label;
    std::wstring folder;  // empty: at the top, outside every folder
    bool separator = false;  // a line between rows (menus only)
};

struct PickTreeRow {
    bool isFolder = false;
    std::wstring label;   // the item's, or the folder's own name (its last part)
    std::wstring path;    // a folder: its whole path ("物理/2D"); an item: its folder
    int depth = 0;        // 0 at the top
    int item = -1;        // an item: its index in the list given
    bool open = false;    // a folder: shown open (its rows follow)
    size_t count = 0;     // a folder: how many items are inside it (inside its folders too), after the search
};

// The rows to show, top to bottom. Items outside every folder first, then the folders in the order
// their first item comes in the list; inside a folder its own folders first, then its items, in list
// order. `open`: the folders open now (by path) - the others are closed (only their row).
// `search` (not empty): only the items whose label or folder contains it (ignoring case) - and the
// folders that hold them, all open (what was found is never hidden in a closed folder).
std::vector<PickTreeRow> BuildPickTree(std::span<const PickItem> items, std::wstring_view search, const std::set<std::wstring>& open);

// Whether `text` contains `search` (ignoring the case of Latin letters; full-width letters count as
// their half-width ones, so "ｕｉ" finds "UI").
bool PickMatches(std::wstring_view text, std::wstring_view search);

// One row of one menu of a menu in folders: an item, a line, or a folder (a row that opens the next
// menu: what is inside it).
struct MenuLevelRow {
    bool isFolder = false;
    bool separator = false;
    std::wstring label;  // a folder: its own name (its last part)
    std::wstring path;   // a folder: its whole path ("追加/3D オブジェクト")
    int item = -1;       // an item or a line: its index in the list given
};

// The rows of the menu that shows `folder` ("" = the first menu), in the list's order: its own items
// and lines, and - where the first item inside one of its folders comes - that folder's row (once).
// Lines at the start or the end, and a line after a line, are left out (a folder took what was between).
std::vector<MenuLevelRow> MenuLevel(std::span<const PickItem> items, std::wstring_view folder);

}  // namespace rm::editor::w
