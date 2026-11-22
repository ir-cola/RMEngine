#pragma once
// プラグイン 気候 の ID - ここに 1 度だけ書く (と plugin.json にも。起動時に比べる)。

#include "core/PluginId.h"

namespace rm::climate {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "climate"};

}  // namespace rm::climate
