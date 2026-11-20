#pragma once
// 季節テーマパック (春 / 夏 / 秋 / 冬) の ID: テーマファイルと、その絵のコード - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::theme_seasons {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "theme_seasons"};

}  // namespace rm::theme_seasons
