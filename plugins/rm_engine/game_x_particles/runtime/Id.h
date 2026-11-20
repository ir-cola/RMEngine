#pragma once
// コネクター ゲーム制作 × パーティクル の ID - ここに 1 か所だけ書く (と plugin.json に。起動時に突き合わせる)。

#include "core/PluginId.h"

namespace rm::game_x_particles {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_x_particles"};

}  // namespace rm::game_x_particles
