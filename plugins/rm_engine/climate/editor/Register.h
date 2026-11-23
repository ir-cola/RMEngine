#pragma once
// 気候 のエディター側 (plugins/BuiltinPlugins.cpp に並ぶ): シーンビューに気候エリアの枠を線で描く
// (ビューの「形」のスイッチ: 選択 / 全部。縁と、境目の内側の縁)。「気候」タブ (ClimateTab.h) を出す。

#include "editor/plugin/Software.h"

namespace rm::climate {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::climate
