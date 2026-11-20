#pragma once
// 天候 のランタイム側 (plugins/RuntimePlugins.cpp に並ぶ。書き出したゲームは
// RM_RUNTIME_ENTRY から呼ぶ): シーン設定の 天候 (Weather.h)、ゲームが動く間それを進める
// システム、空に伝えること (ゲーム制作 × 3D の SkyWeather)、3D の絵への描画 (WeatherDraw.h)、
// スクリプト側 (sdk/RMWeather.h)。

#include "core/RuntimeRegistry.h"

namespace rm::weather {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::weather
