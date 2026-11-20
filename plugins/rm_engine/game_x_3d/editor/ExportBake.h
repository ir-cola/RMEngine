#pragma once
// 書き出すゲームの、3D モデルとその画像の焼き込み (ExportBake.cpp)。

#include "editor/plugin/Software.h"

namespace rm::game_x_3d {

void RegisterExportBake(editor::PluginRegistry& registry);

}  // namespace rm::game_x_3d
