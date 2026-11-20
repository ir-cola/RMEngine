#pragma once
// 天候のエディター側 (plugins/BuiltinPlugins.cpp に並ぶ): エディターがシーン設定の 天候 を
// どう見せるか - そのテンプレート (快晴 / 雨 / 嵐 / 大雪 ...: 欄の上の行で、まとめて設定する)。

#include "editor/plugin/Software.h"

namespace rm::weather {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::weather
