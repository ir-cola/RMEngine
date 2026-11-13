// RMPhysics.h - the physics for scripts (the plugin 物理 gives it: Jolt Physics inside).
//
// Give an entity a コライダー (or コライダー (2D)) and, to make it move, a 剛体 in the inspector. Then:
//
//   #include "RMPhysics.h"
//   rm::Physics::AddImpulse(self, {0, 5, 0});          // a kick up (N·s), at once
//   rm::Physics::AddForce(self, {10, 0, 0});           // a push (N), over the next fixed step
//   rm::Vec3 v = rm::Physics::Velocity(self);          // m/s
//   rm::Physics::SetVelocity(bullet, {0, 0, -20});     // a bullet just made with rm::Spawn: it has a body at once
//   rm::Physics::RayHit hit;
//   if (rm::Physics::Raycast(from, {0, -1, 0}, 10.0f, &hit)) { hit.entity, hit.point, hit.normal, hit.distance }
//
// The hooks (write them in the script; called after the frame's fixed steps):
//   void OnHit(rm::Entity self, rm::Entity other);           // began touching (once until they part)
//   void OnTriggerEnter(rm::Entity self, rm::Entity other);  // a コライダー with トリガー: came in ...
//   void OnTriggerExit(rm::Entity self, rm::Entity other);   // ... went out
//
// A character (the キャラクター component: walks on slopes and steps, stands on the ground):
//   rm::Character::Move(self, {x, 0, z});      // the walk in m/s, kept until changed (gravity is its own)
//   rm::Character::Jump(self, 5.0f);           // up at 5 m/s, when on the ground
//   bool onGround = rm::Character::Grounded(self);
//
// In 2D the same calls work with z = 0 (2D bodies stay on the plane): AddImpulse(self, {0, 5, 0}).
// Without the plugin 物理 (or for an entity with no body) nothing happens and the getters give zeros.
#pragma once

#include "RM.h"
#include "RMPhysicsAbi.h"

namespace rm {

namespace Physics {
namespace detail {
inline const RmPhysicsApi* Api() { return Part<RmPhysicsApi>(RM_PART_PHYSICS, RM_PART_PHYSICS_VERSION); }
}  // namespace detail

struct RayHit {
    Entity entity;
    Vec3 point{0, 0, 0}, normal{0, 0, 0};
    float distance = 0;
};

inline bool AddForce(Entity e, Vec3 force) {
    const float f[3] = {force.x, force.y, force.z};
    const RmPhysicsApi* a = detail::Api();
    return a && a->addForce(rm::Api()->ctx, e.id, f, 0) != 0;
}
inline bool AddImpulse(Entity e, Vec3 impulse) {
    const float f[3] = {impulse.x, impulse.y, impulse.z};
    const RmPhysicsApi* a = detail::Api();
    return a && a->addForce(rm::Api()->ctx, e.id, f, 1) != 0;
}
inline Vec3 Velocity(Entity e) {
    float v[3] = {0, 0, 0};
    const RmPhysicsApi* a = detail::Api();
    if (a) a->getVelocity(rm::Api()->ctx, e.id, v);
    return {v[0], v[1], v[2]};
}
inline bool SetVelocity(Entity e, Vec3 velocity) {
    const float v[3] = {velocity.x, velocity.y, velocity.z};
    const RmPhysicsApi* a = detail::Api();
    return a && a->setVelocity(rm::Api()->ctx, e.id, v) != 0;
}
// The first thing (not a trigger) along the ray, at most `maxDistance` away.
inline bool Raycast(Vec3 origin, Vec3 direction, float maxDistance, RayHit* hit = nullptr) {
    const float o[3] = {origin.x, origin.y, origin.z}, d[3] = {direction.x, direction.y, direction.z};
    RmRayHit h{};
    const RmPhysicsApi* a = detail::Api();
    if (!a || !a->raycast(rm::Api()->ctx, o, d, maxDistance, &h)) return false;
    if (hit) {
        hit->entity = Entity(h.entity);
        hit->point = {h.point[0], h.point[1], h.point[2]};
        hit->normal = {h.normal[0], h.normal[1], h.normal[2]};
        hit->distance = h.distance;
    }
    return true;
}
}  // namespace Physics

namespace Character {
inline bool Move(Entity e, Vec3 velocity) {
    const float v[3] = {velocity.x, velocity.y, velocity.z};
    const RmPhysicsApi* a = Physics::detail::Api();
    return a && a->characterMove(rm::Api()->ctx, e.id, v) != 0;
}
inline bool Jump(Entity e, float speed) {
    const RmPhysicsApi* a = Physics::detail::Api();
    return a && a->characterJump(rm::Api()->ctx, e.id, speed) != 0;
}
inline bool Grounded(Entity e) {
    const RmPhysicsApi* a = Physics::detail::Api();
    return a && a->characterGrounded(rm::Api()->ctx, e.id) != 0;
}
// The ground's normal under it (0, 0, 0 when not standing on anything).
inline Vec3 GroundNormal(Entity e) {
    float v[3] = {0, 0, 0};
    const RmPhysicsApi* a = Physics::detail::Api();
    if (a) a->characterGroundNormal(rm::Api()->ctx, e.id, v);
    return {v[0], v[1], v[2]};
}
}  // namespace Character

}  // namespace rm
