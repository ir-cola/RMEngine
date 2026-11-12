// The 3D part of the scripts' API (RM3D.h / RM3DAbi.h): Transform, on the scene that plays.
// RmApi::ctx is game production's ScriptWorld.

#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"

#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <limits>

namespace rm::game_x_3d {

namespace {

using game_production::ScriptWorld;

game_production::SceneEntity* EntityOf(void* ctx, uint64_t id) {
    auto& w = *static_cast<ScriptWorld*>(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return nullptr;
    return w.scene->Find(static_cast<int>(id));
}

space3d::Transform* TransformOf(void* ctx, uint64_t id) {
    game_production::SceneEntity* e = EntityOf(ctx, id);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<space3d::Transform>().name) : nullptr;
    return c && c->Known() ? static_cast<space3d::Transform*>(c->value.Get()) : nullptr;
}

Vec3* Part(space3d::Transform& t, int which) { return which == 0 ? &t.position : which == 1 ? &t.rotation : &t.scale; }

int Get(void* ctx, uint64_t id, int which, float out[3]) {
    space3d::Transform* t = TransformOf(ctx, id);
    if (!t || which < 0 || which > 2) return 0;
    const Vec3 v = *Part(*t, which);
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
    return 1;
}

int Set(void* ctx, uint64_t id, int which, const float v[3]) {
    space3d::Transform* t = TransformOf(ctx, id);
    if (!t || which < 0 || which > 2) return 0;
    *Part(*t, which) = Vec3{v[0], v[1], v[2]};
    return 1;
}

int WorldPosition(void* ctx, uint64_t id, float out[3]) {
    auto& w = *static_cast<ScriptWorld*>(ctx);
    if (!TransformOf(ctx, id)) return 0;
    // The Transforms from the top parent down (parents without one are skipped).
    std::vector<const space3d::Transform*> chain;
    size_t steps = 0;
    for (const game_production::SceneEntity* e = EntityOf(ctx, id); e && steps++ <= w.scene->Entities().size(); e = w.scene->Find(e->data.parent)) {
        if (const space3d::Transform* t = TransformOf(ctx, static_cast<uint64_t>(e->data.id))) chain.push_back(t);
    }
    space3d::Mat4 m;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) m = space3d::WorldMatrix(m, **it);
    const Vec3 p = space3d::TransformPoint(m, {0, 0, 0});
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
    return 1;
}

constexpr RmTransform3DApi kTable{&Get, &Set, &WorldPosition};

}  // namespace

const game_production::ScriptApiPart& ScriptTransform3DPart() {
    static const game_production::ScriptApiPart part{RM_PART_TRANSFORM3D, RM_PART_TRANSFORM3D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_3d
