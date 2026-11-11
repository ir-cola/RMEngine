#pragma once
// The ID of the 2D space plugin - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::space2d {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "space2d"};

}  // namespace rm::space2d
