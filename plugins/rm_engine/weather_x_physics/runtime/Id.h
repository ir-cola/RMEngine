#pragma once
// コネクター 天候 × 物理 の ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::weather_x_physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "weather_x_physics"};

}  // namespace rm::weather_x_physics
