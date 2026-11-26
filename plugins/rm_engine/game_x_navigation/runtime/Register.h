#pragma once
// ゲーム制作 × ナビゲーション のランタイム部分: シーンの設定「ナビ」と部品 (ナビメッシュ・ナビの形・ナビのエリア)。
// plugins/RuntimePlugins.cpp に並べる。これを使う書き出したゲームは、RM_RUNTIME_ENTRY (Register.cpp) でリンクする。

#include "core/RuntimeRegistry.h"

namespace rm::game_x_navigation {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_x_navigation
