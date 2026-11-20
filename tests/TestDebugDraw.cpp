// スクリプトのデバッグ描画のテスト: ゲーム制作のバッファ (runtime/DebugDraw) と、rm::Debug の
// 図形を線にするコネクターの部分 (game_x_3d / game_x_2d ScriptDebug*)。

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
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {1, 0, 0, 1}, 0));   // このフレームだけ
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {0, 1, 0}, {0, 1, 0, 1}, 0.5));  // 0.5 秒
    CHECK(d.Add(DebugSpace::Space2D, {0, 0, 0}, {0, 0, 1}, {0, 0, 1, 1}, -3));  // 0 未満: このフレーム
    CHECK(d.Lines().size() == 3);
    d.BeginFrame(0.2);  // 次のフレーム: 1 フレームだけの線は消える
    CHECK(d.Lines().size() == 1 && d.Lines()[0].color.g == 1);
    d.BeginFrame(0.2);  // 0.4 秒: まだある
    CHECK(d.Lines().size() == 1);
    d.BeginFrame(0.0);  // 時間がかからなかったフレームでも、このフレームの線は消すが、時間つきの線は消さない
    CHECK(d.Lines().size() == 1);
    d.BeginFrame(0.2);  // 0.6 秒: 消えた
    CHECK(d.Lines().empty());
    // 1 フレームだけの線は、時間が進まなくても次のフレームで消える。
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 1, 1}, {}, 0));
    d.BeginFrame(0);
    CHECK(d.Lines().empty());
    // 秒が NaN: このフレームだけ (ずっと残ることは決してない)。
    CHECK(d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 1, 1}, {}, std::numeric_limits<double>::quiet_NaN()));
    d.BeginFrame(1);
    CHECK(d.Lines().empty());
}

RM_TEST(DebugDraw_FullIsDroppedAndToldOnce) {
    DebugDraw d;
    for (size_t i = 0; i < DebugDraw::kMaxLines; ++i) d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 10);
    CHECK(!d.TakeOverflowNotice());  // いっぱいだが、まだ何も捨てていない
    CHECK(!d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 0));
    CHECK(!d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 0));
    CHECK(d.Lines().size() == DebugDraw::kMaxLines && d.Dropped() == 2);
    CHECK(d.TakeOverflowNotice());   // 1 回だけ知らせる ...
    CHECK(!d.TakeOverflowNotice());  // ... 2 回目はない
    d.Add(DebugSpace::Space3D, {0, 0, 0}, {1, 0, 0}, {}, 0);
    CHECK(!d.TakeOverflowNotice());
    // Stop: すべて消え、次の Play ではまた知らせてよい。
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
    // 角は箱の角。
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
    Debug3D().ray(&w, x, dir, 5, kRed, 0);  // +Z に 5、向きの長さに関係なく
    CHECK(d.Lines().size() == 1 && d.Lines()[0].b.z == 5);
    Debug3D().ray(&w, x, x, 5, kRed, 0);  // 向きなし: 何もしない
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float bad[3] = {nan, 0, 0};
    Debug3D().line(&w, x, bad, kRed, 0);  // 場所ではない: 何もしない
    Debug3D().sphere(&w, o, -1, kRed, 0);  // 負の半径: 何もしない
    CHECK(d.Lines().size() == 1);
    // 2D: 平面の上 (z = 0)。
    d.Clear();
    const float c2[2] = {0, 0}, s2[2] = {2, 1}, b2[2] = {3, 4};
    Debug2D().rect(&w, c2, s2, kRed, 0);
    Debug2D().circle(&w, c2, 1, kRed, 0);
    Debug2D().line(&w, c2, b2, kRed, 0);
    CHECK(d.Lines().size() == 4 + game_x_2d::kDebugCircleSegments + 1);
    for (const DebugLine& l : d.Lines()) CHECK(l.space == DebugSpace::Space2D && l.a.z == 0 && l.b.z == 0);
}

RM_TEST(DebugDraw_ExportedGameDoesNothing) {
    // 書き出したゲームは、スクリプトにバッファのない世界を渡す: どの呼び出しもすぐ戻る。
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
    // 部品は名前とバージョンで提供される (RM3D.h / RM2D.h が求めるもの)。
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
    // おかしくなったスクリプト: 毎フレーム箱を出し続ける。
    for (size_t i = 0; i < DebugDraw::kMaxLines / 12 + 100; ++i) Debug3D().box(&w, p, s, kRed, 1000);
    CHECK(d.Lines().size() == DebugDraw::kMaxLines && d.Dropped() > 0);
    CHECK(told.size() == 1);
}
