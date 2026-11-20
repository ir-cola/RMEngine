#pragma once
// ゲーム制作プラグインのエディター側: 「ゲーム」ソフトを足す。
// 今あるのは、ゲームの設定と、1 つのシーン。そのエンティティ (名前・スイッチ・親) は核の階層に
// 並び、核のインスペクターで編集する。シーンビュー・部品 (ECS)・Play などは後でここに来る
// (設計 RME_PLATFORM 4-b)。

#include "editor/plugin/Software.h"

namespace rm::game_production {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_production
