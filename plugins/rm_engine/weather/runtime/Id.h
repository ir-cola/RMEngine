#pragma once
// The ID of the plugin 天候 - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::weather {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "weather"};

}  // namespace rm::weather
