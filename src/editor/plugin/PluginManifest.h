#pragma once
// plugin.json and deciding which plugins can be used (pure - tested in RMTests).
//
//   Plugins/<author>/<name>/plugin.json
//   {
//     "author": "rm_engine", "name": "game_ui", "displayName": "ゲーム UI",
//     "requires": [ { "author": "rm_engine", "name": "game_production" } ]
//   }
//
// The ID is written twice on purpose (design RME_PLATFORM 3-b, plan "A"): in the plugin's
// header (for C++) and in plugin.json (read without compiling). Loading compares the two and
// refuses a plugin whose IDs differ - the mismatch is always found, never silently picked.
// A plugin whose requirement is missing or unusable is not enabled, and says why.
//
// Connectors (design RME_PLATFORM 4-f, plan C): game production must not know 3D or 2D, so the
// parts that do live in a connector plugin ("ゲーム制作 × 3D") that requires both sides:
//   game production   "needsConnector": true          -> enabled only while one of its connectors is
//   the connector     "connectorFor": { game production }, "requires": [ game production, 3D 空間 ]
// The arrow goes one way only (connector -> both sides), so requirements never go round.
// A connector is installed automatically once everything it requires is (never chosen by hand).

#include "base/Json.h"
#include "core/PluginId.h"

#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

struct PluginManifest {
    PluginId id;
    std::string displayName;  // UTF-8
    std::vector<PluginId> requirements;
    bool needsConnector = false;            // enabled only while a connector for it is
    std::optional<PluginId> connectorFor;   // this plugin is a connector for that one (also in requirements)
};

// A list of IDs: [ { "author": ..., "name": ... }, ... ]. Used for "requires" and for the
// plugins recorded in a project file. Each entry needs both values (never guessed).
std::optional<std::vector<PluginId>> ParsePluginIdList(const json::Value& v, std::string* error);
std::optional<PluginManifest> ParsePluginManifest(const json::Value& v, std::string* error);

enum class PluginState {
    Enabled,
    NotInstalled,         // this engine has it, the user has not installed it
    NoManifest,           // plugin.json not found
    BadManifest,          // unreadable, wrong shape, invalid ID, two plugins with one ID
    IdMismatch,           // plugin.json and the code disagree
    MissingRequirement,   // a required plugin is not in this engine at all
    RequirementDisabled,  // a required plugin exists but is not installed or cannot be used
    Cycle,                // requirements go round in a circle
    NoConnector,          // "needsConnector", and none of its connectors can be used
};

// One plugin built into the exe, with what was read from its plugin.json.
struct PluginCandidate {
    PluginId id;  // from the code
    bool installed = true;
    // Installed: its installed plugin.json. Not installed: the engine's copy (for the name only).
    std::optional<PluginManifest> manifest;
    bool manifestFound = false;
    std::string manifestError;  // when found but not usable
};

struct PluginStatus {
    PluginId id;
    std::string displayName;  // from plugin.json; the ID text when there is none
    PluginState state = PluginState::Enabled;
    std::string reason;  // Japanese, empty when enabled
};

struct PluginResolution {
    std::vector<PluginStatus> statuses;  // same order as the candidates
    std::vector<size_t> order;           // enabled candidates, requirements first
};

PluginResolution ResolvePlugins(const std::vector<PluginCandidate>& candidates);

// True for the states the Hub marks "⚠ 前提が無い" (installed, but a requirement is missing).
inline bool LacksRequirement(PluginState s) {
    return s == PluginState::MissingRequirement || s == PluginState::RequirementDisabled || s == PluginState::Cycle ||
           s == PluginState::NoConnector;
}

// --- Installing and removing (pure) ------------------------------------------------------
// Design RME_PLATFORM 1-b-2 / 5: requirements are installed together with a plugin; any installed
// plugin can be removed on its own, after a warning that lists who needs it.

// What to copy so that `id` can be used: every plugin of its requirement tree, `id` included,
// that is not in `ready` (installed and readable) - requirements first, each once. Ready plugins
// are looked through, so a requirement missing further down is found. Empty = nothing to copy.
// A plugin of the tree that is not in `available` (not in this engine) goes to *unavailable.
// Connectors come along: every connector whose requirements are then all there, and - for a plugin
// that needs a connector and would have none - the first connector this engine has for it, with
// its requirements (game production alone brings 3D 空間 and ゲーム制作 × 3D).
std::vector<PluginId> InstallPlan(const PluginId& id, const std::vector<PluginManifest>& available,
                                  const std::vector<PluginId>& ready, std::vector<PluginId>* unavailable);

struct Dependent {
    PluginId id;
    std::optional<PluginId> via;  // empty: needs it directly; else needs it through this plugin
};
// Installed plugins that stop working when `id` is removed: direct ones first, then the ones
// that need it through them. Each once. A plugin that loses its last connector counts too (via the connector).
std::vector<Dependent> Dependents(const PluginId& id, const std::vector<PluginManifest>& installed);

}  // namespace rm::editor
