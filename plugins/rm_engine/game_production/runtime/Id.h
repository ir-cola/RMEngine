#pragma once
// The ID of the game production plugin - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::game_production {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_production"};

}  // namespace rm::game_production
