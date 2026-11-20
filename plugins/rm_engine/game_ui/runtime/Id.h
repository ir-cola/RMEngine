#pragma once
// ゲーム UI プラグインの ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::game_ui {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_ui"};

}  // namespace rm::game_ui
