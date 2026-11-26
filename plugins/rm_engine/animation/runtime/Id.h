#pragma once
// アニメーションプラグイン (アニメーション) の ID - ここに 1 度だけ書く (plugin.json にも書き、起動時に突き合わせる)。

#include "core/PluginId.h"

namespace rm::animation {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "animation"};

}  // namespace rm::animation
