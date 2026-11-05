#pragma once
// The inspector: draws the fields of reflected objects from their TypeDesc. There is no per-type
// inspector code - a plugin only describes its struct (design RME_PLATFORM 4-c: "the core shows,
// the software decides what").
//
// It shows a list of BLOCKS (the "lumps of values" the selected thing has - for an entity of a
// game: the entity itself, then each component). The core does not know what a block means: a
// block has a title, an optional menu (⋮: e.g. 外す) and its fields; under the blocks there can be
// an "add" button whose menu the software fills. The word "component" is the game's, not the core's.
//
// It never writes into an object. A change the user finishes (Enter / leaving the field / a click
// on a check box or a list item) comes back as an Edit (with the index of its block), which the
// caller turns into a reflect::SetFieldCommand on the software's history - so every edit is
// undoable and marks the project unsaved (rule 7). Numbers go through reflect::NumberFromInput: the
// same Min / Max / rounding as loading a file (design R1 T3).
//
//   bool           check box          int / float     number field (Enter / leaving = set)
//   std::string    text field         Vec2/3, Color   one number field per component (+ a swatch)
//   enum           drop-down list (a name this version does not know shows as "名前 (不明)")
//   struct         its fields, indented          std::vector   read-only list for now

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "editor/widgets/Widgets.h"
#include "ui/Context.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rm::editor::inspector {

struct Edit {
    size_t block = 0;
    reflect::FieldPath path;
    json::Value before, after;
    std::string label;  // for the undo history, e.g. 「目標 FPS」を変更
};

// One lump of values.
struct Block {
    const reflect::TypeDesc* type = nullptr;  // null: only `note` is shown (e.g. a component whose plugin is missing)
    void* object = nullptr;
    const reflect::Unknowns* unknowns = nullptr;
    std::wstring title;           // the header (folds the block when clicked); empty = no header
    std::wstring note;            // a line under the header
    std::vector<w::Action> menu;  // the header's ⋮ menu; empty = no button
};

// Per inspector: text being typed, the open drop-down / menu, folded blocks.
struct State {
    std::unordered_map<ui::Id, std::string> text;
    ui::Id editing = 0;
    w::Popup popup;  // a drop-down list of an enum field
    size_t popupBlock = 0;
    reflect::FieldPath popupPath;
    std::optional<Edit> pending;  // chosen in the drop-down (drawn at the end of the frame)
    w::Popup menu;                // a block's ⋮ menu or the add button's menu
    std::vector<w::Action> menuActions;
    std::unordered_set<std::wstring> folded;  // block titles
    float contentHeight = 0;
};

// Draws the blocks into `area` (scrolls when needed); `addLabel` non-empty = a button under them
// that opens the menu `addMenu` returns. Returns a finished change, if any.
std::optional<Edit> Draw(ui::Context& c, State& st, const ui::Rect& area, std::span<const Block> blocks,
                         std::wstring_view addLabel = {}, const std::function<std::vector<w::Action>()>& addMenu = {});

}  // namespace rm::editor::inspector
