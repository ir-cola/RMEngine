#include "rm_engine/physics/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/physics/runtime/ScriptPhysics.h"

namespace rm::physics {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // 部品: データだけ - ワールドが固定ステップごとに読む (手で足した)。
    static const ComponentInfo kRigidbody{.type = &reflect::TypeOf<Rigidbody>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kRigidbody);
    static const ComponentInfo kCollider{.type = &reflect::TypeOf<Collider>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kCollider);
    static const ComponentInfo kCollider2D{.type = &reflect::TypeOf<Collider2D>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kCollider2D);
    static const ComponentInfo kCharacter{.type = &reflect::TypeOf<Character>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kCharacter);
    // シーンの 重力 (シーンごとに自分の)。
    static const game_production::SceneSettingInfo kSettings{.type = &reflect::TypeOf<PhysicsSettings>};
    registry.Contribute(game_production::kSceneSettingPoint, kSettings);
    // ワールドはゲーム設定の 物理の刻み で進む (スクリプトの Tick のあと; フックはその後)。
    static const game_production::SystemInfo kSystem{"物理", game_production::SystemPhase::Fixed, 0, &RunPhysics};
    registry.Contribute(game_production::kSystemPoint, kSystem);
    // スクリプト: 力、速さ、レイ、キャラクター (sdk/RMPhysics.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptPhysicsPart());
}

}  // namespace rm::physics

RM_RUNTIME_ENTRY(rm_engine, physics, rm::physics::RegisterRuntime)
