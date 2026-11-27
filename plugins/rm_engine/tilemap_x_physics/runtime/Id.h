#pragma once
// タイルマップ × 物理 (つなぎ) の ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::tilemap_x_physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "tilemap_x_physics"};

}  // namespace rm::tilemap_x_physics
