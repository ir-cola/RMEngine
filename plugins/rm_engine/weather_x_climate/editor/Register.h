#pragma once
// つなぎ 天候 × 気候 のエディター側 (plugins/BuiltinPlugins.cpp に並ぶ): 気候のタブの「天候」の側に、
// シーンの設定「天候」を出す (気候の口 rm_engine.climate.tab_page)。

#include "editor/plugin/Software.h"

namespace rm::weather_x_climate {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::weather_x_climate
