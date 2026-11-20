#pragma once
// The editor part of 天候 (listed in plugins/BuiltinPlugins.cpp): how the editor shows the scene setting
// 天候 - its テンプレート (快晴 / 雨 / 嵐 / 大雪 ...: a row above its fields that sets them at once).

#include "editor/plugin/Software.h"

namespace rm::weather {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::weather
