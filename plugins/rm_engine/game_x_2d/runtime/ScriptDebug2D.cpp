// The 2D debug drawing part of the scripts' API (RM2D.h's rm::Debug). RmApi::ctx is game
// production's ScriptWorld; its `debug` is the editor's buffer while it plays, null in a game.

#include "rm_engine/game_x_2d/runtime/ScriptDebug2D.h"

#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_x_2d/runtime/sdk/RM2DAbi.h"

#include <cmath>
#include <format>

namespace rm::game_x_2d {

namespace {

using game_production::DebugSpace;
using game_production::ScriptWorld;

Color C(const float c[4]) { return {c[0], c[1], c[2], c[3]}; }

void Put(ScriptWorld& w, float ax, float ay, float bx, float by, Color color, float seconds) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by)) return;
    if (!w.debug->Add(DebugSpace::Space2D, {ax, ay, 0}, {bx, by, 0}, color, seconds) && w.debug->TakeOverflowNotice() && w.log) {
        w.log(1, std::format("デバッグ描画の線が多すぎます ({} 本まで)。それより多い分は描きません", game_production::DebugDraw::kMaxLines));
    }
}

ScriptWorld* Drawing(void* ctx) {
    auto* w = static_cast<ScriptWorld*>(ctx);
    return w && w->debug ? w : nullptr;
}

void Line(void* ctx, const float a[2], const float b[2], const float color[4], float seconds) {
    if (ScriptWorld* w = Drawing(ctx)) Put(*w, a[0], a[1], b[0], b[1], C(color), seconds);
}

void Rect(void* ctx, const float center[2], const float size[2], const float color[4], float seconds) {
    ScriptWorld* w = Drawing(ctx);
    if (!w) return;
    const float x0 = center[0] - size[0] * 0.5f, x1 = center[0] + size[0] * 0.5f;
    const float y0 = center[1] - size[1] * 0.5f, y1 = center[1] + size[1] * 0.5f;
    Put(*w, x0, y0, x1, y0, C(color), seconds);
    Put(*w, x1, y0, x1, y1, C(color), seconds);
    Put(*w, x1, y1, x0, y1, C(color), seconds);
    Put(*w, x0, y1, x0, y0, C(color), seconds);
}

void Circle(void* ctx, const float center[2], float radius, const float color[4], float seconds) {
    ScriptWorld* w = Drawing(ctx);
    if (!w || !(radius >= 0) || !std::isfinite(radius)) return;
    auto x = [&](int i) { return center[0] + std::cos(6.2831853f * static_cast<float>(i) / kDebugCircleSegments) * radius; };
    auto y = [&](int i) { return center[1] + std::sin(6.2831853f * static_cast<float>(i) / kDebugCircleSegments) * radius; };
    for (int i = 0; i < kDebugCircleSegments; ++i) Put(*w, x(i), y(i), x(i + 1), y(i + 1), C(color), seconds);
}

constexpr RmDebug2DApi kTable{&Line, &Rect, &Circle};

}  // namespace

const game_production::ScriptApiPart& ScriptDebug2DPart() {
    static const game_production::ScriptApiPart part{RM_PART_DEBUG2D, RM_PART_DEBUG2D_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_2d
