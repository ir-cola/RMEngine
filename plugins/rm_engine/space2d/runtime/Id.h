#pragma once
// 2D 空間プラグインの ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::space2d {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "space2d"};

}  // namespace rm::space2d
