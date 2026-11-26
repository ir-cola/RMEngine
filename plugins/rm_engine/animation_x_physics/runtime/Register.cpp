#include "rm_engine/animation_x_physics/runtime/Register.h"

#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_animation/runtime/RootMotionSink.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"

#include <cmath>

namespace rm::animation_x_physics {

bool TakeRootMotion(game_production::Scene& scene, int entity, Vec3 worldMove, float dt) {
    if (!(dt > 0) || !std::isfinite(dt)) return false;
    const game_production::SceneEntity* e = scene.Find(entity);
    const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<physics::Character>().name) : nullptr;
    if (!c || !c->Known()) return false;
    // 横だけ (縦は使わない: 重力とジャンプは物理。docs/ANIMATION.md 6 章の 11)。
    Vec3 v{worldMove.x / dt, 0, worldMove.z / dt};
    if (!std::isfinite(v.x) || !std::isfinite(v.z)) v = {0, 0, 0};
    return physics::WorldOf(scene).Move(scene, entity, v);
}

void RegisterRuntime(RuntimeRegistry& registry) {
    static const game_x_animation::RootMotionSinkInfo kSink{&TakeRootMotion};
    registry.Contribute(game_x_animation::kRootMotionPoint, kSink);
}

}  // namespace rm::animation_x_physics

RM_RUNTIME_ENTRY(rm_engine, animation_x_physics, rm::animation_x_physics::RegisterRuntime)
