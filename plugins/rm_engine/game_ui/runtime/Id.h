#pragma once
// The ID of the game UI plugin - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::game_ui {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_ui"};

}  // namespace rm::game_ui
