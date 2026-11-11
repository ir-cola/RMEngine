#pragma once
// Panel layouts the user saved under a name (pure - tested in RMTests).
//
// One list per software, kept with that software's current layout in <project>/UserSettings/Layout.json:
//   "named": [ { "name": "絵を描く", "layout": { "main": ..., "floating": [...] }, ...unknown keys }, ... ]
// The entry's own fields are the RM_STRUCT below (saved and loaded by reflection); "layout" is the
// docking tree's own format (Layout::ToJson). Keys this version does not know are written back, and
// an element that is not an entry at all (no name) is kept as it is, unseen.
//
// Names are compared as typed after trimming spaces (also the full-width one), case and all.

#include "core/reflect/Data.h"
#include "editor/dock/DockLayout.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::dock {

RM_STRUCT()
struct NamedLayoutInfo {
    RM_PROPERTY() std::string name;
};

class NamedLayouts {
public:
    struct Entry {
        NamedLayoutInfo info;
        json::Value layout;          // as saved (Layout::ToJson); made into a Layout only when used
        reflect::Unknowns unknowns;  // keys of a newer version
    };
    enum class NameCheck { Ok, Empty, Taken };

    const std::vector<Entry>& Entries() const { return entries_; }
    bool Empty() const { return entries_.empty(); }
    const Entry* Find(std::string_view name) const;

    // Spaces at both ends removed. Empty / the name of another entry (except `self`) is refused.
    static std::string CleanName(std::string_view name);
    NameCheck Check(std::string_view name, std::string_view self = {}) const;
    // A name not used yet: base, "base 2", "base 3" ...
    std::string FreeName(std::string_view base) const;

    // Saves `layout` under `name`: replaces the entry of that name in place (its unknown keys stay)
    // or adds one at the end. Returns false (and does nothing) for an empty name.
    bool Put(std::string_view name, const Layout& layout);
    bool Rename(std::string_view from, std::string_view to);  // false: missing / refused name
    bool Remove(std::string_view name);
    // Moves the entry `name` (with its unknown keys) to the end of `to` - between a project's list and
    // the shared one. False (nothing moved): missing in `from`, or `to` has that name already.
    static bool Move(NamedLayouts& from, std::string_view name, NamedLayouts& to);
    // The saved layout, cleaned for the panels that exist now. nullopt: missing or unreadable.
    std::optional<Layout> Get(std::string_view name, const std::vector<std::string>& knownPanels) const;

    json::Value ToJson() const;
    static NamedLayouts FromJson(const json::Value& v, reflect::Warnings* warnings = nullptr);

private:
    std::vector<Entry> entries_;
    std::vector<json::Value> others_;  // elements that are not entries: written back unchanged
};

}  // namespace rm::editor::dock
