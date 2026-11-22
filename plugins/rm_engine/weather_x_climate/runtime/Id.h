#pragma once
// コネクター 天候 × 気候 の ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::weather_x_climate {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "weather_x_climate"};

}  // namespace rm::weather_x_climate
