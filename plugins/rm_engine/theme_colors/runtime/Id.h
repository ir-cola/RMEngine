#pragma once
// 基本の色テーマパック (青 / 緑) の ID: テーマファイルだけ - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::theme_colors {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "theme_colors"};

}  // namespace rm::theme_colors
