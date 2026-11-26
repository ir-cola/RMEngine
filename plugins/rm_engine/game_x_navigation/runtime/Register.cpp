#include "rm_engine/game_x_navigation/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_navigation/runtime/Components.h"

namespace rm::game_x_navigation {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // 部品: データだけ (段階 1 はエディターで焼く。ゲームの中で道を探すのは段階 2: docs/NAVIGATION.md 10 章)。
    static const ComponentInfo kVolume{.type = &reflect::TypeOf<NavMeshVolume>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kVolume);
    static const ComponentInfo kGeometry{.type = &reflect::TypeOf<NavGeometry>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kGeometry);
    static const ComponentInfo kArea{.type = &reflect::TypeOf<NavArea>, .space = "3d", .folder = "ナビ"};
    registry.Contribute(kComponentPoint, kArea);
    // シーンの設定「ナビ」: エージェントの型とエリアの種類 (「ナビ」タブで変える)。
    static const game_production::SceneSettingInfo kSettings{.type = &reflect::TypeOf<NavSettings>};
    registry.Contribute(game_production::kSceneSettingPoint, kSettings);
}

}  // namespace rm::game_x_navigation

RM_RUNTIME_ENTRY(rm_engine, game_x_navigation, rm::game_x_navigation::RegisterRuntime)
