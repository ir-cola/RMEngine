// スクリプト API の 3D の部分 (RM3D.h / RM3DAbi.h): Transform。遊んでいるシーンの上で。
// RmApi::ctx はゲーム制作の ScriptWorld。

#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"

#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <limits>

namespace rm::game_x_3d {

namespace {

using game_production::ScriptWorld;

const space3d::Transform* TransformIn(const game_production::Scene& scene, int id) {
    const game_production::SceneEntity* e = scene.Find(id);
    const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<space3d::Transform>().name) : nullptr;
    return c && c->Known() ? static_cast<const space3d::Transform*>(c->value.Get()) : nullptr;
}

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
    game_production::Pose pose;
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max()) || !Pose3D(*w.scene, static_cast<int>(id), pose)) return 0;
    out[0] = pose.position.x;
    out[1] = pose.position.y;
    out[2] = pose.position.z;
    return 1;
}

constexpr RmTransform3DApi kTable{&Get, &Set, &WorldPosition};

// 一番上から `id` の親まで (self = false)、または `id` まで (true) の Transform のワールド行列。
// Transform の無い親は飛ばす。1 つも無ければ単位行列。
space3d::Mat4 ChainMatrix(const game_production::Scene& scene, int id, bool self) {
    std::vector<const space3d::Transform*> chain;
    size_t steps = 0;
    const game_production::SceneEntity* start = scene.Find(id);
    if (start && !self) start = scene.Find(start->data.parent);
    for (const game_production::SceneEntity* e = start; e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
        if (const space3d::Transform* t = TransformIn(scene, e->data.id)) chain.push_back(t);
    }
    space3d::Mat4 m;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) m = space3d::WorldMatrix(m, **it);
    return m;
}

// エンティティの Transform とワールド行列 (null: 無い)。
space3d::Transform* WorldOf(void* ctx, uint64_t id, space3d::Mat4& world) {
    space3d::Transform* t = TransformOf(ctx, id);
    if (!t) return nullptr;
    world = ChainMatrix(*static_cast<ScriptWorld*>(ctx)->scene, static_cast<int>(id), true);
    return t;
}

// ワールドの `want` に置く (親の分を補って Transform を作る)。
void PlaceInWorld(void* ctx, uint64_t id, space3d::Transform& t, const space3d::WorldPlacement& want) {
    const space3d::Mat4 parent = ChainMatrix(*static_cast<ScriptWorld*>(ctx)->scene, static_cast<int>(id), false);
    t = space3d::FromWorld(t, parent, want);
}

int WorldMatrix(void* ctx, uint64_t id, float out[16]) {
    space3d::Mat4 w;
    if (!WorldOf(ctx, id, w)) return 0;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) out[c * 4 + r] = w.m[c][r];
    }
    return 1;
}

int SetWorldPosition(void* ctx, uint64_t id, const float p[3]) {
    space3d::Mat4 w;
    space3d::Transform* t = WorldOf(ctx, id, w);
    if (!t) return 0;
    space3d::WorldPlacement want = space3d::Decompose(w, t->rotation);
    want.position = Vec3{p[0], p[1], p[2]};
    PlaceInWorld(ctx, id, *t, want);
    return 1;
}

int WorldRotation(void* ctx, uint64_t id, float out[3]) {
    space3d::Mat4 w;
    space3d::Transform* t = WorldOf(ctx, id, w);
    if (!t) return 0;
    const Vec3 r = space3d::Decompose(w, t->rotation).rotation;
    out[0] = r.x;
    out[1] = r.y;
    out[2] = r.z;
    return 1;
}

int SetWorldRotation(void* ctx, uint64_t id, const float d[3]) {
    space3d::Mat4 w;
    space3d::Transform* t = WorldOf(ctx, id, w);
    if (!t) return 0;
    space3d::WorldPlacement want = space3d::Decompose(w, t->rotation);
    want.rotation = Vec3{d[0], d[1], d[2]};
    PlaceInWorld(ctx, id, *t, want);
    return 1;
}

int LookAt(void* ctx, uint64_t id, const float target[3], const float up[3]) {
    space3d::Mat4 w;
    space3d::Transform* t = WorldOf(ctx, id, w);
    if (!t) return 0;
    space3d::WorldPlacement want = space3d::Decompose(w, t->rotation);
    const Vec3 forward = space3d::Normalize(space3d::operator-(Vec3{target[0], target[1], target[2]}, want.position));
    if (space3d::Length(forward) == 0) return 1;  // (目標の所にいる: 向く方向が無い)
    const Vec3 z = forward;  // (左手系: もの自身の +Z が前)
    Vec3 x = space3d::Normalize(space3d::Cross(Vec3{up[0], up[1], up[2]}, z));
    if (space3d::Length(x) == 0) x = space3d::Normalize(space3d::Cross(std::fabs(z.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, z));
    const Vec3 y = space3d::Cross(z, x);
    space3d::Mat4 r;
    const Vec3 cols[3] = {x, y, z};
    for (int c = 0; c < 3; ++c) {
        r.m[c][0] = cols[c].x;
        r.m[c][1] = cols[c].y;
        r.m[c][2] = cols[c].z;
    }
    want.rotation = space3d::EulerFromRotation(r, want.rotation);
    PlaceInWorld(ctx, id, *t, want);
    return 1;
}

constexpr RmTransform3DApi2 kTable2{&Get, &Set, &WorldPosition, &WorldMatrix, &SetWorldPosition, &WorldRotation, &SetWorldRotation, &LookAt};

}  // namespace

void KeepWorld3D(game_production::Scene& scene, int entity, int newParent) {
    game_production::SceneEntity* e = scene.Find(entity);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<space3d::Transform>().name) : nullptr;
    if (!c || !c->Known()) return;
    auto& t = *static_cast<space3d::Transform*>(c->value.Get());
    const space3d::Mat4 world = ChainMatrix(scene, entity, true);
    const space3d::Mat4 parent = newParent != 0 ? ChainMatrix(scene, newParent, true) : space3d::Mat4{};
    t = space3d::FromWorld(t, parent, space3d::Decompose(world, t.rotation));
}

const game_production::ScriptApiPart& ScriptTransform3DPart2() {
    static const game_production::ScriptApiPart part{RM_PART_TRANSFORM3D, RM_PART_TRANSFORM3D_VERSION2, &kTable2};
    return part;
}

space3d::Mat4 WorldMatrixOf(const game_production::Scene& scene, int entity, bool self) { return ChainMatrix(scene, entity, self); }

bool Pose3D(const game_production::Scene& scene, int entity, game_production::Pose& out) {
    if (!TransformIn(scene, entity)) return false;
    // 一番上の親から下へ、Transform を並べる (持たない親は飛ばす)。
    std::vector<const space3d::Transform*> chain;
    size_t steps = 0;
    for (const game_production::SceneEntity* e = scene.Find(entity); e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
        if (const space3d::Transform* t = TransformIn(scene, e->data.id)) chain.push_back(t);
    }
    space3d::Mat4 m;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) m = space3d::WorldMatrix(m, **it);
    out.position = space3d::TransformPoint(m, {0, 0, 0});
    // 見る向き: -Z、Y が上 (カメラと同じ: space3d::ViewFromWorld)。
    const Vec3 f = space3d::Normalize(space3d::TransformDirection(m, {0, 0, 1}));
    const Vec3 u = space3d::Normalize(space3d::TransformDirection(m, {0, 1, 0}));
    out.forward = space3d::Length(f) > 0 ? f : Vec3{0, 0, 1};
    out.up = space3d::Length(u) > 0 ? u : Vec3{0, 1, 0};
    return true;
}

const game_production::ScriptApiPart& ScriptTransform3DPart() {
    static const game_production::ScriptApiPart part{RM_PART_TRANSFORM3D, RM_PART_TRANSFORM3D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_3d
