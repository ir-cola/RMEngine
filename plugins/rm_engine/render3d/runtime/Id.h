#pragma once
// The ID of the 3D rendering plugin - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::render3d {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "render3d"};

}  // namespace rm::render3d
