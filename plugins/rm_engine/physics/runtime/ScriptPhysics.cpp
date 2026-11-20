// スクリプト API の物理の部分 (sdk/RMPhysics.h / RMPhysicsAbi.h)。再生しているシーンに対して働く。
// RmApi::ctx はゲーム制作の ScriptWorld。

#include "rm_engine/physics/runtime/ScriptPhysics.h"

#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/physics/runtime/sdk/RMPhysicsAbi.h"

#include <limits>

namespace rm::physics {

namespace {

game_production::Scene* SceneOf(void* ctx) { return static_cast<game_production::ScriptWorld*>(ctx)->scene; }

bool IdOk(uint64_t id) { return id != 0 && id <= static_cast<uint64_t>(std::numeric_limits<int>::max()); }

int AddForceApi(void* ctx, uint64_t e, const float f[3], int impulse) {
    game_production::Scene* s = SceneOf(ctx);
    return s && IdOk(e) && WorldOf(*s).AddForce(*s, static_cast<int>(e), {f[0], f[1], f[2]}, impulse != 0) ? 1 : 0;
}

int GetVelocityApi(void* ctx, uint64_t e, float out[3]) {
    game_production::Scene* s = SceneOf(ctx);
    if (!s || !IdOk(e)) return 0;
    const auto v = WorldOf(*s).Velocity(*s, static_cast<int>(e));
    if (!v) return 0;
    out[0] = v->x, out[1] = v->y, out[2] = v->z;
    return 1;
}

int SetVelocityApi(void* ctx, uint64_t e, const float v[3]) {
    game_production::Scene* s = SceneOf(ctx);
    return s && IdOk(e) && WorldOf(*s).SetVelocity(*s, static_cast<int>(e), {v[0], v[1], v[2]}) ? 1 : 0;
}

int RaycastApi(void* ctx, const float o[3], const float d[3], float maxDistance, RmRayHit* hit) {
    game_production::Scene* s = SceneOf(ctx);
    if (!s) return 0;
    const auto h = WorldOf(*s).Raycast({o[0], o[1], o[2]}, {d[0], d[1], d[2]}, maxDistance);
    if (!h) return 0;
    if (hit) {
        hit->entity = static_cast<uint64_t>(std::max(h->entity, 0));
        hit->point[0] = h->point.x, hit->point[1] = h->point.y, hit->point[2] = h->point.z;
        hit->normal[0] = h->normal.x, hit->normal[1] = h->normal.y, hit->normal[2] = h->normal.z;
        hit->distance = h->distance;
    }
    return 1;
}

int MoveApi(void* ctx, uint64_t e, const float v[3]) {
    game_production::Scene* s = SceneOf(ctx);
    return s && IdOk(e) && WorldOf(*s).Move(*s, static_cast<int>(e), {v[0], v[1], v[2]}) ? 1 : 0;
}

int JumpApi(void* ctx, uint64_t e, float speed) {
    game_production::Scene* s = SceneOf(ctx);
    return s && IdOk(e) && WorldOf(*s).Jump(*s, static_cast<int>(e), speed) ? 1 : 0;
}

int GroundedApi(void* ctx, uint64_t e) {
    game_production::Scene* s = SceneOf(ctx);
    return s && IdOk(e) && WorldOf(*s).Grounded(static_cast<int>(e)) ? 1 : 0;
}

int GroundNormalApi(void* ctx, uint64_t e, float out[3]) {
    game_production::Scene* s = SceneOf(ctx);
    if (!s || !IdOk(e)) return 0;
    const auto n = WorldOf(*s).GroundNormal(static_cast<int>(e));
    if (!n) return 0;
    out[0] = n->x, out[1] = n->y, out[2] = n->z;
    return 1;
}

constexpr RmPhysicsApi kTable{&AddForceApi, &GetVelocityApi, &SetVelocityApi, &RaycastApi, &MoveApi, &JumpApi, &GroundedApi, &GroundNormalApi};

}  // namespace

const game_production::ScriptApiPart& ScriptPhysicsPart() {
    static const game_production::ScriptApiPart part{RM_PART_PHYSICS, RM_PART_PHYSICS_VERSION, &kTable};
    return part;
}

}  // namespace rm::physics
