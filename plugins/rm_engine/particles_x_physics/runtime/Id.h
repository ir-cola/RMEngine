#pragma once
// The ID of the connector パーティクル × 物理 - written here once (and in plugin.json, compared at start-up).

#include "core/PluginId.h"

namespace rm::particles_x_physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "particles_x_physics"};

}  // namespace rm::particles_x_physics
