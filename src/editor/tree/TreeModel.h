#pragma once
// The pure part of the core tree (階層): which rows are visible, selection, and where a drop goes.
// No drawing, no I/O - tested in tests/TestTree.cpp.
//
// Design RME_PLATFORM 4-c: the core shows the tree (open / close, select, drag to reorder, rename,
// search, the right-click menu frame) and knows nothing of what is in it. A software lists its
// items (game: entities and their parents, 3D model: meshes and bones, illustration: layers) and
// decides what a move or a menu row means.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rm::editor::tree {

using ItemId = uint64_t;  // chosen by the software; 0 = nothing / the top level

// The small picture in front of a row's name (drawn by the core; the software picks one).
enum class Icon : uint8_t {
    None,    // no picture (the name starts where the picture would be)
    Object,  // a thing with no shape of its own (a circle): an entity without a place
    Cube,    // a thing in 3D space
    Camera,  // a thing that looks (a game's camera)
    Light,   // a thing that lights the others
    Square,  // a thing on a 2D plane
    Effect,  // a sparkle (パーティクル)
    Scene,   // layers (a game's scene: the シーン list)
    // files (the アセット panel)
    Folder,
    File,
    Image,
    Model,
    Audio,
    Font,
    Text,
};
// By name, for plugins that cannot see this header (a component's ComponentInfo::icon): "object",
// "cube", "camera", "light", "square", "effect", "image", "audio"; anything else is None.
Icon IconNamed(std::string_view name);

struct Item {
    ItemId id = 0;
    ItemId parent = 0;     // 0 = top level
    std::string name;      // UTF-8
    bool canHaveChildren = true;
    bool dimmed = false;   // drawn faint (e.g. a disabled entity)
    Icon icon = Icon::None;
    std::wstring badge;    // a word on the row's right, in the accent (the シーン list's 最初); empty: none
};
// A software lists its items in display order: a parent before its children, siblings in order.

struct Row {
    size_t item = 0;  // index into the item list
    int depth = 0;
    bool hasChildren = false;
    bool expanded = false;  // children are shown
};

// The rows to draw. `collapsed`: items whose children are hidden. A non-empty `filter` shows the
// items whose name contains it (ASCII letters ignore case) and their ancestors, all opened.
std::vector<Row> VisibleRows(std::span<const Item> items, const std::unordered_set<ItemId>& collapsed, std::string_view filter);
bool NameMatches(std::string_view name, std::string_view filter);

// Index of an item in the list, or npos. Looks through the list: for many lookups, ItemIndex.
size_t Find(std::span<const Item> items, ItemId id);

// id -> position in the list, made once (per frame) so that finding by id costs nothing. Every
// function here that looks things up for each item or each selected one uses it (or a set): a
// tree of thousands of items, all selected, stays light (no work that grows with items x selected).
class ItemIndex {
public:
    explicit ItemIndex(std::span<const Item> items);
    size_t Find(ItemId id) const;  // npos when missing
    bool Has(ItemId id) const { return map_.contains(id); }

private:
    std::unordered_map<ItemId, size_t> map_;
};
// True when `id` is `ancestor` itself or somewhere below it.
bool IsUnder(std::span<const Item> items, ItemId id, ItemId ancestor);

struct Selection {
    std::vector<ItemId> ids;  // in display order
    ItemId anchor = 0;        // where a Shift range starts
    ItemId focus = 0;         // the keyboard cursor / last clicked

    bool Contains(ItemId id) const;  // looks through ids: for every row of a frame, use Set()
    std::unordered_set<ItemId> Set() const { return {ids.begin(), ids.end()}; }
    bool operator==(const Selection&) const = default;
};

// A click on a row: plain = only it, Ctrl = toggle it, Shift = the range from the anchor over the
// visible rows, Ctrl+Shift = add that range.
void Click(Selection& sel, std::span<const Item> items, std::span<const Row> rows, ItemId id, bool ctrl, bool shift);
void SelectOnly(Selection& sel, ItemId id);
void SelectAll(Selection& sel, std::span<const Item> items, std::span<const Row> rows);
// Forgets ids that are gone and puts the rest in display order. Returns true when the ids changed.
bool Prune(Selection& sel, std::span<const Item> items);

// The selected items that move (or are copied / deleted) as a whole: an item whose ancestor is
// also selected goes along with that ancestor. In display order.
std::vector<ItemId> Roots(std::span<const Item> items, std::span<const ItemId> ids);

// Where dragged items go: under `parent`, at `index` among its children counted WITHOUT the moved
// items (so the software removes them first, then inserts there).
struct Drop {
    ItemId parent = 0;
    size_t index = 0;
    enum class Mark { Before, Inside, After, End } mark = Mark::End;  // what the view draws
    ItemId target = 0;                                                // the row the mark is on (not for End)
    bool operator==(const Drop&) const = default;
};

// rowIndex: the visible row under the mouse, `fraction`: 0 (its top) .. 1 (its bottom).
// Top quarter = before it, bottom quarter = after it (or first child when it is open), middle =
// inside it (at the end). nullopt when the drop would put an item inside itself.
std::optional<Drop> DropOnRow(std::span<const Item> items, std::span<const Row> rows, size_t rowIndex, float fraction,
                              std::span<const ItemId> moving);
// Below the last row: the end of the top level.
Drop DropAtEnd(std::span<const Item> items, std::span<const ItemId> moving);

}  // namespace rm::editor::tree
