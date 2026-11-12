// Tests for the scripts' debug drawing: game production's buffer (runtime/DebugDraw) and the
// connectors' parts that turn rm::Debug's shapes into its lines (game_x_3d / game_x_2d ScriptDebug*).

#include "Test.h"

#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_2d/runtime/ScriptDebug2D.h"
#include "rm_engine/game_x_2d/runtime/sdk/RM2DAbi.h"
#include "rm_engine/game_x_3d/runtime/ScriptDebug3D.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::game_production;

namespace {

const RmDebug3DApi& Debug3D() { return *static_cast<const RmDebug3DApi*>(game_x_3d::ScriptDebug3DPart().table); }
const RmDebug2DApi& Debug2D() { return *static_cast<const RmDebug2DApi*>(game_x_2d::ScriptDebug2DPart().table); }
constexpr float kRed[4] = {1, 0, 0, 1};

}  // namespace

RM_TEST(DebugDraw_OneFrameAndSeconds) {
    DebugDraw d;
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {1, 0, 0, 1}, 0));   // this frame only
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {0, 1, 0}, {0, 1, 0, 1}, 0.5));  // half a second
    CHECK(d.Add(DebugSpace::Space2D, {0, 0, 0}, {0, 0, 1}, {0, 0, 1, 1}, -3));  // less than 0: this frame
    CHECK(d.Lines().size() == 3);
    d.BeginFrame(0.2);  // the next frame: the one-frame lines go
    CHECK(d.Lines().size() == 1 && d.Lines()[0].color.g == 1);
    d.BeginFrame(0.2);  // 0.4 s: still
    CHECK(d.Lines().size() == 1);
    d.BeginFrame(0.0);  // a frame that took no time still drops this frame's lines, not the timed one
    CHECK(d.Lines().size() == 1);
    d.BeginFrame(0.2);  // 0.6 s: gone
    CHECK(d.Lines().empty());
    // A one-frame line is gone at the next frame even when no time passes.
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 1, 1}, {}, 0));
    d.BeginFrame(0);
    CHECK(d.Lines().empty());
    // NaN seconds: this frame only (never kept for good).
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 1, 1}, {}, std::numeric_limits<double>::quiet_NaN()));
    d.BeginFrame(1);
    CHECK(d.Lines().empty());
}

RM_TEST(DebugDraw_FullIsDroppedAndToldOnce) {
    DebugDraw d;
    for (size_t i = 0; i < DebugDraw::kMaxLines; ++i) d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 10);
    CHECK(!d.TakeOverflowNotice());  // full, but nothing dropped yet
    CHECK(!d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 0));
    CHECK(!d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 0));
    CHECK(d.Lines().size() == DebugDraw::kMaxLines && d.Dropped() == 2);
    CHECK(d.TakeOverflowNotice());   // told once ...
    CHECK(!d.TakeOverflowNotice());  // ... not again
    d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 0);
    CHECK(!d.TakeOverflowNotice());
    // Stop: all gone, and it may be told again in the next Play.
    d.Clear();
    CHECK(d.Lines().empty() && d.Dropped() == 0 && d.Clock() == 0);
}

RM_TEST(DebugDraw_ShapesBecomeLines) {
    DebugDraw d;
    ScriptWorld w;
    w.debug = &d;
    const float o[3] = {1, 2, 3}, size[3] = {2, 4, 6}, x[3] = {0, 0, 0}, dir[3] = {0, 0, 2};
    Debug3D().box(&w, o, size, kRed, 0);
    CHECK(d.Lines().size() == 12);
    // Its corners are the box's.
    float minX = 1e9f, maxY = -1e9f;
    for (const DebugLine& l : d.Lines()) {
        minX = std::min({minX, l.a.x, l.b.x});
        maxY = std::max({maxY, l.a.y, l.b.y});
        CHECK(l.space == DebugSpace::Space3D && l.color.r == 1 && l.oneFrame);
    }
    CHECK(minX == 0 && maxY == 4);
    d.Clear();
    Debug3D().sphere(&w, o, 0.5f, kRed, 1);
    CHECK(d.Lines().size() == 3 * game_x_3d::kDebugSphereSegments && !d.Lines()[0].oneFrame);
    d.Clear();
    Debug3D().ray(&w, x, dir, 5, kRed, 0);  // 5 along +Z, whatever the direction's length
    CHECK(d.Lines().size() == 1 && d.Lines()[0].b.z == 5);
    Debug3D().ray(&w, x, x, 5, kRed, 0);  // no direction: nothing
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float bad[3] = {nan, 0, 0};
    Debug3D().line(&w, x, bad, kRed, 0);  // not a place: nothing
    Debug3D().sphere(&w, o, -1, kRed, 0);  // a negative radius: nothing
    CHECK(d.Lines().size() == 1);
    // 2D: in the plane (z = 0).
    d.Clear();
    const float c2[2] = {0, 0}, s2[2] = {2, 1}, b2[2] = {3, 4};
    Debug2D().rect(&w, c2, s2, kRed, 0);
    Debug2D().circle(&w, c2, 1, kRed, 0);
    Debug2D().line(&w, c2, b2, kRed, 0);
    CHECK(d.Lines().size() == 4 + game_x_2d::kDebugCircleSegments + 1);
    for (const DebugLine& l : d.Lines()) CHECK(l.space == DebugSpace::Space2D && l.a.z == 0 && l.b.z == 0);
}

RM_TEST(DebugDraw_ExportedGameDoesNothing) {
    // An exported game gives its scripts a world without a buffer: every call returns at once.
    ScriptWorld w;
    CHECK(w.debug == nullptr);
    std::vector<std::string> told;
    w.log = [&](int, std::string_view t) { told.emplace_back(t); };
    const float p[3] = {0, 0, 0}, s[3] = {1, 1, 1}, p2[2] = {0, 0}, s2[2] = {1, 1};
    for (int i = 0; i < 1000; ++i) {
        Debug3D().line(&w, p, s, kRed, 0);
        Debug3D().box(&w, p, s, kRed, 5);
        Debug3D().sphere(&w, p, 1, kRed, 5);
        Debug3D().ray(&w, p, s, 1, kRed, 5);
        Debug2D().line(&w, p2, s2, kRed, 0);
        Debug2D().rect(&w, p2, s2, kRed, 0);
        Debug2D().circle(&w, p2, 1, kRed, 0);
    }
    CHECK(told.empty());
    // The parts are offered under their names and versions (what RM3D.h / RM2D.h ask for).
    const std::vector<ScriptApiPart> parts{game_x_3d::ScriptDebug3DPart(), game_x_2d::ScriptDebug2DPart()};
    CHECK(FindScriptApiPart(parts, RM_PART_DEBUG3D, RM_PART_DEBUG3D_VERSION) == &Debug3D());
    CHECK(FindScriptApiPart(parts, RM_PART_DEBUG2D, RM_PART_DEBUG2D_VERSION) == &Debug2D());
}

RM_TEST(DebugDraw_TooManyIsToldOnceOnTheConsole) {
    DebugDraw d;
    ScriptWorld w;
    w.debug = &d;
    std::vector<std::string> told;
    w.log = [&](int level, std::string_view t) {
        if (level == 1) told.emplace_back(t);
    };
    const float p[3] = {0, 0, 0}, s[3] = {1, 1, 1};
    // A script gone wrong: boxes every frame, for good.
    for (size_t i = 0; i < DebugDraw::kMaxLines / 12 + 100; ++i) Debug3D().box(&w, p, s, kRed, 1000);
    CHECK(d.Lines().size() == DebugDraw::kMaxLines && d.Dropped() > 0);
    CHECK(told.size() == 1);
}
