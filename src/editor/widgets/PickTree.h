#pragma once
// The rows of a list in folders (the インスペクター's 部品を足す panel): items with a folder ("3D",
// "UI", "物理/2D" - "/" between folders inside folders), folders that open and close, and a search that
// keeps only what matches. Pure: what is drawn comes from this, and the tests check it (TestPickTree).

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

}  // namespace rm::editor::w
