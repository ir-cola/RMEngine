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
    // The components: data only - the world reads them each fixed step (added by hand).
    static const ComponentInfo kRigidbody{.type = &reflect::TypeOf<Rigidbody>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kRigidbody);
    static const ComponentInfo kCollider{.type = &reflect::TypeOf<Collider>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kCollider);
    static const ComponentInfo kCollider2D{.type = &reflect::TypeOf<Collider2D>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kCollider2D);
    static const ComponentInfo kCharacter{.type = &reflect::TypeOf<Character>, .folder = "物理"};
    registry.Contribute(kComponentPoint, kCharacter);
    // The world steps in the game settings' 物理の刻み (after the scripts' Tick; the hooks after it).
    static const game_production::SystemInfo kSystem{"物理", game_production::SystemPhase::Fixed, 0, &RunPhysics};
    registry.Contribute(game_production::kSystemPoint, kSystem);
    // Scripts: forces, speeds, rays, characters (sdk/RMPhysics.h).
    registry.Contribute(game_production::kScriptApiPoint, ScriptPhysicsPart());
}

}  // namespace rm::physics

RM_RUNTIME_ENTRY(rm_engine, physics, rm::physics::RegisterRuntime)
