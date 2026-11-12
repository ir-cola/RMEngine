// The 2D part of the scripts' API (RM2D.h / RM2DAbi.h): Transform 2D, on the scene that plays.
// RmApi::ctx is game production's ScriptWorld.

#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"

#include "rm_engine/game_x_2d/runtime/sdk/RM2DAbi.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

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

}  // namespace

const game_production::ScriptApiPart& ScriptTransform2DPart() {
    static const game_production::ScriptApiPart part{RM_PART_TRANSFORM2D, RM_PART_TRANSFORM2D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_2d
