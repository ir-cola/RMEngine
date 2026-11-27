#pragma once
// タイルマッププラグインの ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::tilemap {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "tilemap"};

}  // namespace rm::tilemap
