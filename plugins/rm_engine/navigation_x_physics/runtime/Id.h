#pragma once
// プラグイン ナビ × 物理 の ID - ここに 1 度だけ書く (と plugin.json にも。起動時に比べる)。

#include "core/PluginId.h"

namespace rm::navigation_x_physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "navigation_x_physics"};

}  // namespace rm::navigation_x_physics
