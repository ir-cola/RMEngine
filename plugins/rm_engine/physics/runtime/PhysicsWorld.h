#pragma once
// 物理 - the physics world of a scene that plays: Jolt Physics behind a thin wall (nothing outside this
// plugin's .cpp files sees a Jolt type - design NewEngineBluePrint 9-f). One per playing scene, kept in
// the scene itself (Scene::PlayState: the copy that plays at ■ is thrown away with its world; a scene
// switched to gets a new one).
//
//   each fixed step (the system 物理, SystemPhase::Fixed - after the scripts' Tick):
//     1 the bodies made as the components are (made, made again when changed, gone when the entity /
//       the component / 有効 goes); where a script put a Transform, the body goes there
//     2 Jolt steps by the game settings' 物理の刻み, with the game settings' 重力
//     3 where the bodies went, back into the Transforms (動く bodies, characters)
//     4 what touched / parted: HookCall (OnHit / OnTriggerEnter / OnTriggerExit) for the scripts
//
// A 2D body (コライダー (2D) on a Transform 2D) is a Jolt body that stays on the plane: it moves in X and Y
// and turns about Z only (Jolt's AllowedDOFs) - 2D and 3D share one world.

#include "base/Math.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace rm::physics {

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // One fixed step of `dt` seconds over `scene`. `hooks` / `assets`: as SystemContext's (may be null).
    void Step(game_production::Scene& scene, float dt, Vec3 gravity, std::vector<game_production::HookCall>* hooks,
              const game_production::AssetAccess* assets);

    // --- for the scripts (RMPhysics.h). An entity without a body yet gets it here (a bullet made in
    // Tick can be given its speed at once). False / nullopt: it has no body (no collider, a trigger
    // for forces ...). Forces and impulses act on 動く bodies only.
    bool AddForce(game_production::Scene& scene, int entity, Vec3 force, bool impulse);
    std::optional<Vec3> Velocity(game_production::Scene& scene, int entity);
    bool SetVelocity(game_production::Scene& scene, int entity, Vec3 velocity);
    struct RayHit {
        int entity = 0;
        Vec3 point{0, 0, 0}, normal{0, 0, 0};
        float distance = 0;
    };
    // The first thing (not a trigger) along the ray from `origin` towards `direction`, at most
    // `maxDistance` away. Characters are hit too (their body).
    std::optional<RayHit> Raycast(Vec3 origin, Vec3 direction, float maxDistance) const;

    // Characters (キャラクター): walk with `velocity` (its horizontal part) until told otherwise; jump
    // when on the ground; where they stand.
    bool Move(game_production::Scene& scene, int entity, Vec3 velocity);
    bool Jump(game_production::Scene& scene, int entity, float speed);
    bool Grounded(int entity) const;
    std::optional<Vec3> GroundNormal(int entity) const;

    // How many bodies / characters there are now (tests, the console).
    size_t Bodies() const;
    size_t Characters() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// The physics world of a playing scene (made the first time).
PhysicsWorld& WorldOf(game_production::Scene& scene);

// The system 物理 (SystemPhase::Fixed): PhysicsWorld::Step of the scene's world.
void RunPhysics(game_production::SystemContext& ctx);

}  // namespace rm::physics
