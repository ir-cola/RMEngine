#pragma once
// コネクター ゲーム制作 × アニメーション の ID - ここに 1 か所だけ書く (と plugin.json に。起動時に突き合わせる)。

#include "core/PluginId.h"

namespace rm::game_x_animation {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "game_x_animation"};

}  // namespace rm::game_x_animation
