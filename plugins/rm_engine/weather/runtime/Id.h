#pragma once
// プラグイン 天候 の ID - ここに 1 度だけ書く (と plugin.json にも。起動時に比べる)。

#include "core/PluginId.h"

namespace rm::weather {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "weather"};

}  // namespace rm::weather
