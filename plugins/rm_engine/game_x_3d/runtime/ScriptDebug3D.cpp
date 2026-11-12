// The 3D debug drawing part of the scripts' API (RM3D.h's rm::Debug). RmApi::ctx is game
// production's ScriptWorld; its `debug` is the editor's buffer while it plays, null in a game.

#include "rm_engine/game_x_3d/runtime/ScriptDebug3D.h"

#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"

#include <cmath>
#include <format>

namespace rm::game_x_3d {

namespace {

using game_production::DebugSpace;
using game_production::ScriptWorld;

bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Vec3 V(const float p[3]) { return {p[0], p[1], p[2]}; }
Color C(const float c[4]) { return {c[0], c[1], c[2], c[3]}; }

// One line into the buffer (a line that is not a place - NaN, infinity - is left out). Full: told once.
void Put(ScriptWorld& w, Vec3 a, Vec3 b, Color color, float seconds) {
    if (!Finite(a) || !Finite(b)) return;
    if (!w.debug->Add(DebugSpace::Space3D, a, b, color, seconds) && w.debug->TakeOverflowNotice() && w.log) {
        w.log(1, std::format("デバッグ描画の線が多すぎます ({} 本まで)。それより多い分は描きません", game_production::DebugDraw::kMaxLines));
    }
}

// The world the call came from, when it draws (null: an exported game - nothing to do).
ScriptWorld* Drawing(void* ctx) {
    auto* w = static_cast<ScriptWorld*>(ctx);
    return w && w->debug ? w : nullptr;
}

void Line(void* ctx, const float a[3], const float b[3], const float color[4], float seconds) {
    if (ScriptWorld* w = Drawing(ctx)) Put(*w, V(a), V(b), C(color), seconds);
}

void Box(void* ctx, const float center[3], const float size[3], const float color[4], float seconds) {
    ScriptWorld* w = Drawing(ctx);
    if (!w) return;
    const Vec3 c = V(center), h{size[0] * 0.5f, size[1] * 0.5f, size[2] * 0.5f};
    Vec3 k[8];
    for (int i = 0; i < 8; ++i) k[i] = {c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z)};
    static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : kEdges) Put(*w, k[e[0]], k[e[1]], C(color), seconds);
}

void Sphere(void* ctx, const float center[3], float radius, const float color[4], float seconds) {
    ScriptWorld* w = Drawing(ctx);
    if (!w || !(radius >= 0) || !std::isfinite(radius)) return;
    const Vec3 c = V(center);
    // Round X (in Y-Z), round Y (in X-Z) and round Z (in X-Y).
    for (int axis = 0; axis < 3; ++axis) {
        auto at = [&](int i) {
            const float t = 6.2831853f * static_cast<float>(i) / kDebugSphereSegments;
            const float u = std::cos(t) * radius, v = std::sin(t) * radius;
            return axis == 0 ? Vec3{c.x, c.y + u, c.z + v} : axis == 1 ? Vec3{c.x + u, c.y, c.z + v} : Vec3{c.x + u, c.y + v, c.z};
        };
        for (int i = 0; i < kDebugSphereSegments; ++i) Put(*w, at(i), at(i + 1), C(color), seconds);
    }
}

void Ray(void* ctx, const float origin[3], const float direction[3], float length, const float color[4], float seconds) {
    ScriptWorld* w = Drawing(ctx);
    if (!w) return;
    const Vec3 d = V(direction);
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!(len > 0) || !std::isfinite(len)) return;  // no direction: nothing
    const float k = length / len;
    const Vec3 o = V(origin);
    Put(*w, o, {o.x + d.x * k, o.y + d.y * k, o.z + d.z * k}, C(color), seconds);
}

constexpr RmDebug3DApi kTable{&Line, &Box, &Sphere, &Ray};

}  // namespace

const game_production::ScriptApiPart& ScriptDebug3DPart() {
    static const game_production::ScriptApiPart part{RM_PART_DEBUG3D, RM_PART_DEBUG3D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_3d
