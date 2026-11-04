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
};

// A list of IDs: [ { "author": ..., "name": ... }, ... ]. Used for "requires" and for the
// plugins recorded in a project file. Each entry needs both values (never guessed).
std::optional<std::vector<PluginId>> ParsePluginIdList(const json::Value& v, std::string* error);
std::optional<PluginManifest> ParsePluginManifest(const json::Value& v, std::string* error);

enum class PluginState {
    Enabled,
    NoManifest,           // plugin.json not found
    BadManifest,          // unreadable, wrong shape, invalid ID, two plugins with one ID
    IdMismatch,           // plugin.json and the code disagree
    MissingRequirement,   // a required plugin is not built in / installed
    RequirementDisabled,  // a required plugin exists but cannot be used
    Cycle,                // requirements go round in a circle
};

// One plugin built into the exe, with what was read from its plugin.json.
struct PluginCandidate {
    PluginId id;  // from the code
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

}  // namespace rm::editor
