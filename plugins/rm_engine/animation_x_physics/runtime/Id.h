#pragma once
// コネクター アニメーション × 物理 の ID - ここに 1 度だけ書く (plugin.json にも書き、起動時に突き合わせる)。

#include "core/PluginId.h"

namespace rm::animation_x_physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "animation_x_physics"};

}  // namespace rm::animation_x_physics
