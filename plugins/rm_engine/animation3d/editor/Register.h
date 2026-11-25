#pragma once
// 3D アニメーションのエディターの部分 (plugins/BuiltinPlugins.cpp に並べる): 3D モデルのアセットの
// インスペクターに「クリップとイベント」のブロックを足す (核の口 core.asset_inspect。editor/ClipStrip.h)。

#include "editor/plugin/Software.h"

namespace rm::animation3d {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::animation3d
