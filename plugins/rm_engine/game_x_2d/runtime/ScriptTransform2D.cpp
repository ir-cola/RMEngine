// スクリプト API の 2D の部分 (RM2D.h / RM2DAbi.h): 動いているシーンの Transform 2D。
// RmApi::ctx は ゲーム制作 の ScriptWorld。

#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"

#include "rm_engine/game_x_2d/runtime/sdk/RM2DAbi.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

#include <cmath>
#include <limits>

namespace rm::game_x_2d {

namespace {

space2d::Transform2D* TransformOf(void* ctx, uint64_t id) {
    auto& w = *static_cast<game_production::ScriptWorld*>(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return nullptr;
    game_production::SceneEntity* e = w.scene->Find(static_cast<int>(id));
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<space2d::Transform2D>().name) : nullptr;
    return c && c->Known() ? static_cast<space2d::Transform2D*>(c->value.Get()) : nullptr;
}

int Get(void* ctx, uint64_t id, int which, float out[2]) {
    space2d::Transform2D* t = TransformOf(ctx, id);
    if (!t || which < 0 || which > 2) return 0;
    if (which == 1) {
        out[0] = t->rotation;
        out[1] = 0;
    } else {
        const Vec2 v = which == 0 ? t->position : t->scale;
        out[0] = v.x;
        out[1] = v.y;
    }
    return 1;
}

int Set(void* ctx, uint64_t id, int which, const float v[2]) {
    space2d::Transform2D* t = TransformOf(ctx, id);
    if (!t || which < 0 || which > 2) return 0;
    if (which == 1) {
        t->rotation = v[0];
    } else {
        (which == 0 ? t->position : t->scale) = Vec2{v[0], v[1]};
    }
    return 1;
}

constexpr RmTransform2DApi kTable{&Get, &Set};

space2d::Transform2D* TransformIn(game_production::Scene& scene, int id) {
    game_production::SceneEntity* e = scene.Find(id);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<space2d::Transform2D>().name) : nullptr;
    return c && c->Known() ? static_cast<space2d::Transform2D*>(c->value.Get()) : nullptr;
}

// 一番上から `id` の親 (self = false) または `id` 自身 (true) までの Transform 2D の
// ワールド行列と順番。Transform 2D のない親は飛ばす。
space2d::Mat3 ChainMatrix(game_production::Scene& scene, int id, bool self, int& order) {
    std::vector<const space2d::Transform2D*> chain;
    size_t steps = 0;
    const game_production::SceneEntity* start = scene.Find(id);
    if (start && !self) start = scene.Find(start->data.parent);
    for (const game_production::SceneEntity* e = start; e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
        if (const space2d::Transform2D* t = TransformIn(scene, e->data.id)) chain.push_back(t);
    }
    space2d::Mat3 m;
    order = 0;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        m = space2d::WorldMatrix(m, **it);
        order += (*it)->order;
    }
    return m;
}

game_production::Scene* SceneOf(void* ctx) { return static_cast<game_production::ScriptWorld*>(ctx)->scene; }

int WorldGet(void* ctx, uint64_t id, int which, float out[2]) {
    space2d::Transform2D* t = TransformOf(ctx, id);
    if (!t || which < 0 || which > 2) return 0;
    int order = 0;
    const space2d::WorldPlacement2D p = space2d::Decompose(ChainMatrix(*SceneOf(ctx), static_cast<int>(id), true, order), t->rotation);
    const Vec2 v = which == 0 ? p.position : which == 1 ? Vec2{p.rotation, 0} : p.scale;
    out[0] = v.x;
    out[1] = v.y;
    return 1;
}

// `want` に置く (ワールドの順番は保つ)。親の下で。
void Place(void* ctx, uint64_t id, space2d::Transform2D& t, space2d::WorldPlacement2D want) {
    int parentOrder = 0;
    const space2d::Mat3 parent = ChainMatrix(*SceneOf(ctx), static_cast<int>(id), false, parentOrder);
    want.order = parentOrder + t.order;
    t = space2d::FromWorld(t, parent, parentOrder, want);
}

int WorldSet(void* ctx, uint64_t id, int which, const float v[2]) {
    space2d::Transform2D* t = TransformOf(ctx, id);
    if (!t || which < 0 || which > 2) return 0;
    int order = 0;
    space2d::WorldPlacement2D want = space2d::Decompose(ChainMatrix(*SceneOf(ctx), static_cast<int>(id), true, order), t->rotation);
    if (which == 0) want.position = Vec2{v[0], v[1]};
    if (which == 1) want.rotation = v[0];
    if (which == 2) want.scale = Vec2{v[0], v[1]};
    Place(ctx, id, *t, want);
    return 1;
}

int LookAt(void* ctx, uint64_t id, const float target[2]) {
    space2d::Transform2D* t = TransformOf(ctx, id);
    if (!t) return 0;
    int order = 0;
    space2d::WorldPlacement2D want = space2d::Decompose(ChainMatrix(*SceneOf(ctx), static_cast<int>(id), true, order), t->rotation);
    const float dx = target[0] - want.position.x, dy = target[1] - want.position.y;
    if (dx == 0 && dy == 0) return 1;
    const float angle = std::atan2(dy, dx) * 57.29577951308232f;
    want.rotation = angle + 360.f * std::round((want.rotation - angle) / 360.f);  // (今の値に近いほう)
    Place(ctx, id, *t, want);
    return 1;
}

constexpr RmTransform2DApi2 kTable2{&Get, &Set, &WorldGet, &WorldSet, &LookAt};

}  // namespace

void KeepWorld2D(game_production::Scene& scene, int entity, int newParent) {
    space2d::Transform2D* t = TransformIn(scene, entity);
    if (!t) return;
    int order = 0, parentOrder = 0;
    space2d::WorldPlacement2D want = space2d::Decompose(ChainMatrix(scene, entity, true, order), t->rotation);
    want.order = order;
    const space2d::Mat3 parent = newParent != 0 ? ChainMatrix(scene, newParent, true, parentOrder) : space2d::Mat3{};
    *t = space2d::FromWorld(*t, parent, parentOrder, want);
}

const game_production::ScriptApiPart& ScriptTransform2DPart2() {
    static const game_production::ScriptApiPart part{RM_PART_TRANSFORM2D, RM_PART_TRANSFORM2D_VERSION2, &kTable2};
    return part;
}

bool Pose2D(const game_production::Scene& scene, int entity, game_production::Pose& out) {
    auto transformIn = [&](int id) -> const space2d::Transform2D* {
        const game_production::SceneEntity* e = scene.Find(id);
        const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<space2d::Transform2D>().name) : nullptr;
        return c && c->Known() ? static_cast<const space2d::Transform2D*>(c->value.Get()) : nullptr;
    };
    if (!transformIn(entity)) return false;
    std::vector<const space2d::Transform2D*> chain;
    size_t steps = 0;
    for (const game_production::SceneEntity* e = scene.Find(entity); e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
        if (const space2d::Transform2D* t = transformIn(e->data.id)) chain.push_back(t);
    }
    space2d::Mat3 m;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) m = space2d::WorldMatrix(m, **it);
    const Vec2 p = space2d::TransformPoint(m, {0, 0});
    out.position = {p.x, p.y, 0};
    out.forward = {0, 0, -1};  // 画面の奥向き: 左は左、右は右
    out.up = {0, 1, 0};
    return true;
}

const game_production::ScriptApiPart& ScriptTransform2DPart() {
    static const game_production::ScriptApiPart part{RM_PART_TRANSFORM2D, RM_PART_TRANSFORM2D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_2d
