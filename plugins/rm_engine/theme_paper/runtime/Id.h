#pragma once
// 紙 テーマの ID: テーマファイルだけ - ここに 1 回だけ書く (plugin.json にも書き、起動時に比べる)。

#include "core/PluginId.h"

namespace rm::theme_paper {

inline constexpr PluginIdLiteral kPluginId{"rm_engine", "theme_paper"};

}  // namespace rm::theme_paper
