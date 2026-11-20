#pragma once
// 物理 のランタイム部分: 部品 (剛体、コライダー、コライダー (2D)、キャラクター)、物理を進める
// システム (固定ステップ)、スクリプト向けの部分 (RMPhysics.h)。plugins/RuntimePlugins.cpp に並べる。
// これを使う書き出したゲームは、RM_RUNTIME_ENTRY (Register.cpp) でリンクする。

#include "core/RuntimeRegistry.h"

namespace rm::physics {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::physics
