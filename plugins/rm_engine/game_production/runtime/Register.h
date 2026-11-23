#pragma once
// ゲーム制作のランタイムの部分: 書き出したゲームにもこれが要る (エディターは plugins/RuntimePlugins.cpp
// から、書き出したゲームは GamePlayer.cpp の RegisterForGame から呼ぶ)。
// 出すのは、空間を知らない部品 (セーブする: WorldSave.h)。

#include "core/RuntimeRegistry.h"

namespace rm::game_production {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_production
