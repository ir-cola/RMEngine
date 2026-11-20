#pragma once
// プラグイン 物理 の ID - ここに 1 度だけ書く (plugin.json にも書く)。

#include "core/PluginId.h"

namespace rm::physics {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "physics"};

}  // namespace rm::physics
