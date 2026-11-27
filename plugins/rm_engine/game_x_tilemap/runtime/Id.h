#pragma once
// ゲーム制作 × タイルマップ (つなぎ) の ID - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::game_x_tilemap {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_x_tilemap"};

}  // namespace rm::game_x_tilemap
