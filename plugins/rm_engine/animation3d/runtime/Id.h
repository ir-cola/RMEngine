#pragma once
// 3D アニメーションプラグイン (3D アニメーション) の ID - ここに 1 度だけ書く (plugin.json にも書き、起動時に突き合わせる)。

#include "core/PluginId.h"

namespace rm::animation3d {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "animation3d"};

}  // namespace rm::animation3d
