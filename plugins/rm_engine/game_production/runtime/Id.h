#pragma once
// ゲーム制作プラグインの ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::game_production {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_production"};

}  // namespace rm::game_production
