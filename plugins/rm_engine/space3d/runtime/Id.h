#pragma once
// 3D 空間プラグインの ID - ここに 1 回だけ書く (plugin.json にもあり、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::space3d {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "space3d"};

}  // namespace rm::space3d
