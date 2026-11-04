#pragma once
// The inspector: draws the fields of any reflected object from its TypeDesc. There is no
// per-type inspector code - a plugin only describes its struct (design RME_PLATFORM 4-c:
// "the core shows, the software decides what").
//
// It never writes into the object. A change the user finishes (Enter / leaving the field /
// a click on a check box or a list item) comes back as an Edit, which the caller turns into a
// reflect::SetFieldCommand on the software's history - so every edit is undoable and marks the
// project unsaved (rule 7). Numbers go through reflect::NumberFromInput: the same Min / Max /
// rounding as loading a file (design R1 T3).
//
//   bool           check box          int / float     number field (Enter / leaving = set)
//   std::string    text field         Vec2/3, Color   one number field per component (+ a swatch)
//   enum           drop-down list (a name this version does not know shows as "名前 (不明)")
//   struct         its fields, indented          std::vector   read-only list for now

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "editor/widgets/Widgets.h"
#include "ui/Context.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace rm::editor::inspector {

struct Edit {
    reflect::FieldPath path;
    json::Value before, after;
    std::string label;  // for the undo history, e.g. 「目標 FPS」を変更
};

// Per inspector: text being typed, the open drop-down.
struct State {
    std::unordered_map<ui::Id, std::string> text;
    ui::Id editing = 0;
    w::Popup popup;
    reflect::FieldPath popupPath;
    std::optional<Edit> pending;  // chosen in the drop-down (drawn at the end of the frame)
    float contentHeight = 0;
};

// Draws into area (scrolls when needed). Returns a finished change, if any.
std::optional<Edit> Draw(ui::Context& c, State& st, const ui::Rect& area, const reflect::TypeDesc& type, const void* object,
                         const reflect::Unknowns* unknowns);

}  // namespace rm::editor::inspector
