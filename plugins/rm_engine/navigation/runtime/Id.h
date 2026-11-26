#pragma once
// プラグイン ナビゲーション の ID - ここに 1 度だけ書く (と plugin.json にも。起動時に比べる)。

#include "core/PluginId.h"

namespace rm::navigation {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "navigation"};

}  // namespace rm::navigation
