#pragma once
// The core tree view (階層 panel). One piece of code draws every tree of every software
// (rule 3); a software only implements tree::Source.
//
//   click / Ctrl+click / Shift+click     select (range over the visible rows)
//   ▸ / ▾, ← / →                         close / open (← on a closed item goes to its parent)
//   ↑ / ↓ / Home / End (+Shift)          move the selection
//   drag rows                            move them: on the upper / lower edge of a row = before /
//                                        after it, on its middle = inside it, below the last row = the end;
//                                        out of the tree onto a reference field (Source::DragOut)
//   F2 / double-click                    rename in place (Enter / clicking elsewhere = done, Esc = cancel)
//   Ctrl+A                               select all
//   right-click                          the software's menu (+ 名前を変更)
//   search box                           shows the matching items and their parents
//
// The view never changes the software's data. Renaming and moving are requests the software
// turns into undo entries (rule 7); what the menu rows do is up to the software.

#include "base/Json.h"
#include "editor/lang/Lang.h"
#include "editor/tree/TreeModel.h"
#include "editor/widgets/Widgets.h"
#include "ui/Context.h"

#include <functional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace rm::editor::tree {

// A row of the right-click menu, optionally with a shortcut (works while the tree has the keys).
using Action = w::Action;

// The picture of a row (Item::icon) as the widgets draw it (the rows, and a reference field's list).
w::Icon PictureOf(Icon icon);

// What a software puts in the tree.
class Source {
public:
    virtual ~Source() = default;
    // Every item, in display order (see Item). Called whenever the tree is drawn.
    virtual void Items(std::vector<Item>& out) = 0;
    // Shown when there are no items.
    virtual std::wstring EmptyText() const { return Tr(L"まだ何もありません。"); }

    virtual bool CanRename(ItemId id) const {
        (void)id;
        return false;
    }
    virtual void Rename(ItemId id, const std::string& nameUtf8) {
        (void)id;
        (void)nameUtf8;
    }
    // Move `items` (Roots of the selection, in display order) under `parent` at `index` (see Drop).
    virtual bool CanMove() const { return false; }
    virtual void Move(std::span<const ItemId> items, ItemId parent, size_t index) {
        (void)items;
        (void)parent;
        (void)index;
    }
    // The selection changed (also when selected items disappeared, e.g. after undo).
    virtual void SelectionChanged(std::span<const ItemId> ids) { (void)ids; }
    // Menu rows for a right-click (on empty space the selection is empty) and their shortcuts.
    virtual std::vector<Action> Actions(std::span<const ItemId> selection) {
        (void)selection;
        return {};
    }
    // Rows dragged out of the tree onto another panel (a reference field of the インスペクター): what
    // they are (w::DragThing). The default: "item" with the ids (EntityRef fields take them).
    virtual w::DragThing DragOut(std::span<const ItemId> items);
    // How tall a row is (DIPs). Taller rows have a bigger picture (the アセット panel's thumbnails).
    virtual float RowHeight() const { return 24.f; }
    // Draws the row's picture itself into `r` (a thumbnail) and returns true; false: the core draws
    // the item's Icon.
    virtual bool DrawPicture(ui::Context& c, const Item& item, const ui::Rect& r) {
        (void)c;
        (void)item;
        (void)r;
        return false;
    }
};

// Per tree (one per software): selection, open / closed items, search, rename, drag.
struct State {
    Selection sel;
    std::unordered_set<ItemId> collapsed;
    std::string filter;

    // Set from outside (the software): applied at the next Draw, once the items are listed.
    bool hasPending = false;
    std::vector<ItemId> pending;
    bool pendingRename = false;

    ItemId renaming = 0;
    bool renameStart = false;
    std::string renameText;

    ItemId pressItem = 0;     // a row pressed with the left button, not yet released
    float pressX = 0, pressY = 0;
    bool pressDeferred = false;  // a press on a selected row: select only it on release (not after a drag)
    bool dragging = false;
    std::vector<ItemId> moving;

    bool hasKeys = false;     // the last click was in this tree
    w::Popup menu;
    std::vector<Action> menuActions;
    ItemId scrollTo = 0;      // keep this row in view on the next Draw
};

// Asks the tree to select `ids` (and, with rename, to start renaming the first) at the next Draw.
void RequestSelect(State& st, std::vector<ItemId> ids, bool rename = false);

// What is kept of a tree when the project is closed (UserSettings/Layout.json, with the layout - the
// user's way of looking, not project data): { "collapsed": [ids], "selected": [ids] }. Loading asks
// for the selection (applied at the next Draw; ids that are gone are dropped there, and closed ids
// of items that are gone are forgotten by Draw).
json::Value SaveState(const State& st);
void LoadState(State& st, const json::Value& v);

void Draw(ui::Context& c, State& st, Source& src, const ui::Rect& area);

}  // namespace rm::editor::tree
