#pragma once
// The ID of the 3D space plugin - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::space3d {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "space3d"};

}  // namespace rm::space3d
