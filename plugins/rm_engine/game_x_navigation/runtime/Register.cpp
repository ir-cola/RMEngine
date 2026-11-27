#include "rm_engine/game_x_navigation/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_navigation/runtime/Components.h"
#include "rm_engine/game_x_navigation/runtime/NavWorld.h"
#include "rm_engine/game_x_navigation/runtime/ScriptNav.h"

namespace rm::game_x_navigation {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // 部品: データだけ (焼くのはエディター。ゲームはシステム「ナビ」が焼いた物を読んで歩かせる: docs/NAVIGATION.md 10 章)。
    static const ComponentInfo kVolume{.type = &reflect::TypeOf<NavMeshVolume>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kVolume);
    static const ComponentInfo kGeometry{.type = &reflect::TypeOf<NavGeometry>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kGeometry);
    static const ComponentInfo kArea{.type = &reflect::TypeOf<NavArea>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kArea);
    static const ComponentInfo kAgent{.type = &reflect::TypeOf<NavAgent>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kAgent);
    // シーンの設定「ナビ」: エージェントの型とエリアの種類 (「ナビ」タブで変える)。
    static const game_production::SceneSettingInfo kSettings{.type = &reflect::TypeOf<NavSettings>};
    registry.Contribute(game_production::kSceneSettingPoint, kSettings);
    // システム「ナビ」(固定の刻み・物理の前): 焼いた物を読む (最初だけ)・費用と通れないエリア・動く者を歩かせる・着いたら NavArrived。
    static const game_production::SystemInfo kSystem{"ナビ", game_production::SystemPhase::Fixed, -10, &RunNavigation};
    registry.Contribute(game_production::kSystemPoint, kSystem);
    // スクリプト: 行き先・止まる・道だけを探す・一番近い点・でたらめな点・エリアの費用 (sdk/RMNav.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptNavPart());
}

}  // namespace rm::game_x_navigation

RM_RUNTIME_ENTRY(rm_engine, game_x_navigation, rm::game_x_navigation::RegisterRuntime)
