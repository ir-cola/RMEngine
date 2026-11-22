#pragma once
// 気候 のランタイム側 (plugins/RuntimePlugins.cpp に並ぶ。書き出したゲームは RM_RUNTIME_ENTRY から呼ぶ):
// シーンの設定「気候」・部品「気候エリア」(3D) /「気候エリア 2D」(Climate.h)、スクリプト側 (sdk/RMClimate.h)。

#include "core/RuntimeRegistry.h"

namespace rm::climate {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::climate
