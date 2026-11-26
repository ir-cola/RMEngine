#pragma once
// アニメーションのエディターの部分 (plugins/BuiltinPlugins.cpp に並べる): 状態とルールのファイル (.rmanim) を
// アセットのパネルの「作成」で作り、インスペクターで編集できるようにする (核の口 core.asset_file_type)。

#include "editor/plugin/Software.h"

namespace rm::animation {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::animation
