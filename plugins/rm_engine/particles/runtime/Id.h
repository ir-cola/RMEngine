#pragma once
// The ID of the particle plugin (パーティクル) - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::particles {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "particles"};

}  // namespace rm::particles
