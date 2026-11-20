#pragma once
// コネクター パーティクル × 物理 の ID - ここに 1 度だけ書く (plugin.json にも書き、起動時に突き合わせる)。

#include "core/PluginId.h"

namespace rm::particles_x_physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "particles_x_physics"};

}  // namespace rm::particles_x_physics
