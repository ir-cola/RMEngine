// RMPhysics.h - スクリプト用の物理 (プラグイン 物理 が提供する。中身は Jolt Physics)。
//
// エンティティに コライダー (か コライダー (2D)) を付け、動かしたいなら インスペクターで 剛体 も付ける。
// そのうえで:
//
//   #include "RMPhysics.h"
//   rm::Physics::AddImpulse(self, {0, 5, 0});          // 上へ蹴る (N·s)。すぐ効く
//   rm::Physics::AddForce(self, {10, 0, 0});           // 押す (N)。次の固定ステップの間かかる
//   rm::Vec3 v = rm::Physics::Velocity(self);          // m/s
//   rm::Physics::SetVelocity(bullet, {0, 0, -20});     // rm::Spawn で作ったばかりの弾。すぐ体を持つ
//   rm::Physics::RayHit hit;
//   if (rm::Physics::Raycast(from, {0, -1, 0}, 10.0f, &hit)) { hit.entity, hit.point, hit.normal, hit.distance }
//
// フック (スクリプトに書く。フレームの固定ステップのあとに呼ばれる):
//   void OnHit(rm::Entity self, rm::Entity other);           // 触れ始めた (離れるまでは 1 回)
//   void OnTriggerEnter(rm::Entity self, rm::Entity other);  // トリガー 付きの コライダー: 入ってきた ...
//   void OnTriggerExit(rm::Entity self, rm::Entity other);   // ... 出ていった
//
// キャラクター (キャラクター 部品: 坂や段を歩き、地面に立つ):
//   rm::Character::Move(self, {x, 0, z});      // 歩く速さ (m/s)。変えるまで続く (重力は別)
//   rm::Character::Jump(self, 5.0f);           // 秒速 5 m で上へ。地面にいるとき
//   bool onGround = rm::Character::Grounded(self);
//
// 2D でも同じ呼び出しが z = 0 で使える (2D の物体は平面に留まる): AddImpulse(self, {0, 5, 0})。
// プラグイン 物理 がない (または体のないエンティティ) と何も起きず、取得は 0 を返す。
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
// 光線に沿った最初のもの (トリガーは除く)。`maxDistance` までの距離で調べる。
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
// 足元の地面の法線 (何にも立っていないときは 0, 0, 0)。
inline Vec3 GroundNormal(Entity e) {
    float v[3] = {0, 0, 0};
    const RmPhysicsApi* a = Physics::detail::Api();
    if (a) a->characterGroundNormal(rm::Api()->ctx, e.id, v);
    return {v[0], v[1], v[2]};
}
}  // namespace Character

}  // namespace rm
