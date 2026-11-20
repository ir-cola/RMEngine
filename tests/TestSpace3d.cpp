// 3D 空間プラグインの計算 (plugins/rm_engine/space3d/runtime/Transform, View)、モデル (Mesh: 形・
// マテリアル・パーツ。MeshFile: 関連ファイルのうち読んでよいもの)、光 (Lighting)、
// コネクターのカメラと光の部品 (game_x_3d/runtime) のテスト。

#include "Test.h"

#include <cstring>

#include "core/reflect/Data.h"
#include "rm_engine/game_x_3d/editor/SceneViewState.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/DrawOrder.h"
#include "rm_engine/space3d/runtime/Fbx.h"
#include "rm_engine/space3d/runtime/Inflate.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/MeshLoader.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/BlockCompress.h"
#include "rm_engine/space3d/runtime/TextureFile.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"
#include "rm_engine/space3d/runtime/TextureSampling.h"
#include "rm_engine/space3d/runtime/UploadSlice.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <mutex>
#include <thread>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::space3d;

namespace {

bool Near(Vec3 a, Vec3 b) { return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f && std::abs(a.z - b.z) < 1e-4f; }

}  // namespace

RM_TEST(Space3d_LocalMatrix_ScaleRotateMove) {
    CHECK(Near(TransformPoint(LocalMatrix({}), {1, 2, 3}), {1, 2, 3}));  // 初期値では何も変わらない
    Transform t;
    t.position = {1, 2, 3};
    t.rotation = {0, 90, 0};
    t.scale = {2, 2, 2};
    // (1,0,0) -> 2 倍 (2,0,0) -> Y まわりに 90 度 (0,0,-2) -> 移動 (1,2,1)
    CHECK(Near(TransformPoint(LocalMatrix(t), {1, 0, 0}), {1, 2, 1}));
    Transform rx;
    rx.rotation = {90, 0, 0};
    CHECK(Near(TransformPoint(LocalMatrix(rx), {0, 1, 0}), {0, 0, 1}));
    Transform rz;
    rz.rotation = {0, 0, 90};
    CHECK(Near(TransformPoint(LocalMatrix(rz), {1, 0, 0}), {0, 1, 0}));
    // 先に X、次に Y: (0,1,0) -X90-> (0,0,1) -Y90-> (1,0,0)
    Transform xy;
    xy.rotation = {90, 90, 0};
    CHECK(Near(TransformPoint(LocalMatrix(xy), {0, 1, 0}), {1, 0, 0}));
}

RM_TEST(Space3d_WorldMatrix_FollowsParent) {
    Transform parent;
    parent.position = {10, 0, 0};
    parent.rotation = {0, 0, 90};
    Transform child;
    child.position = {1, 0, 0};
    const Mat4 world = WorldMatrix(LocalMatrix(parent), child);
    CHECK(Near(TransformPoint(world, {0, 0, 0}), {10, 1, 0}));  // 子の x が親の y になる
    CHECK(Near(TransformPoint(Mat4{} * LocalMatrix(child), {0, 0, 0}), {1, 0, 0}));
}

namespace {
bool NearF(float a, float b) { return std::abs(a - b) < 1e-3f; }
}  // namespace

RM_TEST(Space3d_Inverse_UndoesTheMatrix) {
    Transform t;
    t.position = {3, -2, 5};
    t.rotation = {20, 40, -70};
    t.scale = {2, 0.5f, 3};
    const Mat4 m = LocalMatrix(t);
    const auto inv = Inverse(m);
    CHECK(inv.has_value());
    CHECK(Near(TransformPoint(*inv, TransformPoint(m, {1, 2, 3})), {1, 2, 3}));
    Transform flat;
    flat.scale = {1, 0, 1};
    CHECK(!Inverse(LocalMatrix(flat)).has_value());  // スケール 0 は元に戻せない
}

RM_TEST(Space3d_Camera_ProjectsThePivotToTheMiddle) {
    OrbitView v;
    v.pivot = {1, 2, 3};
    const Mat4 vp = v.ProjectionMatrix(16.f / 9.f) * v.ViewMatrix();
    float c[4];
    const Vec3 p = v.pivot;
    for (int row = 0; row < 4; ++row) c[row] = vp.m[0][row] * p.x + vp.m[1][row] * p.y + vp.m[2][row] * p.z + vp.m[3][row];
    CHECK(NearF(c[0] / c[3], 0) && NearF(c[1] / c[3], 0));
    CHECK(c[2] / c[3] > 0 && c[2] / c[3] < 1);  // 深さは 0 .. 1 の中
    // 画面の真ん中を通るレイは、目から支点へ向かう。
    const Ray r = ScreenRay(*Inverse(vp), 0, 0);
    CHECK(Near(r.dir, v.Forward()));
    // 初期のカメラ: 少し見下ろして、-Z と -X の方を向く。
    CHECK(v.Forward().y < 0 && v.Forward().z < 0);
    CHECK(NearF(Length(v.Forward()), 1) && NearF(Dot(v.Forward(), v.Right()), 0) && NearF(Dot(v.Up(), v.Forward()), 0));
}

RM_TEST(Space3d_Rays_HitBoxes) {
    const Ray r{{0, 0, 10}, {0, 0, -1}};
    auto t = IntersectBox(r, {-1, -1, -1}, {1, 1, 1});
    CHECK(t && NearF(*t, 9));
    CHECK(!IntersectBox({{5, 0, 10}, {0, 0, -1}}, {-1, -1, -1}, {1, 1, 1}));
    CHECK(!IntersectBox({{0, 0, 10}, {0, 0, 1}}, {-1, -1, -1}, {1, 1, 1}));  // 後ろ
    // x = 4 へ動かして幅 2 にした立方体: 面に当たる、距離 9。
    Transform box;
    box.position = {4, 0, 0};
    box.scale = {2, 2, 2};
    t = IntersectPlacedCube({{4, 0, 10}, {0, 0, -1}}, LocalMatrix(box));
    CHECK(t && NearF(*t, 9));
    CHECK(!IntersectPlacedCube(r, LocalMatrix(box)));
}

RM_TEST(Space3d_OrbitView_Moves) {
    OrbitView v;
    const Vec3 eye = v.Eye();
    Look(v, 30, -10);  // 首を振る: 目は動かない
    CHECK(Near(v.Eye(), eye));
    const Vec3 pivot = v.pivot;
    Orbit(v, 45, 20);  // 支点のまわり: 支点は動かない
    CHECK(Near(v.pivot, pivot) && NearF(Length(v.Eye() - v.pivot), v.distance));
    Orbit(v, 0, 500);
    CHECK(v.pitch == 89.f);  // 真上は越えない
    const float d = v.distance;
    Dolly(v, 1);
    CHECK(v.distance < d);
    const Vec3 before = v.pivot;
    Pan(v, 100, 0, 500);  // 右へドラッグするとカメラは左へ動く
    CHECK(Dot(v.pivot - before, v.Right()) < 0);
    const Vec3 at = v.pivot;
    Fly(v, {0, 0, 2});
    CHECK(Near(v.pivot - at, v.Forward() * 2));
    Frame(v, {7, 0, 0}, 1);
    CHECK(Near(v.pivot, {7, 0, 0}) && v.distance >= 2.5f);
}

// --- 回転の計算とギズモ (space3d/runtime/Transform, space3d/editor/Gizmo) --------------

namespace {

bool NearM(const Mat4& a, const Mat4& b) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a.m[c][r] - b.m[c][r]) > 1e-4f) return false;
        }
    }
    return true;
}

// 原点の手前から -Z 方向を見るカメラ: X は画面の右、Y は上。
OrbitView FrontView() {
    OrbitView v;
    v.yaw = 0;
    v.pitch = 0;
    v.distance = 10;
    return v;
}

gizmo::Screen ScreenOf(const OrbitView& v) {
    return *gizmo::MakeScreen(v.ProjectionMatrix(800.f / 600.f) * v.ViewMatrix(), 0, 0, 800, 600);
}

}  // namespace

RM_TEST(Space3d_AxisAngleAndRotationMatrix) {
    CHECK(NearM(AxisAngle({0, 1, 0}, 90), RotationMatrix({0, 90, 0})));
    CHECK(NearM(AxisAngle({0, 0, 5}, 30), RotationMatrix({0, 0, 30})));  // 軸の長さは何でもよい
    CHECK(NearM(AxisAngle({0, 0, 0}, 30), Mat4{}));                    // 軸なし: 回転なし
    // LocalMatrix も同じ回転から作る。
    Transform t;
    t.rotation = {10, 20, 30};
    CHECK(NearM(LocalMatrix(t), RotationMatrix({10, 20, 30})));
}

RM_TEST(Space3d_EulerFromRotation) {
    for (Vec3 a : {Vec3{0, 0, 0}, Vec3{30, -50, 120}, Vec3{-170, 80, 5}, Vec3{45, 0, -90}}) {
        CHECK(Near(EulerFromRotation(RotationMatrix(a), a), a));
        CHECK(NearM(RotationMatrix(EulerFromRotation(RotationMatrix(a))), RotationMatrix(a)));  // 0 から: 別の組でも同じ回転
    }
    // ドラッグ中は途切れない: 表示中の角度が 180 の近くなら、180 を越えた分はそのまま越えたまま。
    CHECK(Near(EulerFromRotation(RotationMatrix({0, 181, 0}), {0, 179, 0}), {0, 181, 0}));
    CHECK(Near(EulerFromRotation(RotationMatrix({0, 120, 0}), {0, 119, 0}), {0, 120, 0}));
    CHECK(Near(EulerFromRotation(RotationMatrix({0, 0, 400}), {0, 0, 399}), {0, 0, 400}));
    // 真上 (ジンバルロック): 同じ回転、z は保つ。
    const Vec3 lock = EulerFromRotation(RotationMatrix({20, 90, 0}), {0, 85, 0});
    CHECK(NearM(RotationMatrix(lock), RotationMatrix({20, 90, 0})));
    CHECK(std::abs(lock.y - 90) < 1e-3f && std::abs(lock.z) < 1e-3f);
    const Vec3 down = EulerFromRotation(RotationMatrix({-30, -90, 10}), {0, -90, 10});
    CHECK(NearM(RotationMatrix(down), RotationMatrix({-30, -90, 10})));
}

RM_TEST(Space3d_FromWorld_ChangesOnlyWhatWasTyped) {
    Transform parent;
    parent.position = {5, 0, 0};
    parent.rotation = {0, 90, 0};
    parent.scale = {2, 2, 2};
    const Mat4 pw = LocalMatrix(parent);
    Transform child;
    child.position = {1, 0, 0};
    child.rotation = {0, 0, 30};
    const WorldPlacement now = Decompose(WorldMatrix(pw, child), child.rotation);
    // 何も入力しない: 何も変わらない (最後のビットまで)。
    const Transform same = FromWorld(child, pw, now);
    CHECK(same.position == child.position && same.rotation == child.rotation && same.scale == child.scale);
    // ワールド位置: 位置だけが動く。
    WorldPlacement want = now;
    want.position = {5, 0, -4};  // 親の X に沿って 2 (-Z を向く)、しかも 2 倍の大きさ
    const Transform moved = FromWorld(child, pw, want);
    CHECK(Near(moved.position, {2, 0, 0}) && moved.rotation == child.rotation && moved.scale == child.scale);
    // ワールドの回転とスケールは入力どおりに出る。
    want = now;
    want.rotation = {10, 20, 30};
    const Transform turned = FromWorld(child, pw, want);
    CHECK(NearM(RotationMatrix(Decompose(WorldMatrix(pw, turned)).rotation), RotationMatrix({10, 20, 30})));
    CHECK(turned.position == child.position);
    want = now;
    want.scale = {6, 2, 2};
    const Transform scaled = FromWorld(child, pw, want);
    CHECK(Near(scaled.scale, {3, 1, 1}));
    CHECK(Near(Decompose(WorldMatrix(pw, scaled)).scale, {6, 2, 2}));
}

RM_TEST(Space3d_Decompose_WorldPlacement) {
    Transform parent;
    parent.rotation = {0, 90, 0};
    parent.scale = {2, 2, 2};
    Transform child;
    child.position = {1, 0, 0};
    child.rotation = {0, 0, 30};
    const Mat4 world = WorldMatrix(LocalMatrix(parent), child);
    const WorldPlacement p = Decompose(world);
    CHECK(Near(p.position, {0, 0, -2}));  // (1,0,0) を 2 倍して Y まわりに 90 度
    CHECK(Near(p.scale, {2, 2, 2}));
    CHECK(NearM(RotationMatrix(p.rotation), RotationMatrix({0, 90, 0}) * RotationMatrix({0, 0, 30})));
    // 鏡映は X スケールが負の値。
    Transform m;
    m.scale = {-1, 1, 1};
    CHECK(Near(Decompose(LocalMatrix(m)).scale, {-1, 1, 1}));
    CHECK(Near(Decompose(LocalMatrix(m)).rotation, {0, 0, 0}));
}

RM_TEST(Gizmo_LinesPlanesSegments) {
    const Ray down{{0, 5, 0}, {0, -1, 0}};
    CHECK(!gizmo::ClosestOnLine(down, {0, 0, 0}, {0, 1, 0}));  // 平行
    const Ray across{{3, 1, 5}, {0, 0, -1}};
    CHECK(std::abs(*gizmo::ClosestOnLine(across, {0, 0, 0}, {1, 0, 0}) - 3) < 1e-4f);
    CHECK(Near(*gizmo::IntersectPlane(down, {0, 0, 0}, {0, 1, 0}), {0, 0, 0}));
    CHECK(!gizmo::IntersectPlane(down, {0, 10, 0}, {0, 1, 0}));  // レイの後ろ
    CHECK(!gizmo::IntersectPlane(across, {0, 0, 0}, {1, 0, 0}));  // 面に沿う
    CHECK(std::abs(gizmo::DistanceToSegment({5, 3}, {0, 0}, {10, 0}) - 3) < 1e-4f);
    CHECK(std::abs(gizmo::DistanceToSegment({-4, 3}, {0, 0}, {10, 0}) - 5) < 1e-4f);
}

RM_TEST(Gizmo_FrameAndScreen) {
    const OrbitView v = FrontView();
    const gizmo::Screen s = ScreenOf(v);
    const auto mid = s.ToScreen({0, 0, 0});
    CHECK(mid && std::abs(mid->x - 400) < 1e-2f && std::abs(mid->y - 300) < 1e-2f);
    CHECK(!s.ToScreen({0, 0, 20}));  // 目の後ろ
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, gizmo::Tool::Move, gizmo::Space::World, v, 600);
    const auto end = s.ToScreen(gizmo::AxisEnd(f, 0));
    CHECK(end && std::abs(end->x - 490) < 0.5f && std::abs(end->y - 300) < 0.5f);  // 右へ 90 ピクセル
    CHECK(!gizmo::AxisShown(f, s, 2));  // Z は目を向く
    CHECK(gizmo::AxisShown(f, s, 0) && gizmo::AxisShown(f, s, 1));
    CHECK(gizmo::PlaneShown(f, s, gizmo::Handle::XY));  // 目に向く
    CHECK(!gizmo::PlaneShown(f, s, gizmo::Handle::YZ) && !gizmo::PlaneShown(f, s, gizmo::Handle::ZX));  // 真横
    // ローカル: 回した物自身の軸 (スケールはいつもローカル)。
    Transform t;
    t.rotation = {0, 0, 90};
    const auto local = gizmo::AxesOf(LocalMatrix(t), gizmo::Tool::Rotate, gizmo::Space::Local);
    CHECK(Near(local[0], {0, 1, 0}));
    CHECK(Near(gizmo::AxesOf(LocalMatrix(t), gizmo::Tool::Move, gizmo::Space::World)[0], {1, 0, 0}));
    CHECK(Near(gizmo::AxesOf(LocalMatrix(t), gizmo::Tool::Scale, gizmo::Space::World)[0], {0, 1, 0}));
}

RM_TEST(Gizmo_RingShowsTheHalfFacingTheEye) {
    const OrbitView v = FrontView();
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, gizmo::Tool::Rotate, gizmo::Space::World, v, 600);
    const auto facing = gizmo::RingArcs(f, 2, v.Eye(), true, 64);  // Z まわり: 目に向く
    CHECK(facing.size() == 1 && facing[0].size() == 65);
    CHECK(gizmo::RingArcs(f, 2, v.Eye(), false, 64).empty());  // ... だから裏はない
    const auto back = gizmo::RingArcs(f, 1, v.Eye(), false, 64);  // Y まわり: もう半分は裏側
    size_t backPoints = 0;
    for (const auto& arc : back) {
        backPoints += arc.size();
        for (Vec3 p : arc) CHECK(p.z <= 1e-3f);
    }
    CHECK(back.size() == 1 && backPoints >= 32 && backPoints <= 34);
    const auto edge = gizmo::RingArcs(f, 1, v.Eye(), true, 64);  // Y まわり: 半分が手前
    size_t points = 0;
    for (const auto& arc : edge) {
        points += arc.size();
        for (Vec3 p : arc) CHECK(p.z >= -1e-3f);  // 真ん中の裏には何もない
    }
    CHECK(edge.size() == 1 && points >= 32 && points <= 34);
}

RM_TEST(Gizmo_HitTestFindsTheHandle) {
    const OrbitView v = FrontView();
    const gizmo::Screen s = ScreenOf(v);
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, gizmo::Tool::Move, gizmo::Space::World, v, 600);
    using gizmo::Handle;
    using gizmo::Tool;
    CHECK(gizmo::HitTest(Tool::Move, f, s, v.Eye(), {488, 301}) == Handle::X);
    CHECK(gizmo::HitTest(Tool::Move, f, s, v.Eye(), {401, 240}) == Handle::Y);  // y 上 = 画面の上
    CHECK(gizmo::HitTest(Tool::Move, f, s, v.Eye(), {400 + 28, 300 - 28}) == Handle::XY);
    CHECK(gizmo::HitTest(Tool::Move, f, s, v.Eye(), {600, 500}) == Handle::None);
    CHECK(gizmo::HitTest(Tool::Scale, f, s, v.Eye(), {402, 299}) == Handle::Uniform);
    const float r = 90.f * 0.70710678f;
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {400 + r, 300 - r}) == Handle::Z);
}

RM_TEST(Gizmo_DragMovesRotatesScales) {
    const OrbitView v = FrontView();
    const gizmo::Screen s = ScreenOf(v);
    using gizmo::Handle;
    using gizmo::Tool;
    gizmo::Snap smooth, steps;
    steps.on = true;
    {  // X に沿って、ワールドの 1 単位だけ動かす
        const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, Tool::Move, gizmo::Space::World, v, 600);
        const auto drag = gizmo::BeginDrag(Tool::Move, Handle::X, f, s, v.Eye(), {490, 300});
        CHECK(drag.has_value());
        const float px = 90.f / f.size;  // 原点での、ワールドの 1 単位あたりのピクセル数
        const auto c = gizmo::Update(*drag, s, {490 + px, 300}, smooth);
        CHECK(c && Near(c->move, {1, 0, 0}));
        const auto snapped = gizmo::Update(*drag, s, {490 + px * 0.9f, 300}, steps);
        CHECK(snapped && Near(snapped->move, {1, 0, 0}));  // 0.9 -> 1.0 (0.25 刻み)
        // 面の上: どちらの向きにも動く。
        const auto plane = gizmo::BeginDrag(Tool::Move, Handle::XY, f, s, v.Eye(), {428, 272});
        CHECK(plane.has_value());
        const auto pc = gizmo::Update(*plane, s, {428 + px, 272 - px}, smooth);
        CHECK(pc && Near(pc->move, {1, 1, 0}));
    }
    {  // Z まわりに、マウスの下のリングの長さの 4 分の 1 だけ回す
        const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, Tool::Rotate, gizmo::Space::World, v, 600);
        const float r = 90.f * 0.70710678f;
        const auto drag = gizmo::BeginDrag(Tool::Rotate, Handle::Z, f, s, v.Eye(), {400 + r, 300 - r});
        CHECK(drag.has_value());
        // Z まわりの順方向 (画面で反時計回り) で右上にいる = 上と左へ。
        CHECK(drag->tangent.x < 0 && drag->tangent.y < 0);
        const float quarter = 90.f * 3.14159265f / 2.f;
        const Vec2 to{400 + r + drag->tangent.x * quarter, 300 - r + drag->tangent.y * quarter};
        const auto c = gizmo::Update(*drag, s, to, smooth);
        CHECK(c && std::abs(c->degrees - 90) < 0.5f && c->axis == 2);
        const Transform t = gizmo::Apply({}, Mat4{}, Mat4{}, *c, gizmo::Space::World, {});
        CHECK(NearM(RotationMatrix(t.rotation), AxisAngle({0, 0, 1}, c->degrees)));
        // 鏡映の親 (x のスケールが -1) の下: ワールドでは、やはりリングの向きに回る。
        Transform mirror;
        mirror.scale = {-1, 1, 1};
        const Mat4 p = LocalMatrix(mirror);
        const Transform m = gizmo::Apply({}, p, p, *c, gizmo::Space::World, {});
        CHECK(NearM(p * RotationMatrix(m.rotation), AxisAngle({0, 0, 1}, c->degrees) * p));
        const auto s15 = gizmo::Update(*drag, s, {400 + r + drag->tangent.x * 20, 300 - r + drag->tangent.y * 20}, steps);
        CHECK(s15 && std::abs(s15->degrees - 15) < 1e-3f);  // 12.7 度 -> 15
    }
    {  // スケール: 真ん中の箱は全体を均等に、軸は 1 つだけ
        const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, Tool::Scale, gizmo::Space::World, v, 600);
        const auto all = gizmo::BeginDrag(Tool::Scale, Handle::Uniform, f, s, v.Eye(), {400, 300});
        const auto c = gizmo::Update(*all, s, {450, 300}, smooth);
        CHECK(c && std::abs(c->factor - 1.5f) < 1e-4f && c->axis == -1);
        Transform start;
        start.scale = {1, 2, 3};
        CHECK(Near(gizmo::Apply(start, Mat4{}, Mat4{}, *c, gizmo::Space::World, {}).scale, {1.5f, 3, 4.5f}));
        const auto x = gizmo::BeginDrag(Tool::Scale, Handle::X, f, s, v.Eye(), {490, 300});
        const auto cx = gizmo::Update(*x, s, {580, 300}, smooth);
        CHECK(cx && std::abs(cx->factor - 2) < 1e-3f && cx->axis == 0);
        CHECK(Near(gizmo::Apply(start, Mat4{}, Mat4{}, *cx, gizmo::Space::World, {}).scale, {2, 2, 3}));
    }
}

RM_TEST(Gizmo_ApplyThroughTheParent) {
    Transform parent;
    parent.rotation = {0, 90, 0};
    parent.scale = {2, 2, 2};
    const Mat4 pw = LocalMatrix(parent);
    Transform child;
    child.position = {1, 0, 0};
    const Mat4 cw = WorldMatrix(pw, child);
    // ワールドの X に沿って 2 動かすのは、親の +Z に沿って 1 (Y まわりに 90 度回り、2 倍の大きさ)。
    gizmo::Change move;
    move.tool = gizmo::Tool::Move;
    move.move = {2, 0, 0};
    CHECK(Near(gizmo::Apply(child, pw, cw, move, gizmo::Space::World, {}).position, {1, 0, 1}));
    // ワールドの X まわりと、その物自身の X まわり。
    Transform turned;
    turned.rotation = {0, 90, 0};
    const Mat4 tw = LocalMatrix(turned);
    gizmo::Change rot;
    rot.tool = gizmo::Tool::Rotate;
    rot.axis = 0;
    rot.degrees = 90;
    const Transform world = gizmo::Apply(turned, Mat4{}, tw, rot, gizmo::Space::World, turned.rotation);
    CHECK(NearM(RotationMatrix(world.rotation), AxisAngle({1, 0, 0}, 90) * RotationMatrix({0, 90, 0})));
    const Transform local = gizmo::Apply(turned, Mat4{}, tw, rot, gizmo::Space::Local, turned.rotation);
    CHECK(NearM(RotationMatrix(local.rotation), RotationMatrix({0, 90, 0}) * AxisAngle({1, 0, 0}, 90)));
    CHECK(Near(local.rotation, {90, 90, 0}));  // 先に X、次に Y: インスペクターが書くとおり
}

RM_TEST(Gizmo_ViewRingAndFreeRotation) {
    const OrbitView v = FrontView();  // 目は +Z にあって原点を見る
    const gizmo::Screen s = ScreenOf(v);
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, gizmo::Tool::Rotate, gizmo::Space::World, v, 600);
    using gizmo::Handle;
    using gizmo::Tool;
    const float outer = 90.f * gizmo::kViewRing;
    // 目に向く外側のリングと、球の内側 (リングから離れた所): 自由に回す。
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {400 + outer, 300}) == Handle::View);
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {430, 320}) == Handle::Free);
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {400 + outer + 30, 300}) == Handle::None);
    const float quarter = 3.14159265f / 2.f;
    gizmo::Snap smooth;
    {  // 外側のリング: 視線 (ここでは +Z) まわり、長さの 4 分の 1 = 90 度
        const auto drag = gizmo::BeginDrag(Tool::Rotate, Handle::View, f, s, v.Eye(), {400 + outer, 300});
        CHECK(drag.has_value());
        CHECK(std::abs(drag->tangent.x) < 1e-3f && drag->tangent.y < -0.99f);  // 順方向 = 画面の上へ
        const auto c = gizmo::Update(*drag, s, {400 + outer, 300 - outer * quarter}, smooth);
        CHECK(c && std::abs(c->degrees - 90) < 0.5f && c->axis == -1 && Near(c->worldAxis, {0, 0, 1}));
        const Transform t = gizmo::Apply({}, Mat4{}, Mat4{}, *c, gizmo::Space::Local, {});
        CHECK(NearM(RotationMatrix(t.rotation), AxisAngle({0, 0, 1}, c->degrees)));
    }
    {  // 内側: 右へドラッグすると手前が右へ回る = +Y まわり。長さの 4 分の 1 = 90 度
        const auto drag = gizmo::BeginDrag(Tool::Rotate, Handle::Free, f, s, v.Eye(), {430, 320});
        CHECK(drag.has_value());
        const auto c = gizmo::Update(*drag, s, {430 + 90.f * quarter, 320}, smooth);
        CHECK(c && std::abs(c->degrees - 90) < 0.5f && Near(c->worldAxis, {0, 1, 0}));
        const auto upward = gizmo::Update(*drag, s, {430, 320 - 90.f * quarter}, smooth);  // 上: 手前が上がる = -X まわり
        CHECK(upward && std::abs(upward->degrees - 90) < 0.5f && Near(upward->worldAxis, {-1, 0, 0}));
        const auto still = gizmo::Update(*drag, s, {430, 320}, smooth);
        CHECK(still && still->degrees == 0);
    }
}

RM_TEST(Gizmo_PivotCenterMovesThePlaces) {
    // 複数の物を 1 つの支点 (それらの真ん中) のまわりで: 位置も一緒に回る / スケールされる。
    Transform start;
    start.position = {1, 0, 0};
    const Mat4 sw = LocalMatrix(start);
    const std::optional<Vec3> pivot = Vec3{0, 0, 0};
    gizmo::Change rot;
    rot.tool = gizmo::Tool::Rotate;
    rot.axis = 1;
    rot.worldAxis = {0, 1, 0};
    rot.degrees = 90;
    const Transform turned = gizmo::Apply(start, Mat4{}, sw, rot, gizmo::Space::World, {}, pivot);
    CHECK(Near(turned.position, {0, 0, -1}));
    CHECK(NearM(RotationMatrix(turned.rotation), AxisAngle({0, 1, 0}, 90)));
    CHECK(Near(gizmo::Apply(start, Mat4{}, sw, rot, gizmo::Space::World, {}).position, {1, 0, 0}));  // 支点なし: そのまま
    gizmo::Change all;
    all.tool = gizmo::Tool::Scale;
    all.factor = 2;
    const Transform bigger = gizmo::Apply(start, Mat4{}, sw, all, gizmo::Space::Local, {}, pivot);
    CHECK(Near(bigger.position, {2, 0, 0}) && Near(bigger.scale, {2, 2, 2}));
    Transform corner;
    corner.position = {1, 0, 1};
    gizmo::Change wide;
    wide.tool = gizmo::Tool::Scale;
    wide.axis = 0;
    wide.worldAxis = {1, 0, 0};
    wide.factor = 2;
    const Transform wider = gizmo::Apply(corner, Mat4{}, LocalMatrix(corner), wide, gizmo::Space::Local, {}, pivot);
    CHECK(Near(wider.position, {2, 0, 1}) && Near(wider.scale, {2, 1, 1}));
    // ギズモはそれらの真ん中に置き、与えられた軸を使う。
    const gizmo::Frame f = *gizmo::MakeFrame(Vec3{1, 2, 3}, gizmo::AxesOf(Mat4{}, gizmo::Tool::Move, gizmo::Space::World), FrontView(), 600);
    CHECK(Near(f.origin, {1, 2, 3}) && f.size > 0);
}

RM_TEST(SceneViewState_KeepsCameraAndTool) {
    game_x_3d::SceneViewState st;
    st.camera.pivot = {1, 2, 3};
    st.camera.pitch = 40;
    st.tool = gizmo::Tool::Rotate;
    st.space = gizmo::Space::World;
    st.pivot = gizmo::Pivot::Center;
    st.snapMove = 0.5f;
    st.snapDegrees = 45;
    st.snapAlways = true;
    json::Value j = reflect::Save(st);
    game_x_3d::SceneViewState back;
    reflect::Load(back, j);
    CHECK(Near(back.camera.pivot, {1, 2, 3}) && back.camera.pitch == 40);
    CHECK(back.tool == gizmo::Tool::Rotate && back.space == gizmo::Space::World);
    CHECK(back.pivot == gizmo::Pivot::Center && back.snapMove == 0.5f && back.snapDegrees == 45 && back.snapAlways);
    CHECK(back.snapScale == 0.1f);  // 変えない: 初期値
    // 範囲の外の値は範囲に戻す。新しいバージョンのキーは書き戻す。
    j.Find("camera")->Set("pitch", 500.0);
    j.Set("future", 1);
    reflect::Unknowns u;
    game_x_3d::SceneViewState clamped;
    reflect::Load(clamped, j, &u);
    CHECK(clamped.camera.pitch == 89);
    j.Set("snapDegrees", 0.0);  // 刻みが 0 だと動かない: 最小値まで引き上げる
    game_x_3d::SceneViewState least;
    reflect::Load(least, j);
    CHECK(std::abs(least.snapDegrees - 0.1f) < 1e-6f);
    CHECK(reflect::Save(clamped, &u).Find("future") != nullptr);
}

// --- モデル (space3d/runtime/Mesh) と、シーンビューがその境界で行うこと -------------

namespace {

std::string Base64(const std::vector<uint8_t>& b) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < b.size(); i += 3) {
        const uint32_t n = (uint32_t{b[i]} << 16) | (i + 1 < b.size() ? uint32_t{b[i + 1]} << 8 : 0u) | (i + 2 < b.size() ? uint32_t{b[i + 2]} : 0u);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < b.size() ? k[(n >> 6) & 63] : '=';
        out += i + 2 < b.size() ? k[n & 63] : '=';
    }
    return out;
}

template <class T>
void PutBytes(std::vector<uint8_t>& out, std::initializer_list<T> values) {
    for (const T v : values) {
        uint8_t p[sizeof(T)];
        std::memcpy(p, &v, sizeof(T));
        out.insert(out.end(), p, p + sizeof(T));
    }
}

// 三角形 1 つ (0,0,0) (1,0,0) (0,1,0)。+Z から見て反時計回り、ushort のインデックス: 44 バイト。
std::vector<uint8_t> TriangleBin(uint16_t lastIndex = 2) {
    std::vector<uint8_t> b;
    PutBytes<float>(b, {0, 0, 0, 1, 0, 0, 0, 1, 0});
    PutBytes<uint16_t>(b, {0, 1, lastIndex, 0});
    return b;
}

// その三角形の glTF。`buffer` = "buffers":[{...}] のキーの追加、`node` = ノードのキー。
std::string TriangleGltf(const std::string& buffer, const std::string& node = "\"translation\":[0,0,5]") {
    return std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)") + R"("nodes":[{"mesh":0,)" + node + "}]," +
           R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" + R"("buffers":[{"byteLength":44)" + buffer + "}]," +
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)" +
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)" +
           R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

std::string Embedded(const std::vector<uint8_t>& bin) { return ",\"uri\":\"data:application/octet-stream;base64," + Base64(bin) + "\""; }

std::span<const uint8_t> Bytes(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

}  // namespace

RM_TEST(Mesh_Base64AndPercent) {
    const std::vector<uint8_t> data{0, 1, 2, 250, 251, 252, 'a'};
    const auto back = DecodeBase64(Base64(data));
    CHECK(back && *back == data);
    CHECK(DecodeBase64("TWFu") == std::vector<uint8_t>({'M', 'a', 'n'}));
    CHECK(!DecodeBase64("TW*u"));   // base64 ではない
    CHECK(!DecodeBase64("TWFuT"));  // 1 文字だけ
    CHECK(DecodePercent("my%20model.bin") == "my model.bin");
    CHECK(DecodePercent("100%") == "100%");  // コードではない: そのまま残す
}

RM_TEST(Mesh_Gltf_EmbeddedTriangleIsPlacedByItsNode) {
    std::string error;
    const std::string doc = TriangleGltf(Embedded(TriangleBin()));
    const auto m = ParseMesh(".gltf", Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->vertices.size() == 3 && m->indices.size() == 3);
    CHECK(Near(m->vertices[1].position, {1, 0, 5}));  // ノードで動かした
    CHECK(Near(m->vertices[0].normal, {0, 0, 1}));    // ファイルに法線なし: 作って、手前を向ける
    CHECK(Near(m->min, {0, 0, 5}) && Near(m->max, {1, 1, 5}));
}

RM_TEST(Mesh_Gltf_MirroredNodeKeepsTheFront) {
    std::string error;
    const auto m = ParseGltf(Bytes(TriangleGltf(Embedded(TriangleBin()), "\"scale\":[-1,1,1]")), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    const Vec3 a = m->vertices[m->indices[0]].position, b = m->vertices[m->indices[1]].position, c = m->vertices[m->indices[2]].position;
    CHECK(Near(m->vertices[1].position, {-1, 0, 0}));
    // 反時計回りは、やはり法線の側を向く。
    CHECK(Dot(Cross(b - a, c - a), m->vertices[0].normal) > 0);
}

RM_TEST(Mesh_Gltf_BufferInAnotherFile) {
    std::string error;
    std::string asked;
    const ReadRelated read = [&](std::string_view uri) -> std::optional<std::vector<uint8_t>> {
        asked = uri;
        return TriangleBin();
    };
    const auto m = ParseGltf(Bytes(TriangleGltf(",\"uri\":\"tri%20data.bin\"")), read, &error);
    CHECK(m.has_value() && asked == "tri data.bin");
    const ReadRelated missing = [](std::string_view) -> std::optional<std::vector<uint8_t>> { return std::nullopt; };
    CHECK(!ParseGltf(Bytes(TriangleGltf(",\"uri\":\"gone.bin\"")), missing, &error));
    CHECK(error.find("gone.bin") != std::string::npos);
}

RM_TEST(Mesh_Glb_OneFile) {
    std::string json = TriangleGltf("");  // buffer 0 に uri なし: BIN チャンク
    while (json.size() % 4 != 0) json += ' ';
    const std::vector<uint8_t> bin = TriangleBin();
    std::vector<uint8_t> glb;
    PutBytes<uint32_t>(glb, {0x46546C67u, 2u, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size())});
    PutBytes<uint32_t>(glb, {static_cast<uint32_t>(json.size()), 0x4E4F534Au});
    glb.insert(glb.end(), json.begin(), json.end());
    PutBytes<uint32_t>(glb, {static_cast<uint32_t>(bin.size()), 0x004E4942u});
    glb.insert(glb.end(), bin.begin(), bin.end());
    std::string error;
    const auto m = ParseMesh(".GLB", glb, nullptr, &error);
    CHECK(m.has_value() && m->indices.size() == 3);
    // 途中で切れた: 知らせる。終わりより先は読まない。
    glb.resize(glb.size() - 20);
    CHECK(!ParseMesh(".glb", glb, nullptr, &error));
}

RM_TEST(Mesh_Gltf_BrokenFilesAreToldWhy) {
    std::string error;
    // 最後の頂点より先を指すインデックス。
    CHECK(!ParseGltf(Bytes(TriangleGltf(Embedded(TriangleBin(7)))), nullptr, &error));
    CHECK(error.find("インデックス") != std::string::npos);
    // 書いてあるより短いバッファ。
    std::vector<uint8_t> shortBin = TriangleBin();
    shortBin.resize(20);
    CHECK(!ParseGltf(Bytes(TriangleGltf(Embedded(shortBin))), nullptr, &error));
    // JSON ですらない。glTF 1.0。読まない形式。
    CHECK(!ParseGltf(Bytes(std::string("not json")), nullptr, &error));
    CHECK(!ParseGltf(Bytes(std::string(R"({"asset":{"version":"1.0"}})")), nullptr, &error));
    CHECK(!ParseMesh(".fbx", Bytes(std::string("x")), nullptr, &error));
}

RM_TEST(Mesh_Gltf_StripBecomesTriangles) {
    std::vector<uint8_t> bin;
    PutBytes<float>(bin, {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0});
    const std::string doc = std::string(R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[{"attributes":{"POSITION":0},"mode":5}]}],)") +
                            R"("buffers":[{"byteLength":48)" + Embedded(bin) + "}]," +
                            R"("bufferViews":[{"buffer":0,"byteLength":48}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"}]})";
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);  // シーンもノードもなし: メッシュをそのまま使う
    CHECK(m.has_value() && m->indices.size() == 6);
    if (!m) return;
    for (size_t t = 0; t < 6; t += 3) {
        const Vec3 a = m->vertices[m->indices[t]].position, b = m->vertices[m->indices[t + 1]].position, c = m->vertices[m->indices[t + 2]].position;
        CHECK(Cross(b - a, c - a).z > 0);  // どちらも手前を向く
    }
}

RM_TEST(Mesh_Obj) {
    std::string error;
    // UV と法線のある四角形 (4 つの角で 1 つのポリゴン)。2 つ目の面は終わりから数える。
    const char* square = "# a square\nv 0 0 0\nv 1 0 0\nv 1 1 0\r\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                         "o Square\nusemtl none\nf 1/1/1 2/2/1 3/3/1 4/4/1\nf -4/-4/-1 -2/-2/-1 -1/-1/-1\n";
    const auto m = ParseObj(square, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->indices.size() == 9);
    CHECK(m->vertices.size() == 4);  // 同じ角 (v/vt/vn) は 1 つの頂点
    CHECK(NearF(m->vertices[0].uv.y, 1.f) && NearF(m->vertices[2].uv.y, 0.f));  // OBJ の V は上下が逆
    CHECK(Near(m->min, {0, 0, 0}) && Near(m->max, {1, 1, 0}));
    // 法線なし: 面から作る。
    const auto flat = ParseObj("v 0 0 0\nv 0 0 1\nv 1 0 0\nf 1 2 3\n", &error);
    CHECK(flat && Near(flat->vertices[0].normal, {0, 1, 0}));
    // 間違った数は、その行を名指しする。
    CHECK(!ParseObj("v 0 0 0\nv 1 0 0\nf 1 2 9\n", &error));
    CHECK(error.find("3 行目") != std::string::npos);
    CHECK(!ParseObj("v 0 0 0\n", &error));  // 面なし
}

// --- マテリアル (色と絵) と、それがモデルを切り分けるパーツ --------------------------

namespace {

// 三角形を 2 つ、それぞれの primitive に別のマテリアル。`more` = 文書の残り
// ("materials", "textures", "images" ...)。`extraViews` = 2 つの後ろに足すバッファビュー。
std::string TwoMaterialGltf(const std::string& more, const std::string& extraViews = "", const std::string& firstMaterial = "0",
                            const std::string& secondMaterial = "1") {
    return std::string(R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[)") +
           R"({"attributes":{"POSITION":0},"indices":1,"material":)" + firstMaterial + "}," +
           R"({"attributes":{"POSITION":0},"indices":1,"material":)" + secondMaterial + "}]}]," + R"("buffers":[{"byteLength":44)" +
           Embedded(TriangleBin()) + "}]," +
           R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6})" + extraViews + "]," +
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)" +
           R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)" + more + "}";
}

std::optional<std::vector<uint8_t>> TextFile(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

}  // namespace

RM_TEST(Mesh_LinearToScreen) {
    CHECK(NearF(LinearToScreen(0), 0) && NearF(LinearToScreen(1), 1));
    CHECK(std::abs(LinearToScreen(0.5f) - 0.7354f) < 1e-3f);  // 光が半分でも、半分よりずっと明るく見える
    CHECK(NearF(LinearToScreen(-1), 0) && NearF(LinearToScreen(3), 1));
    CHECK(NearF(ScreenToLinear(0), 0) && NearF(ScreenToLinear(1), 1) && std::abs(ScreenToLinear(0.7354f) - 0.5f) < 1e-3f);
    for (const float v : {0.001f, 0.02f, 0.3f, 0.9f}) CHECK(std::abs(ScreenToLinear(LinearToScreen(v)) - v) < 1e-4f);  // 行って戻る
}

RM_TEST(Mesh_Gltf_MaterialsMakeParts) {
    const std::string doc = TwoMaterialGltf(
        R"("materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"baseColorTexture":{"index":0}}},)"
        R"({"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.5,0.5,0.5]},"alphaMode":"MASK","alphaCutoff":0.3}],)"
        R"("textures":[{"source":0}],"images":[{"uri":"tex%20a.png"}])");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->indices.size() == 6);
    CHECK(m->parts.size() == 2);
    if (m->parts.size() != 2) return;
    CHECK((m->parts[0] == MeshPart{0, 3, 0}) && (m->parts[1] == MeshPart{3, 3, 1}));
    CHECK(m->materials.size() == 2 && m->materials[0].name == "Red");
    CHECK(m->materials[0].baseColor == (Color{1, 0, 0, 1}) && m->materials[0].image == 0);
    CHECK(m->images.size() == 1 && m->images[0].file == "tex a.png" && m->images[0].bytes.empty());  // モデルの隣のファイル
    // 係数はリニアな光で、そのまま残す (レンダラーはリニアな光で動く)。
    CHECK(NearF(m->materials[1].baseColor.r, 0.5f) && NearF(m->materials[1].baseColor.a, 0.5f));
    CHECK(m->materials[1].alpha == AlphaMode::Mask && NearF(m->materials[1].alphaCutoff, 0.3f) && m->materials[1].image == -1);
    CHECK(m->materials[0].alpha == AlphaMode::Opaque);
    // 金属とラフネスの指定なし: glTF の初期値 (ざらざらした金属)。法線マップなし、自分で光らない。
    CHECK(NearF(m->materials[0].metallic, 1) && NearF(m->materials[0].roughness, 1));
    CHECK(m->materials[0].normalImage == -1 && m->materials[0].emissive == (Color{0, 0, 0, 1}));
}

RM_TEST(Mesh_Gltf_MetalRoughnessNormalEmission) {
    const std::string doc = TwoMaterialGltf(
        R"("materials":[{"pbrMetallicRoughness":{"metallicFactor":0.25,"roughnessFactor":0.5,"metallicRoughnessTexture":{"index":1}},)"
        R"("normalTexture":{"index":2,"scale":0.7},"emissiveFactor":[1,0.5,0],"emissiveTexture":{"index":0},)"
        R"("extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":4}}},)"
        R"({"pbrMetallicRoughness":{"metallicFactor":3,"roughnessFactor":-1}}],)"
        R"("textures":[{"source":0},{"source":1},{"source":2}],"images":[{"uri":"glow.png"},{"uri":"mr.png"},{"uri":"normal.png"}])");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m || m->materials.size() != 2) return;
    const MeshMaterial& a = m->materials[0];
    CHECK(NearF(a.metallic, 0.25f) && NearF(a.roughness, 0.5f));
    CHECK(a.metallicRoughnessImage >= 0 && m->images[a.metallicRoughnessImage].file == "mr.png");
    CHECK(a.normalImage >= 0 && m->images[a.normalImage].file == "normal.png" && NearF(a.normalScale, 0.7f));
    CHECK(a.emissiveImage >= 0 && m->images[a.emissiveImage].file == "glow.png");
    CHECK(NearF(a.emissive.r, 4) && NearF(a.emissive.g, 2) && NearF(a.emissive.b, 0));  // 強さ倍
    CHECK(a.image == -1);  // 色の絵なし
    // 0..1 の外: 中に入れる。
    CHECK(NearF(m->materials[1].metallic, 1) && NearF(m->materials[1].roughness, 0));
    // 法線マップのテクスチャがない場合も、ほかと同じく知らせる。
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{"normalTexture":{"index":5}},{}])")), nullptr, &error));
    CHECK(error.find("テクスチャ 5") != std::string::npos);
}

RM_TEST(Mesh_Gltf_PicturesInsideTheFile) {
    // バッファビューに絵が 1 つ (.glb のように)、data: URI に 1 つ。3 つ目のマテリアルは 1 つ目と共有。
    const std::string doc = TwoMaterialGltf(
        R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{"pbrMetallicRoughness":{"baseColorTexture":{"index":1}}},)"
        R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
        R"("textures":[{"source":0},{"source":1}],"images":[{"bufferView":2,"mimeType":"image/png"},{"uri":"data:image/png;base64,AAEC"}])",
        R"(,{"buffer":0,"byteOffset":0,"byteLength":4})", "0", "2");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->images.size() == 2);  // 使う側が誰でも、1 回ずつ読む
    CHECK(m->materials.size() == 3 && m->materials[0].image == m->materials[2].image);
    CHECK(m->images[m->materials[0].image].bytes == std::vector<uint8_t>({0, 0, 0, 0}));  // 三角形の最初の float、0
    CHECK(m->images[m->materials[1].image].bytes == std::vector<uint8_t>({0, 1, 2}));
    // どちらの三角形も絵 0 を使う (マテリアル 0 と 2): パーツは 2 つ。
    CHECK(m->parts.size() == 2 && m->parts[0].material == 0 && m->parts[1].material == 2);
}

RM_TEST(Mesh_Gltf_BrokenMaterialsAreToldWhy) {
    std::string error;
    // 存在しないマテリアル。
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{}])")), nullptr, &error));
    CHECK(error.find("材質 1") != std::string::npos);
    // 存在しないテクスチャ。バッファの外を指す絵のバッファビュー。
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":3}}},{}])")), nullptr, &error));
    CHECK(error.find("テクスチャ 3") != std::string::npos);
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{}],)"
                                           R"("textures":[{"source":0}],"images":[{"bufferView":2}])",
                                           R"(,{"buffer":0,"byteOffset":40,"byteLength":40})")),
                     nullptr, &error));
    CHECK(error.find("はみ出して") != std::string::npos);
    // マテリアルなし: パーツ 1 つ、白で描く。
    const auto plain = ParseGltf(Bytes(TriangleGltf(Embedded(TriangleBin()))), nullptr, &error);
    CHECK(plain && plain->parts.size() == 1 && plain->parts[0].material == -1 && plain->parts[0].indexCount == 3);
}

RM_TEST(Mesh_Obj_MaterialsFromMtl) {
    const std::string obj =
        "mtllib mats\\colors.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 1\n"
        "usemtl Brick Red\nf 1/1 2/1 3/2\nusemtl Plain\nf 1 3 4\nusemtl Brick Red\nf 1 2 4\nusemtl Ghost\nf 2 3 4\n";
    const std::string mtl =
        "# made by hand\nnewmtl Brick Red\nKd 0.8 0.2 0.1\nmap_Kd -s 2 2 1 -clamp on tex\\brick wall.png\nNs 250\n"
        "map_Bump -bm 0.5 tex/brick_n.png\nKe 0.5 0.5 0.5\nmap_Ke glow.png\n"
        "newmtl Plain\nKd 0.5\nd 0.25\nPm 0.8\nPr 0.3\n";
    std::vector<std::string> asked;
    const ReadRelated read = [&](std::string_view uri) -> std::optional<std::vector<uint8_t>> {
        asked.emplace_back(uri);
        return uri == "mats/colors.mtl" ? TextFile(mtl) : std::nullopt;
    };
    std::string error;
    const auto m = ParseObj(obj, read, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(asked.size() == 1 && asked[0] == "mats/colors.mtl");  // Windows のツールが書く '\'
    CHECK(m->materials.size() == 3);
    if (m->materials.size() != 3) return;
    // Kd は画面の色: リニアな光に直す。
    const Color brick{ScreenToLinear(0.8f), ScreenToLinear(0.2f), ScreenToLinear(0.1f), 1};
    CHECK(m->materials[0].name == "Brick Red" && m->materials[0].baseColor == brick);
    CHECK(m->materials[0].image == 0 && m->images.size() == 3 && m->images[0].file == "mats/tex/brick wall.png");  // .mtl の隣
    CHECK(NearF(m->materials[0].roughness, 0.5f) && NearF(m->materials[0].metallic, 0));  // Ns 250: Blender の 0.5
    CHECK(m->materials[0].normalImage == 1 && m->images[1].file == "mats/tex/brick_n.png" && NearF(m->materials[0].normalScale, 0.5f));
    CHECK(m->materials[0].emissiveImage == 2 && NearF(m->materials[0].emissive.r, ScreenToLinear(0.5f)));
    CHECK(m->materials[1].name == "Plain" && NearF(m->materials[1].baseColor.g, ScreenToLinear(0.5f)) && NearF(m->materials[1].baseColor.a, 0.25f));
    CHECK(NearF(m->materials[1].metallic, 0.8f) && NearF(m->materials[1].roughness, 0.3f));
    CHECK(m->materials[1].alpha == AlphaMode::Blend && m->materials[0].alpha == AlphaMode::Opaque);
    CHECK(m->materials[2].name == "Ghost" && m->materials[2].baseColor == (Color{1, 1, 1, 1}));  // .mtl にない: 白
    CHECK(m->warnings.size() == 1 && m->warnings[0].find("Ghost") != std::string::npos);
    // 三角形はマテリアル順に並べ、最初に使われた順にする (Brick Red の 2 つは順を保つ)。
    CHECK(m->parts.size() == 3);
    if (m->parts.size() != 3) return;
    CHECK((m->parts[0] == MeshPart{0, 6, 0}) && (m->parts[1] == MeshPart{6, 3, 1}) && (m->parts[2] == MeshPart{9, 3, 2}));
    CHECK(Near(m->vertices[m->indices[5]].position, {0, 1, 0}));  // Brick Red の 2 つ目の三角形 (1 2 4) は 3..5 にある
    // .mtl がなくてもモデルは壊れていない: 白にして、警告を出す。
    const auto missing = ParseObj(obj, [](std::string_view) -> std::optional<std::vector<uint8_t>> { return std::nullopt; }, &error);
    CHECK(missing && missing->materials.size() == 3 && missing->warnings.size() == 1 && missing->warnings[0].find("colors.mtl") != std::string::npos);
}

RM_TEST(Mesh_SortByMaterial_KeepsTheOrderInside) {
    MeshData m;
    m.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    m.materials.resize(2);
    const int of[] = {1, -1, 1, 7};  // 7 はマテリアルではない: なし
    SortByMaterial(m, of);
    CHECK(m.indices == std::vector<uint32_t>({0, 1, 2, 6, 7, 8, 3, 4, 5, 9, 10, 11}));
    CHECK(m.parts.size() == 2 && (m.parts[0] == MeshPart{0, 6, 1}) && (m.parts[1] == MeshPart{6, 6, -1}));
}

RM_TEST(MeshFile_RelatedFileStaysInside) {
    const std::filesystem::path root = L"C:/P/Assets";
    const std::filesystem::path model = L"C:/P/Assets/Models/house.gltf";
    CHECK(RelatedFile(model, "tex/wood.png", root) == std::filesystem::path(L"C:/P/Assets/Models/tex/wood.png").lexically_normal());
    CHECK(RelatedFile(model, "../Textures/wood.png", root) == std::filesystem::path(L"C:/P/Assets/Textures/wood.png").lexically_normal());
    CHECK(RelatedFile(model, "../../secret.png", root).empty());  // プロジェクトの Assets の外
    CHECK(RelatedFile(model, "C:/elsewhere/a.png", root).empty());
    CHECK(RelatedFile(model, "/a.png", root).empty());
    CHECK(RelatedFile(model, "", root).empty());
    // モデル自身のフォルダの下だけ (.gltf のバッファ、OBJ の .mtl)。
    CHECK(RelatedFile(model, "../x.bin", model.parent_path()).empty());
    CHECK(!RelatedFile(model, "bin/x.bin", model.parent_path()).empty());
}

RM_TEST(Space3d_BackToFront) {
    // 透ける物は遠い方から描く。同じ距離なら与えられた順を保つ。
    const Vec3 eye{0, 0, 10};
    const std::vector<Vec3> centers{{0, 0, 5}, {0, 0, -5}, {0, 0, 0}, {5, 0, 10}, {0, 0, 0}};
    CHECK(BackToFront(centers, eye) == std::vector<size_t>({1, 2, 4, 0, 3}));
    CHECK(BackToFront({}, eye).empty());
}

namespace {

// 64 x 64 の絵で、アルファは 0 .. 255 に散らばる (固定の並びで、模様ではない)。
platform::Image Speckled() {
    platform::Image img;
    img.width = img.height = 64;
    img.pixels.resize(64 * 64 * 4);
    uint32_t x = 12345;
    for (size_t i = 0; i < 64 * 64; ++i) {
        x = x * 1664525u + 1013904223u;
        img.pixels[i * 4 + 0] = img.pixels[i * 4 + 1] = img.pixels[i * 4 + 2] = 200;
        img.pixels[i * 4 + 3] = static_cast<uint8_t>(x >> 24);
    }
    return img;
}

}  // namespace

RM_TEST(TexturePixels_NoMipmapsAndKeptCoverage) {
    // ミップマップ off: 絵 1 枚だけ。
    const TexturePixels one = MakeTexturePixels(Speckled(), true, {false, 0});
    CHECK(one.levels.size() == 1 && one.levels[0].width == 64);
    // 透明部分を保つ: 平均すると、小さいコピーで 0.8 に届くテクセルは少ない (切り抜きが薄れる)。
    // 保つと、どのコピーも絵と同じくらいの面積を覆う。
    const float cutoff = 0.8f;
    const float whole = AlphaCoverage(Speckled(), cutoff);
    CHECK(whole > 0.15f && whole < 0.25f);
    const TexturePixels plain = MakeTexturePixels(Speckled(), true);
    const TexturePixels kept = MakeTexturePixels(Speckled(), true, {true, cutoff});
    CHECK(plain.levels.size() == 7 && kept.levels.size() == 7);
    CHECK(AlphaCoverage(plain.levels[1], cutoff) < whole * 0.5f);
    for (size_t i = 1; i <= 3; ++i) CHECK(std::abs(AlphaCoverage(kept.levels[i], cutoff) - whole) < 0.06f);
    CHECK(kept.levels[0].pixels == plain.levels[0].pixels);  // 絵そのものはそのまま
    CHECK(kept.levels[1].pixels[0] == plain.levels[1].pixels[0]);  // 変わるのはアルファだけ
}

RM_TEST(TextureSampling_PackedInOneByte) {
    // フィルター / ラップ / ストリーミングは全部戻ってくる。前のファイルのバイト (0 / 1: くっきり) も同じに読める。
    for (const auto filter : {asset::TextureFilter::Smooth, asset::TextureFilter::Sharp}) {
        for (const auto wrap : {asset::TextureWrap::Repeat, asset::TextureWrap::Clamp, asset::TextureWrap::Mirror}) {
            for (const bool streamed : {false, true}) {
                const TextureSampling s{filter, wrap, streamed};
                CHECK(UnpackSampling(PackSampling(s)) == s);
                CHECK(SamplerIndex(s) < kSamplerCount);
            }
        }
    }
    CHECK(UnpackSampling(0) == TextureSampling{});
    CHECK(UnpackSampling(1) == (TextureSampling{asset::TextureFilter::Sharp, asset::TextureWrap::Repeat, false}));
    // 取り込みの設定から: ストリーミングはミップマップがあるときだけ。カバレッジは頼まれたときだけ。
    asset::ImageImport imp;
    imp.streaming = true;
    imp.mipmaps = false;
    imp.keepCoverage = true;
    CHECK(!SamplingOf(imp).streamed && !MipOptionsOf(imp).mipmaps && MipOptionsOf(imp).keepCoverage == 0.f);
    imp.mipmaps = true;
    CHECK(SamplingOf(imp).streamed && MipOptionsOf(imp).keepCoverage == 0.5f);
}

RM_TEST(TexturePixels_MipmapsAveragedAsLight) {
    // 4 x 2: 左半分が黒、右半分が白 (色の絵)、アルファ 255 / 0。
    platform::Image img;
    img.width = 4;
    img.height = 2;
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            const uint8_t v = x < 2 ? 0 : 255;
            img.pixels.insert(img.pixels.end(), {v, v, v, static_cast<uint8_t>(x % 2 ? 0 : 255)});
        }
    }
    const TexturePixels colour = MakeTexturePixels(img, true);
    CHECK(colour.srgb && colour.levels.size() == 3);  // 4x2, 2x1, 1x1
    if (colour.levels.size() != 3) return;
    CHECK(colour.levels[1].width == 2 && colour.levels[1].height == 1 && colour.levels[2].width == 1 && colour.levels[2].height == 1);
    CHECK(colour.levels[1].pixels[0] == 0 && colour.levels[1].pixels[4] == 255);
    // 黒と白を半分ずつ、光として平均すると画面の 188 で、128 ではない (128 だと暗く見える)。
    CHECK(std::abs(int(colour.levels[2].pixels[0]) - 188) <= 1);
    CHECK(std::abs(int(colour.levels[2].pixels[3]) - 128) <= 1);  // アルファ: そのまま
    // 数値 (法線マップ、金属 / ラフネス) はそのまま平均する。
    const TexturePixels numbers = MakeTexturePixels(img, false);
    CHECK(!numbers.srgb && numbers.levels.size() == 3 && std::abs(int(numbers.levels[2].pixels[0]) - 128) <= 1);
    // 空の絵: GPU に置くものはない。
    CHECK(MakeTexturePixels(platform::Image{}, true).levels.empty());
}

// --- テクスチャの圧縮 (BlockCompress / TextureFile / UploadSlice) -----------------------

namespace {

// w x h の絵: テクセルごとに数段階のなめらかなグラデーション (写真や描いた絵のほとんどがそう)
// と、やわらかいアルファ。
platform::Image Gradient(uint32_t w, uint32_t h, bool alpha) {
    platform::Image img;
    img.width = w;
    img.height = h;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            img.pixels.push_back(static_cast<uint8_t>(x * 255 / (w - 1)));
            img.pixels.push_back(static_cast<uint8_t>(y * 255 / (h - 1)));
            img.pixels.push_back(static_cast<uint8_t>(128 + 60 * std::sin(x * 0.1 + y * 0.07)));
            img.pixels.push_back(alpha ? static_cast<uint8_t>((x + y) * 255 / (w + h - 2)) : 255);
        }
    }
    return img;
}

// 各テクセルの `channels` 個ぶんの誤差の 2 乗平均の平方根と、最大の誤差。
struct Error {
    double rms = 0;
    int most = 0;
};
Error Compare(const platform::Image& a, const platform::Image& b, int channels) {
    Error e;
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.pixels.size() && i + 3 < b.pixels.size(); i += 4) {
        for (int k = 0; k < channels; ++k) {
            const int d = std::abs(int(a.pixels[i + k]) - int(b.pixels[i + k]));
            sum += d * d;
            e.most = std::max(e.most, d);
            ++n;
        }
    }
    e.rms = n ? std::sqrt(sum / n) : 0;
    return e;
}

Error RoundTrip(const platform::Image& img, TextureFormat f, int channels) {
    TexturePixels p;
    p.levels.push_back(img);
    const TexturePixels c = Compress(p, f);
    CHECK(c.format == f && c.levels.size() == 1 && c.levels[0].pixels.size() == LevelBytes(f, img.width, img.height));
    return Compare(img, Decompress(c.levels[0], f), channels);
}

}  // namespace

RM_TEST(TextureFormat_SizesOfLevels) {
    CHECK(RowPitch(TextureFormat::Rgba8, 5) == 20 && LevelBytes(TextureFormat::Rgba8, 5, 3) == 60);
    CHECK(RowPitch(TextureFormat::BC1, 5) == 16 && LevelBytes(TextureFormat::BC1, 5, 3) == 16);  // 8 バイトのブロック 2 x 1 個
    CHECK(LevelBytes(TextureFormat::BC7, 1, 1) == 16 && LevelBytes(TextureFormat::BC5, 8, 8) == 64);
    CHECK(ChannelsOf(TextureFormat::BC4) == 1 && ChannelsOf(TextureFormat::BC5) == 2 && ChannelsOf(TextureFormat::BC1) == 3);
}

RM_TEST(TextureFormat_ChosenByUseAndSettings) {
    using C = asset::TextureCompression;
    const PictureTraits opaque{false, false}, alpha{true, false}, gray{false, true};
    // 自動。
    CHECK(ChooseFormat(TextureUse::Color, C::Auto, opaque, true, 256, 256) == TextureFormat::BC1);
    CHECK(ChooseFormat(TextureUse::Color, C::Auto, alpha, true, 256, 256) == TextureFormat::BC7);
    CHECK(ChooseFormat(TextureUse::Color, C::Auto, alpha, false, 256, 256) == TextureFormat::BC3);  // フィーチャーレベル 10.0
    CHECK(ChooseFormat(TextureUse::Normal, C::Auto, opaque, true, 256, 256) == TextureFormat::BC5);
    CHECK(ChooseFormat(TextureUse::Data, C::Auto, gray, true, 256, 256) == TextureFormat::BC4);
    CHECK(ChooseFormat(TextureUse::Data, C::Auto, opaque, true, 256, 256) == TextureFormat::BC1);
    // ブロックにきっちり分けられない一番上: そのまま残す。
    CHECK(ChooseFormat(TextureUse::Color, C::Auto, opaque, true, 250, 256) == TextureFormat::Rgba8);
    // 指定どおり。古い GPU の BC7 は BC3。用途が読めない指定は自動として扱う。
    CHECK(ChooseFormat(TextureUse::Color, C::None, opaque, true, 256, 256) == TextureFormat::Rgba8);
    CHECK(ChooseFormat(TextureUse::Color, C::BC7, opaque, true, 256, 256) == TextureFormat::BC7);
    CHECK(ChooseFormat(TextureUse::Color, C::BC7, opaque, false, 256, 256) == TextureFormat::BC3);
    CHECK(ChooseFormat(TextureUse::Color, C::BC4, opaque, true, 256, 256) == TextureFormat::BC1);
    CHECK(ChooseFormat(TextureUse::Color, C::BC5, alpha, true, 256, 256) == TextureFormat::BC7);
    CHECK(ChooseFormat(TextureUse::Data, C::BC4, opaque, true, 256, 256) == TextureFormat::BC1);  // グレーではない
    CHECK(ChooseFormat(TextureUse::Normal, C::BC1, opaque, true, 256, 256) == TextureFormat::BC1);
    // 絵の種類。
    const PictureTraits t = TraitsOf(Gradient(8, 8, true));
    CHECK(t.alpha && !t.gray);
    platform::Image g;
    g.width = g.height = 1;
    g.pixels = {9, 9, 9, 255};
    CHECK(TraitsOf(g).gray && !TraitsOf(g).alpha);
}

RM_TEST(BlockCompress_ErrorsAreSmall) {
    const platform::Image smooth = Gradient(64, 64, false);
    const Error bc1 = RoundTrip(smooth, TextureFormat::BC1, 3);
    CHECK(bc1.rms < 4.5 && bc1.most < 16);
    // BC4: ブロックの最小と最大の間を 8 段階に: 誤差は半段階以内。
    const Error bc4 = RoundTrip(smooth, TextureFormat::BC4, 1);
    CHECK(bc4.rms < 1.5 && bc4.most <= 2);
    const Error bc5 = RoundTrip(smooth, TextureFormat::BC5, 2);
    CHECK(bc5.rms < 1.5 && bc5.most <= 2);
    const platform::Image soft = Gradient(64, 64, true);
    const Error bc3 = RoundTrip(soft, TextureFormat::BC3, 4);
    CHECK(bc3.rms < 4.0);
    const Error bc7 = RoundTrip(soft, TextureFormat::BC7, 4);
    CHECK(bc7.rms < 3.5 && bc7.most < 14);
    // 1 色: BC4 はぴったり。BC1 は 5 / 6 / 5 ビットの範囲内。BC7 は 7 + 1 ビットの範囲内。
    platform::Image flat;
    flat.width = flat.height = 4;
    for (int i = 0; i < 16; ++i) flat.pixels.insert(flat.pixels.end(), {200, 100, 50, 77});
    CHECK(RoundTrip(flat, TextureFormat::BC4, 1).most == 0);
    CHECK(RoundTrip(flat, TextureFormat::BC1, 3).most <= 4);
    CHECK(RoundTrip(flat, TextureFormat::BC7, 4).most <= 1);
    // 4 の倍数ではない大きさの端のブロックは、全部埋めて作る (最後のテクセルをもう一度使う)。
    const Error odd = RoundTrip(Gradient(30, 22, false), TextureFormat::BC1, 3);
    CHECK(odd.rms < 8.0);
}

RM_TEST(TextureFile_DdsRoundTrip) {
    TexturePixels p = Compress(MakeTexturePixels(Gradient(8, 8, true), true), TextureFormat::BC7);
    CHECK(p.levels.size() == 4);  // 8, 4, 2, 1
    const std::vector<uint8_t> dds = WriteDds(p);
    CHECK(dds.size() == 148 + 4 * 16 + 16 + 16 + 16);
    std::string error;
    const auto back = ReadDds(dds, &error);
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->format == TextureFormat::BC7 && back->srgb && back->levels.size() == 4);
    for (size_t i = 0; i < 4; ++i) CHECK(back->levels[i].pixels == p.levels[i].pixels && back->levels[i].width == p.levels[i].width);
    CHECK(DxgiFormatOf(TextureFormat::BC7, true) == 99 && DxgiFormatOf(TextureFormat::BC5, true) == 83);  // BC5 に sRGB はない
    // 途中で切れた / DDS ではない: 知らせる。終わりより先は読まない。
    std::vector<uint8_t> cut(dds.begin(), dds.end() - 10);
    CHECK(!ReadDds(cut, &error) && !error.empty());
    CHECK(!ReadDds(std::vector<uint8_t>(10, 0), &error));
}

RM_TEST(TextureFile_CacheName) {
    CHECK(HashBytes({}) == 14695981039346656037ull);
    const uint8_t a[] = {'a'};
    CHECK(HashBytes(a) == 0xaf63dc4c8601ec8cull);
    const std::string id = "0123456789abcdef0123456789abcdef";
    const std::string n = TextureCacheName(id, 42, R"({"compression":"Auto"})", TextureUse::Color, true, true);
    CHECK(n.starts_with(id + "_") && n.ends_with(".dds") && n.size() == id.size() + 1 + 16 + 4);
    CHECK(n == TextureCacheName(id, 42, R"({"compression":"Auto"})", TextureUse::Color, true, true));  // 同じ: 同じファイル
    // 頼っているものが 1 つでも変わった: 別のファイル (古いファイルを間違えて読むことはない)。
    CHECK(n != TextureCacheName(id, 43, R"({"compression":"Auto"})", TextureUse::Color, true, true));
    CHECK(n != TextureCacheName(id, 42, R"({"compression":"BC7"})", TextureUse::Color, true, true));
    CHECK(n != TextureCacheName(id, 42, R"({"compression":"Auto"})", TextureUse::Normal, true, true));
    CHECK(n != TextureCacheName(id, 42, R"({"compression":"Auto"})", TextureUse::Color, true, false));
    CHECK(n != TextureCacheName(id + "#1", 42, R"({"compression":"Auto"})", TextureUse::Color, true, true));
    // sRGB として読むか数値として読むか (BC7 のない GPU での 3D の色と、ゲーム用に焼いたスプライト:
    // 用途も BC7 も同じだが、ファイルを共有してはいけない)。
    CHECK(TextureCacheName(id, 42, "", TextureUse::Color, true, false) != TextureCacheName(id, 42, "", TextureUse::Color, false, false));
    CHECK(TextureCacheName("../x:y", 1, "", TextureUse::Data, false, true).starts_with("xy_"));  // 名前には安全な文字だけ
    CHECK(TextureCacheName("", 1, "", TextureUse::Data, false, true).starts_with("noid_"));
}

RM_TEST(UploadSlice_TimePerFrame) {
    UploadSlice s(0.002);
    CHECK(s.May(1));
    s.Spent(0.0015);
    CHECK(s.May(1));  // 2 ms のうち 1.5 ms: あと 1 つ
    s.Spent(0.001);
    CHECK(!s.May(1) && !s.May(1) && s.Deferred(1) == 2);  // 2.5 ms: 残りは待つ
    CHECK(s.May(2) && s.Deferred(2) == 0);                // 新しいフレーム: 先送りしたぶんを今出す
    s.Spent(0.05);                                        // 大きなコピー 1 つ: 予算より長い ...
    CHECK(!s.May(2) && s.Deferred(2) == 1);
    CHECK(s.May(3));  // ... でも毎フレームの最初のコピーは必ず通る (止まらない)
    s.Spent(0);
    CHECK(s.Deferred(3) == 0);
    // レビューのケース: フレーム 10 は A を送り (予算超え) B を先送りする。フレーム 11 は B を送る。
    // そのあとは待つものがないので、どのフレームも聞かない (May は呼ばれない)。それらのフレームで
    // 先送りがあると言ってはいけない (言うと、ビューが何もせず永遠に描き直す)。
    UploadSlice t(0.002);
    CHECK(t.May(10));
    t.Spent(0.01);
    CHECK(!t.May(10) && t.Deferred(10) == 1);
    CHECK(t.May(11));
    t.Spent(0.001);
    CHECK(t.Deferred(11) == 0 && t.Deferred(12) == 0 && t.Deferred(13) == 0);
}

RM_TEST(Space3d_BoundsAndShadowMatrix) {
    // モデルの境界をワールドに置いたものと、単位立方体では外れる所に当たるレイ。
    Transform t;
    t.position = {10, 0, 0};
    t.rotation = {0, 90, 0};
    const Mat4 w = LocalMatrix(t);
    const Bounds b = WorldBounds(w, {-1, 0, -3}, {1, 2, 3});
    CHECK(Near(b.min, {7, 0, -1}) && Near(b.max, {13, 2, 1}));
    const Ray ray{{12.5f, 1, 50}, {0, 0, -1}};
    CHECK(!IntersectPlacedCube(ray, w).has_value());
    const auto hit = IntersectPlacedBox(ray, w, {-1, 0, -3}, {1, 2, 3});
    CHECK(hit && NearF(*hit, 49.f));
    // 正射影: 箱の角は端に来る。
    const Mat4 o = Orthographic(-2, 2, -1, 1, 1, 11);
    const auto back = ProjectToNdc(o, {2, 1, -11});
    CHECK(back && Near(*back, {1, 1, 1}));
    const auto front = ProjectToNdc(o, {-2, -1, -1});
    CHECK(front && Near(*front, {-1, -1, 0}));
    // 光の行列は、影を落とすものの角を全部含む。
    const Bounds casters{{-1, 0, -1}, {1, 3, 2}};
    const Vec3 lightDir{-0.45f, -1.f, -0.3f};
    const Mat4 light = DirectionalShadowMatrix(lightDir, casters);
    for (int i = 0; i < 8; ++i) {
        const Vec3 p{(i & 1) ? casters.max.x : casters.min.x, (i & 2) ? casters.max.y : casters.min.y, (i & 4) ? casters.max.z : casters.min.z};
        const auto q = ProjectToNdc(light, p);
        CHECK(q && std::abs(q->x) <= 1 && std::abs(q->y) <= 1 && q->z >= 0 && q->z <= 1);
    }
    // 一番上の角の影は、床の上のマップの同じ位置に、より深く落ちる。
    const Vec3 top{1, 3, 2};
    const Vec3 dir = Normalize(lightDir);
    const Vec3 onFloor = top + dir * (3.f / -dir.y);
    const auto a = ProjectToNdc(light, top), f = ProjectToNdc(light, onFloor);
    CHECK(a && f && NearF(a->x, f->x) && NearF(a->y, f->y) && f->z > a->z);
    // 真下でも動く (別の上を使う)。
    const auto down = ProjectToNdc(DirectionalShadowMatrix({0, -1, 0}, casters), {0, 1, 0});
    CHECK(down && std::abs(down->x) <= 1 && down->z >= 0 && down->z <= 1);
}

RM_TEST(MeshRenderer_SavesItsModel) {
    game_x_3d::MeshRenderer m;
    CHECK(m.model.Empty() && m.castShadows);
    m.model.guid = "0123456789abcdef0123456789abcdef";  // アセットの参照: ID で保存する
    m.castShadows = false;
    const json::Value j = reflect::Save(m);
    CHECK(j.Get("model").AsString() == m.model.guid);
    game_x_3d::MeshRenderer back;
    reflect::Load(back, j);
    CHECK(back.model == m.model && !back.castShadows);
    CHECK(std::string_view(reflect::TypeOf<game_x_3d::MeshRenderer>().fields[1].name) == "model");  // (after 形)
    CHECK(std::string_view(reflect::TypeOf<game_x_3d::MeshRenderer>().fields[1].assetKinds) == "model");
    // 前の参照のデータ (パスの文字列) はそのまま読む (SoftwareHost::AssetFile がファイルを見つける)。
    json::Value old = j;
    old.Set("model", "Assets/Models/robot.glb");
    reflect::Load(back, old);
    CHECK(back.model.guid == "Assets/Models/robot.glb");
}

RM_TEST(SceneViewState_KeepsShadows) {
    game_x_3d::SceneViewState st;
    CHECK(st.shadows);  // 最初はオン
    st.shadows = false;
    game_x_3d::SceneViewState back;
    reflect::Load(back, reflect::Save(st));
    CHECK(!back.shadows);
}

RM_TEST(Space3d_ViewFromWorld_LooksAlongMinusZ) {
    // (0, 0, 5) で回していない: ワールドの真ん中は 5 手前 (ビュー空間は -Z を見る)。
    Transform t;
    t.position = {0, 0, 5};
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(t)), {0, 0, 0}), {0, 0, -5}));
    // Y まわりに 90 度回すと -X を見る。スケールは何も変えない。
    Transform turned;
    turned.rotation = {0, 90, 0};
    turned.scale = {2, 3, 4};
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(turned)), {-3, 0, 0}), {0, 0, -3}));
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(turned)), {0, 1, 0}), {0, 1, 0}));  // 上は上のまま
    // 真下を見ても、ビューはある。
    Transform down;
    down.rotation = {-90, 0, 0};
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(down)), {0, -2, 0}), {0, 0, -2}));
}

RM_TEST(Space3d_FrustumCorners) {
    // 90 度、正方形、near 1、far 10、真ん中から -Z を見る。
    const auto k = FrustumCorners(Perspective(90, 1, 1, 10));
    CHECK(k.has_value());
    if (!k) return;
    CHECK(Near((*k)[0], {-1, -1, -1}) && Near((*k)[1], {1, -1, -1}) && Near((*k)[2], {1, 1, -1}) && Near((*k)[3], {-1, 1, -1}));
    CHECK(Near((*k)[4], {-10, -10, -10}) && Near((*k)[6], {10, 10, -10}));
    // 逆行列を作れない行列には、逆行列はない。
    Mat4 flat;
    flat.m[0][0] = 0;
    CHECK(!FrustumCorners(flat).has_value());
}

RM_TEST(Camera_Projection) {
    game_x_3d::Camera cam;  // 透視 60 度、0.1 .. 1000
    const Mat4 p = game_x_3d::ProjectionOf(cam, 2.f);
    const Mat4 want = Perspective(60, 2.f, 0.1f, 1000.f);
    CHECK(std::memcmp(&p, &want, sizeof(Mat4)) == 0);
    // 正射影: 高さの半分が大きさで、幅は縦横比に従う。どの奥行きでも同じ大きさ。
    cam.projection = game_x_3d::CameraProjection::Orthographic;
    cam.orthographicSize = 5;
    const Mat4 o = game_x_3d::ProjectionOf(cam, 2.f);
    const auto near1 = ProjectToNdc(o, {10, 5, -1}), far1 = ProjectToNdc(o, {10, 5, -500});
    CHECK(near1 && far1 && NearF(near1->x, 1) && NearF(near1->y, 1) && NearF(far1->x, 1) && NearF(far1->y, 1));
    CHECK(near1 && far1 && near1->z >= 0 && far1->z <= 1 && far1->z > near1->z);
    // far が near より先にない (入力中の) ときも、絵は出る: NaN にならない。
    cam.projection = game_x_3d::CameraProjection::Perspective;
    cam.nearClip = 5;
    cam.farClip = 1;
    const auto q = ProjectToNdc(game_x_3d::ProjectionOf(cam, 1.f), {0, 0, -5});
    CHECK(q && std::isfinite(q->z));
    // シーンビューは、図形をこの距離までしか描かない。
    game_x_3d::Camera wide;
    CHECK(NearF(game_x_3d::ShownNear(wide, 10).farClip, 10) && NearF(game_x_3d::ShownNear(wide, 10).nearClip, 0.1f));
    wide.farClip = 3;
    CHECK(NearF(game_x_3d::ShownNear(wide, 10).farClip, 3));
}

RM_TEST(Camera_Background) {
    game_x_3d::Camera cam;
    const Color game{0.2f, 0.3f, 0.4f, 1};
    const Color a = game_x_3d::BackgroundOf(cam, game);  // ゲームの設定の 背景の色。カメラ自身の指定があればそちら
    CHECK(NearF(a.r, game.r) && NearF(a.g, game.g) && NearF(a.b, game.b));
    cam.ownBackground = true;
    cam.background = {1, 0, 0, 1};
    const Color b = game_x_3d::BackgroundOf(cam, game);
    CHECK(NearF(b.r, 1) && NearF(b.g, 0) && NearF(b.b, 0));
}

RM_TEST(Camera_Light_SaveByName) {
    game_x_3d::Camera cam;
    cam.projection = game_x_3d::CameraProjection::Orthographic;
    const json::Value j = reflect::Save(cam);
    CHECK(j.Get("projection").AsString() == "Orthographic");  // 列挙は名前で
    game_x_3d::Camera back;
    reflect::Load(back, j);
    CHECK(back.projection == game_x_3d::CameraProjection::Orthographic);
    game_x_3d::Light light;
    light.kind = game_x_3d::LightKind::Spot;
    game_x_3d::Light lb;
    reflect::Load(lb, reflect::Save(light));
    CHECK(lb.kind == game_x_3d::LightKind::Spot);
    json::Value big = reflect::Save(light);
    big.Set("intensity", 1000.0);  // ほかの数と同じく範囲に収める (Max=100)
    reflect::Load(lb, big);
    CHECK(NearF(lb.intensity, 100));
}

RM_TEST(Lighting_DefaultLight) {
    // 光なし: 白い平行光 1 つ (初期の向き) と、影。
    const LightSetup s = MakeLights({}, {0, -2, 0});
    CHECK(NearF(s.block.info[0], 1) && s.dropped == 0);
    CHECK(NearF(s.block.position[0][3], 0));    // 平行光
    CHECK(NearF(s.block.direction[0][1], -1));  // 長さ 1 にした
    CHECK(NearF(s.block.color[0][0], 1) && NearF(s.block.color[0][1], 1) && NearF(s.block.color[0][2], 1));
    CHECK(s.shadowLight == 0 && NearF(s.block.color[0][3], 1) && Near(s.shadowDirection, {0, -1, 0}));
}

RM_TEST(Lighting_Values) {
    std::vector<Light> lights(3);
    lights[0].type = LightType::Point;
    lights[0].position = {1, 2, 3};
    lights[0].color = {1, 0.5f, 0, 1};
    lights[0].intensity = 2;
    lights[0].range = 4;
    lights[1].type = LightType::Directional;  // 影を落とさない
    lights[1].castShadow = false;
    lights[2].type = LightType::Directional;  // 最初に影を落とすもの: 影のマップはその光のもの
    lights[2].direction = {1, 0, 0};
    const LightSetup s = MakeLights(lights, {0, -1, 0});
    // 平行光が先 (その順で)、次に点光源。
    CHECK(NearF(s.block.info[0], 3));
    CHECK(NearF(s.block.position[0][3], 0) && NearF(s.block.position[1][3], 0) && NearF(s.block.position[2][3], 1));
    CHECK(NearF(s.block.position[2][0], 1) && NearF(s.block.position[2][2], 3));
    CHECK(NearF(s.block.color[2][0], 2) && NearF(s.block.color[2][1], 1) && NearF(s.block.color[2][2], 0));  // 色 x 強さ
    CHECK(NearF(s.block.direction[2][3], 4));  // 範囲
    CHECK(NearF(s.block.color[0][3], 0) && NearF(s.block.color[1][3], 1) && NearF(s.block.color[2][3], 0));
    CHECK(s.shadowLight == 1 && Near(s.shadowDirection, {1, 0, 0}));
    // 点光源は影のマップを使わない (平行光だけ)。
    std::vector<Light> points(1);
    points[0].type = LightType::Point;
    CHECK(MakeLights(points, {0, -1, 0}).shadowLight == -1);
    // スポット: 円錐の縁は角度の半分。弱まり始めるのはその 4 分の 3 から。
    Light spot;
    spot.type = LightType::Spot;
    spot.spotAngle = 60;
    spot.intensity = -3;  // 負の光はない
    const LightSetup sp = MakeLights(std::span<const Light>(&spot, 1), {0, -1, 0});
    CHECK(NearF(sp.block.position[0][3], 2));
    CHECK(NearF(sp.block.spot[0][0], std::cos(30.f * 3.14159265f / 180.f)) && NearF(sp.block.spot[0][1], std::cos(22.5f * 3.14159265f / 180.f)));
    CHECK(NearF(sp.block.color[0][0], 0));
}

RM_TEST(Lighting_AtMostEight) {
    std::vector<Light> many(11);
    for (Light& l : many) l.type = LightType::Point;
    many[9].type = LightType::Directional;  // 並びは後ろだが、平行光は全部を照らすので、先に来る
    const LightSetup s = MakeLights(many, {0, -1, 0});
    CHECK(NearF(s.block.info[0], static_cast<float>(kMaxLights)) && s.dropped == 3 && s.shadowLight == 0);
    CHECK(NearF(s.block.position[0][3], 0));
}

RM_TEST(Light_FromItsEntity) {
    // X まわりに -90 度回すと、光の -Z は下を向く。位置はエンティティの位置。
    game_x_3d::Light light;
    light.kind = game_x_3d::LightKind::Spot;
    light.intensity = 3;
    Transform t;
    t.position = {4, 5, 6};
    t.rotation = {-90, 0, 0};
    const space3d::Light l = game_x_3d::ToRenderLight(light, LocalMatrix(t));
    CHECK(l.type == LightType::Spot && Near(l.position, {4, 5, 6}) && Near(l.direction, {0, -1, 0}) && NearF(l.intensity, 3));
    t.scale = {3, 3, 3};  // スケールで長くはならない
    CHECK(NearF(Length(game_x_3d::ToRenderLight(light, LocalMatrix(t)).direction), 1));
}

RM_TEST(Space3d_BoxInView_Culling) {
    // (0, 0, 5) から -Z を見る、60 度、正方形、0.1 .. 100。
    const Mat4 vp = Perspective(60, 1, 0.1f, 100) * LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(BoxInView(vp, {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}));   // 真ん中
    CHECK(!BoxInView(vp, {{-0.5f, -0.5f, 6}, {0.5f, 0.5f, 7}}));         // 目の後ろ
    CHECK(!BoxInView(vp, {{20, -0.5f, -0.5f}, {21, 0.5f, 0.5f}}));       // ずっと右
    CHECK(!BoxInView(vp, {{-21, -0.5f, -0.5f}, {-20, 0.5f, 0.5f}}));     // ずっと左
    CHECK(!BoxInView(vp, {{-0.5f, 20, -0.5f}, {0.5f, 21, 0.5f}}));       // 上
    CHECK(!BoxInView(vp, {{-0.5f, -0.5f, -200}, {0.5f, 0.5f, -150}}));   // far 面の先
    // 一部が中にある (縁をまたぐ、または目の後ろから前まで届く): 見える。
    CHECK(BoxInView(vp, {{2, -0.5f, -0.5f}, {30, 0.5f, 0.5f}}));
    CHECK(BoxInView(vp, {{-0.5f, -0.5f, -3}, {0.5f, 0.5f, 10}}));
    // 目のまわりの巨大な箱: 見える (角は全部外だが、同じ側の外ではない)。
    CHECK(BoxInView(vp, {{-1000, -1000, -1000}, {1000, 1000, 1000}}));
    // 正射影でも (カメラ部品のもう 1 つの投影)。
    game_x_3d::Camera cam;
    cam.projection = game_x_3d::CameraProjection::Orthographic;
    cam.orthographicSize = 2;
    const Mat4 o = game_x_3d::ProjectionOf(cam, 1.f) * LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(BoxInView(o, {{1.5f, 1.5f, -0.5f}, {2.5f, 2.5f, 0.5f}}));
    CHECK(!BoxInView(o, {{2.5f, -0.5f, -0.5f}, {3.5f, 0.5f, 0.5f}}));  // 右の縁のすぐ外 (透視で広がらない)
}

// --- 専用のスレッドでモデルを読む (space3d/runtime/MeshLoader) ------------------------

namespace {

// `ticket` が終わるまで (少しずつ) 待つ。約 5 秒たつと nullopt。
std::optional<MeshLoader::Result> WaitFor(MeshLoader& loader, MeshLoader::Ticket ticket) {
    for (int i = 0; i < 500; ++i) {
        if (auto r = loader.Take(ticket)) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

}  // namespace

RM_TEST(MeshLoader_ReadsOnItsThreadInOrder) {
    std::vector<std::string> read;
    std::mutex mu;
    MeshLoader loader([&](const std::filesystem::path& file, std::string* error, ReadBudget&) -> std::optional<MeshData> {
        {
            std::lock_guard lock(mu);
            read.push_back(file.filename().string());
        }
        if (file.filename() == "bad.obj") {
            *error = "読めません";
            return std::nullopt;
        }
        return ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", error);
    });
    CHECK(loader.Pending() == 0);
    const auto a = loader.Request("a.obj");
    const auto b = loader.Request("bad.obj");
    CHECK(a != 0 && b != 0 && a != b);
    const auto ra = WaitFor(loader, a);
    CHECK(ra && ra->mesh && ra->mesh->indices.size() == 3 && ra->seconds >= 0);
    const auto rb = WaitFor(loader, b);
    CHECK(rb && !rb->mesh && rb->error == "読めません");
    CHECK(!loader.Take(a));  // 取れるのは 1 回だけ
    CHECK(loader.Pending() == 0);
    std::lock_guard lock(mu);
    CHECK(read == std::vector<std::string>({"a.obj", "bad.obj"}));
}

RM_TEST(MeshLoader_CancelledIsNotKept) {
    std::atomic<bool> open{false};
    std::atomic<int> calls{0};
    MeshLoader loader([&](const std::filesystem::path&, std::string* error, ReadBudget&) -> std::optional<MeshData> {
        ++calls;
        while (!open) std::this_thread::sleep_for(std::chrono::milliseconds(1));  // テストが開けるまで止めておく
        return ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", error);
    });
    const auto first = loader.Request("first.obj");    // 読んでいる最中 (止めてある)
    const auto second = loader.Request("second.obj");  // 待っている
    while (calls == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    loader.Cancel(first);   // 読み終わると捨てる
    loader.Cancel(second);  // 読まない
    CHECK(loader.Pending() == 0);
    const auto third = loader.Request("third.obj");
    open = true;
    CHECK(WaitFor(loader, third).has_value());
    CHECK(!loader.Take(first) && !loader.Take(second));
    CHECK(calls == 2);  // 最初と 3 つ目
}

RM_TEST(MeshLoader_StopsWhileReading) {
    // 仕事が残ったまま壊す: 読んでいるファイルは待ち、残りは捨てる (クラッシュも固まりもしない)。
    std::atomic<int> calls{0};
    {
        MeshLoader loader([&](const std::filesystem::path&, std::string*, ReadBudget&) -> std::optional<MeshData> {
            ++calls;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return std::nullopt;
        });
        for (int i = 0; i < 5; ++i) loader.Request("x.obj");
        while (calls == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(calls >= 1 && calls < 5);
}

// --- Deflate (space3d/runtime/Inflate) と FBX (space3d/runtime/Fbx) ---------------------
// 圧縮したバイトは Python の zlib で作った (レベル 0 と 9、と生のストリーム)。

RM_TEST(Inflate_FixedDynamicStoredAndRaw) {
    const std::string hello = "hello hello hello hello hello";
    const std::vector<uint8_t> fixed{0x78, 0xDA, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x57, 0xC8, 0xC0, 0x4E, 0x02, 0x00, 0xA3, 0x10, 0x0A, 0xE5};
    const auto a = InflateZlib(fixed, 1000);
    CHECK(a && std::string(a->begin(), a->end()) == hello);
    // zlib の包みがない同じもの。
    const std::vector<uint8_t> raw{0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x57, 0xC8, 0xC0, 0x4E, 0x02, 0x00};
    const auto r = Inflate(raw, 1000);
    CHECK(r && std::string(r->begin(), r->end()) == hello);
    // Stored (圧縮しない) ブロック。
    const std::vector<uint8_t> stored{0x78, 0x01, 0x01, 0x07, 0x00, 0xF8, 0xFF, 0x73, 0x74, 0x6F, 0x72, 0x65, 0x64, 0x21, 0x0B, 0xEF, 0x02, 0xB3};
    const auto s = InflateZlib(stored, 1000);
    CHECK(s && std::string(s->begin(), s->end()) == "stored!");
    // 自前のハフマン符号 (動的ブロック)。
    const std::string text = "aaabaacbaabadbaaaaaaaabaacbaabbbabbaaaabcaaaabababcbaabbbaaabbababaabbacaabdaaaacabcaababadabaabaaba";
    const std::vector<uint8_t> dynamic{0x78, 0xDA, 0x35, 0x8A, 0x89, 0x0D, 0x00, 0x00, 0x0C, 0x01, 0x67, 0xA5, 0xF6, 0x9F, 0xA1, 0x68, 0xDA,
                                       0x87, 0xE4, 0x00, 0x40, 0x60, 0x18, 0x93, 0xF5, 0xE6, 0x11, 0xE9, 0x2F, 0x98, 0xC3, 0xDE, 0x0F, 0xA2,
                                       0x25, 0x71, 0xC7, 0x54, 0x2A, 0xD3, 0x6A, 0xB0, 0x1A, 0xE5, 0x16, 0x84, 0x87, 0x26, 0x1C};
    CHECK((dynamic[2] >> 1 & 3) == 2);
    const auto d = InflateZlib(dynamic, 1000);
    CHECK(d && std::string(d->begin(), d->end()) == text);
    // 壊れたもの: チェックサムが違う、途中で切れた、許す量より多い、zlib ではない。
    std::vector<uint8_t> wrong = fixed;
    wrong.back() ^= 1;
    CHECK(!InflateZlib(wrong, 1000));
    CHECK(!InflateZlib(std::span(fixed).first(9), 1000));
    CHECK(!InflateZlib(fixed, 10));
    CHECK(!InflateZlib(std::vector<uint8_t>{0x12, 0x34, 0, 0, 0, 0, 0}, 1000));
}

namespace {

// 小さなバイナリ FBX の書き込み (7400: 32 ビットのオフセット)。テスト専用。
struct TNode {
    std::string name;
    std::vector<std::vector<uint8_t>> props;
    std::vector<TNode> kids;
};

template <class T>
std::vector<uint8_t> Prop(char type, T v) {
    std::vector<uint8_t> b{static_cast<uint8_t>(type)};
    PutBytes<T>(b, {v});
    return b;
}
std::vector<uint8_t> PropS(std::string_view s) {
    std::vector<uint8_t> b{'S'};
    PutBytes<uint32_t>(b, {static_cast<uint32_t>(s.size())});
    b.insert(b.end(), s.begin(), s.end());
    return b;
}
std::vector<uint8_t> PropD(std::initializer_list<double> v) {
    std::vector<uint8_t> b{'d'};
    PutBytes<uint32_t>(b, {static_cast<uint32_t>(v.size()), 0u, static_cast<uint32_t>(v.size() * 8)});
    PutBytes<double>(b, v);
    return b;
}
std::vector<uint8_t> PropI(std::initializer_list<int32_t> v) {
    std::vector<uint8_t> b{'i'};
    PutBytes<uint32_t>(b, {static_cast<uint32_t>(v.size()), 0u, static_cast<uint32_t>(v.size() * 4)});
    PutBytes<int32_t>(b, v);
    return b;
}
// バイナリ FBX の名前: "Name\0\1Class"。
std::string FbxName(std::string_view name, std::string_view cls) { return std::string(name) + std::string("\0\1", 2) + std::string(cls); }
// "P": 名前、型、型 2、フラグ、値。
TNode P(std::string_view name, std::initializer_list<double> values) {
    TNode p{"P", {PropS(name), PropS("Vector3D"), PropS(""), PropS("A")}, {}};
    for (const double v : values) p.props.push_back(Prop<double>('D', v));
    return p;
}
TNode PInt(std::string_view name, int32_t v) { return {"P", {PropS(name), PropS("int"), PropS("Integer"), PropS(""), Prop<int32_t>('I', v)}, {}}; }

void WriteNode(std::vector<uint8_t>& out, const TNode& n) {
    const size_t start = out.size();
    PutBytes<uint32_t>(out, {0u, static_cast<uint32_t>(n.props.size()), 0u});
    out.push_back(static_cast<uint8_t>(n.name.size()));
    out.insert(out.end(), n.name.begin(), n.name.end());
    const size_t propsAt = out.size();
    for (const auto& p : n.props) out.insert(out.end(), p.begin(), p.end());
    const uint32_t listLen = static_cast<uint32_t>(out.size() - propsAt);
    std::memcpy(out.data() + start + 8, &listLen, 4);
    for (const TNode& k : n.kids) WriteNode(out, k);
    if (!n.kids.empty()) out.insert(out.end(), 13, 0);
    const uint32_t end = static_cast<uint32_t>(out.size());
    std::memcpy(out.data() + start, &end, 4);
}

std::vector<uint8_t> WriteFbx(const std::vector<TNode>& top, uint32_t version = 7400) {
    std::vector<uint8_t> out;
    const char magic[] = "Kaydara FBX Binary  ";
    out.insert(out.end(), magic, magic + 21);  // '\0' も含める
    out.push_back(0x1A);
    out.push_back(0x00);
    PutBytes<uint32_t>(out, {version});
    for (const TNode& n : top) WriteNode(out, n);
    out.insert(out.end(), 13, 0);
    out.insert(out.end(), 16, 0xFA);  // フッター (読まない)
    return out;
}

// 四角形 (0 1 2 3) と三角形 (1 4 2)。+Z から見て反時計回り。Z 上、センチメートル単位のファイルで
// x 100 (メートル)。モデルは 5 上。四角形は赤のマテリアル、三角形は絵のあるマテリアル。
std::vector<uint8_t> SampleFbx() {
    // PolygonVertexIndex 0,1,2,-4, 1,4,-3 を zlib で圧縮 (FBX SDK が大きな配列を書くとおり)。
    std::vector<uint8_t> pvi{'i'};
    const std::vector<uint8_t> z{0x78, 0xDA, 0x63, 0x60, 0x60, 0x60, 0x60, 0x04, 0x62, 0x26, 0x20, 0xFE, 0xF3, 0xFF,
                                 0xFF, 0x7F, 0x10, 0x9B, 0x05, 0x88, 0xFF, 0x02, 0xD9, 0x00, 0x44, 0x0C, 0x07, 0xFC};
    PutBytes<uint32_t>(pvi, {7u, 1u, static_cast<uint32_t>(z.size())});
    pvi.insert(pvi.end(), z.begin(), z.end());

    TNode settings{"GlobalSettings", {}, {{"Properties70", {}, {PInt("UpAxis", 2), PInt("UpAxisSign", 1), PInt("FrontAxis", 1), PInt("FrontAxisSign", -1),
                                                                PInt("CoordAxis", 0), PInt("CoordAxisSign", 1), P("UnitScaleFactor", {100})}}}};
    TNode geometry{"Geometry", {Prop<int64_t>('L', 100), PropS(FbxName("Cube", "Geometry")), PropS("Mesh")},
                   {{"Vertices", {PropD({0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 2, 1, 0})}, {}},
                    {"PolygonVertexIndex", {pvi}, {}},
                    {"LayerElementNormal", {Prop<int32_t>('I', 0)},
                     {{"MappingInformationType", {PropS("ByVertice")}, {}},
                      {"ReferenceInformationType", {PropS("Direct")}, {}},
                      {"Normals", {PropD({0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1})}, {}}}},
                    {"LayerElementUV", {Prop<int32_t>('I', 0)},
                     {{"MappingInformationType", {PropS("ByPolygonVertex")}, {}},
                      {"ReferenceInformationType", {PropS("IndexToDirect")}, {}},
                      {"UV", {PropD({0, 0, 1, 0, 1, 1, 0, 1})}, {}},
                      {"UVIndex", {PropI({0, 1, 2, 3, 1, 1, 2})}, {}}}},
                    {"LayerElementMaterial", {Prop<int32_t>('I', 0)},
                     {{"MappingInformationType", {PropS("ByPolygon")}, {}},
                      {"ReferenceInformationType", {PropS("IndexToDirect")}, {}},
                      {"Materials", {PropI({0, 1})}, {}}}}}};
    TNode model{"Model", {Prop<int64_t>('L', 200), PropS(FbxName("Cube", "Model")), PropS("Mesh")},
                {{"Properties70", {}, {P("Lcl Translation", {0, 0, 5})}}}};
    TNode red{"Material", {Prop<int64_t>('L', 300), PropS(FbxName("Red", "Material")), PropS("")},
              {{"Properties70", {}, {P("DiffuseColor", {1, 0, 0}), P("DiffuseFactor", {0.5})}}}};
    TNode wood{"Material", {Prop<int64_t>('L', 301), PropS(FbxName("Wood", "Material")), PropS("")}, {}};
    TNode texture{"Texture", {Prop<int64_t>('L', 400), PropS(FbxName("Wood", "Texture")), PropS("")},
                  {{"FileName", {PropS("C:\\Users\\someone\\tex\\wood.png")}, {}}, {"RelativeFilename", {PropS("tex\\wood.png")}, {}}}};
    TNode objects{"Objects", {}, {geometry, model, red, wood, texture}};
    auto oo = [](int64_t child, int64_t parent) { return TNode{"C", {PropS("OO"), Prop<int64_t>('L', child), Prop<int64_t>('L', parent)}, {}}; };
    TNode links{"Connections", {}, {oo(200, 0), oo(100, 200), oo(300, 200), oo(301, 200),
                                    {"C", {PropS("OP"), Prop<int64_t>('L', 400), Prop<int64_t>('L', 301), PropS("DiffuseColor")}, {}}}};
    return WriteFbx({{"FBXHeaderExtension", {}, {{"FBXVersion", {Prop<int32_t>('I', 7400)}, {}}}}, settings, objects, links});
}

}  // namespace

RM_TEST(Fbx_Binary_MeshMaterialsAndAxes) {
    std::string error;
    const std::vector<uint8_t> file = SampleFbx();
    int version = 0;
    const auto nodes = ParseFbxNodes(file, &version, &error);
    CHECK(nodes && version == 7400 && nodes->size() == 4);
    if (nodes) {
        const FbxNode* objects = &(*nodes)[2];
        CHECK(objects->name == "Objects" && objects->children[0].props[1].text == "Geometry::Cube");  // 名前が入れ替わった
        CHECK(objects->children[0].Child("PolygonVertexIndex")->props[0].numbers == std::vector<double>({0, 1, 2, -4, 1, 4, -3}));
    }
    const auto m = ParseMesh(".FBX", file, nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->indices.size() == 9 && m->vertices.size() == 7);  // 四角形 (三角形 2 つ) と三角形
    // ファイルの Z 上 -> ここでは Y 上。ファイルの Z に沿ったモデルの 5 は 5 上。
    CHECK(Near(m->vertices[2].position, {1, 5, -1}));
    CHECK(Near(m->vertices[0].normal, {0, 1, 0}));
    CHECK(Near(m->min, {0, 5, -1}) && Near(m->max, {2, 5, 0}));
    for (size_t t = 0; t < m->indices.size(); t += 3) {
        const Vec3 a = m->vertices[m->indices[t]].position, b = m->vertices[m->indices[t + 1]].position, c = m->vertices[m->indices[t + 2]].position;
        CHECK(Dot(Cross(b - a, c - a), {0, 1, 0}) > 0);  // 手前は手前のまま
    }
    CHECK(NearF(m->vertices[0].uv.x, 0) && NearF(m->vertices[0].uv.y, 1));  // V は上下が逆
    // マテリアル: 四角形は赤 x 0.5、三角形はモデルの隣の木の絵。
    CHECK(m->materials.size() == 2 && m->parts.size() == 2);
    CHECK(m->materials[0].name == "Red" && NearF(m->materials[0].baseColor.r, 0.5f) && NearF(m->materials[0].baseColor.g, 0));
    CHECK(m->materials[1].image == 0 && m->images.size() == 1 && m->images[0].file == "tex/wood.png");
    CHECK(m->parts[0].indexCount == 6 && m->parts[1].indexCount == 3 && m->parts[1].material == 1);
}

RM_TEST(Fbx_Ascii) {
    // 三角形、Y 上 (GlobalSettings なし: 1 単位 = 1 cm -> 0.01 m)。配列は 2 行にまたがる。
    const std::string text = "; FBX 7.4.0 project file\n"
                             "FBXHeaderExtension:  {\n\tFBXVersion: 7400\n}\n"
                             "Objects:  {\n"
                             "\tGeometry: 1001, \"Geometry::Tri\", \"Mesh\" {\n"
                             "\t\tVertices: *9 {\n\t\t\ta: 0,0,0,100,0,0,\n0,100,0\n\t\t} \n"
                             "\t\tPolygonVertexIndex: *3 {\n\t\t\ta: 0,1,-3\n\t\t} \n"
                             "\t}\n"
                             "\tModel: 1002, \"Model::Tri\", \"Mesh\" {\n\t\tVersion: 232\n\t\tShading: T\n\t}\n"
                             "}\n"
                             "Connections:  {\n\t;Model::Tri, Model::RootNode\n\tC: \"OO\",1002,0\n\tC: \"OO\",1001,1002\n}\n";
    std::string error;
    const auto m = ParseMesh(".fbx", Bytes(text), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->indices.size() == 3 && Near(m->vertices[1].position, {1, 0, 0}));
    CHECK(Near(m->vertices[0].normal, {0, 0, 1}));  // 法線なし: 面から作る
}

RM_TEST(Fbx_BrokenFilesAreToldWhy) {
    std::string error;
    std::vector<uint8_t> file = SampleFbx();
    // ノードの途中で切れた。
    std::vector<uint8_t> cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(file.size() / 2));
    CHECK(!ParseFbx(cut, &error) && !error.empty());
    // 古い FBX (6.1)。
    CHECK(!ParseFbx(WriteFbx({}, 6100), &error) && error.find("6100") != std::string::npos);
    // メッシュなし。FBX ですらない。
    CHECK(!ParseFbx(WriteFbx({{"Objects", {}, {}}}), &error));
    CHECK(!ParseFbx(Bytes(std::string("hello")), &error));
    // 最後の頂点より先を指す面。
    TNode geometry{"Geometry", {Prop<int64_t>('L', 1), PropS("G"), PropS("Mesh")},
                   {{"Vertices", {PropD({0, 0, 0, 1, 0, 0, 0, 1, 0})}, {}}, {"PolygonVertexIndex", {PropI({0, 1, -10})}, {}}}};
    TNode model{"Model", {Prop<int64_t>('L', 2), PropS("M"), PropS("Mesh")}, {}};
    TNode links{"Connections", {}, {{"C", {PropS("OO"), Prop<int64_t>('L', 1), Prop<int64_t>('L', 2)}, {}}}};
    CHECK(!ParseFbx(WriteFbx({{"Objects", {}, {geometry, model}}, links}), &error));
    CHECK(error.find("9") != std::string::npos);
}

RM_TEST(Lighting_Ambient) {
    // 初期値は、設定できるようになる前の見た目: 上から 0.16、下から 0.08 (リニア)。
    const LightSetup d = MakeLights({}, {0, -1, 0});
    CHECK(NearF(d.block.ambientSky[0], 0.16f) && NearF(d.block.ambientSky[2], 0.16f) && NearF(d.block.ambientGround[1], 0.08f));
    Ambient a;
    a.sky = {0.2f, 0.4f, 0.8f, 1};
    a.ground = {0.1f, 0, 0, 1};
    a.intensity = 2;
    const LightSetup s = MakeLights({}, {0, -1, 0}, a);
    CHECK(NearF(s.block.ambientSky[0], 0.4f) && NearF(s.block.ambientSky[1], 0.8f) && NearF(s.block.ambientSky[2], 1.6f));
    CHECK(NearF(s.block.ambientGround[0], 0.2f) && NearF(s.block.ambientGround[1], 0));
    a.intensity = -1;  // 負の光はない
    CHECK(NearF(MakeLights({}, {0, -1, 0}, a).block.ambientSky[0], 0));
}

RM_TEST(Lighting_PointAndSpotShadowMaps) {
    std::vector<Light> lights(5);
    lights[0].type = LightType::Point;  // マップ 0 .. 5
    lights[0].position = {1, 2, 3};
    lights[1].type = LightType::Spot;   // マップ 6
    lights[2].type = LightType::Point;  // あと 6 つは入らない: 影なし
    lights[3].type = LightType::Spot;   // マップ 7
    lights[4].type = LightType::Spot;   // もう残っていない
    lights[4].castShadow = true;
    const LightSetup s = MakeLights(lights, {0, -1, 0});
    CHECK(s.localSlices == kLocalShadowSlices);
    CHECK(NearF(s.block.shadow[0][0], 0) && NearF(s.block.shadow[1][0], 6) && NearF(s.block.shadow[2][0], -1) &&
          NearF(s.block.shadow[3][0], 7) && NearF(s.block.shadow[4][0], -1));
    CHECK(s.shadowsLeftOut == 2);
    CHECK(s.block.shadow[0][1] > 0 && s.block.shadow[0][2] > 0);  // 法線のオフセットと深度のバイアス
    // 点光源のマップは、その PointShadowMatrices (順に)。
    const auto sides = PointShadowMatrices({1, 2, 3}, 10);
    CHECK(std::memcmp(&s.localViewProj[3], &sides[3], sizeof(Mat4)) == 0);
    // 影を落とさない光は、マップを使わない。
    std::vector<Light> quiet(1);
    quiet[0].type = LightType::Spot;
    quiet[0].castShadow = false;
    const LightSetup q = MakeLights(quiet, {0, -1, 0});
    CHECK(q.localSlices == 0 && NearF(q.block.shadow[0][0], -1) && q.shadowsLeftOut == 0);
}

RM_TEST(Lighting_PointShadowSides) {
    // 各面はその軸に沿って見る (+X -X +Y -Y +Z -Z): その軸上で 3 離れた点は、真ん中に来る。
    const Vec3 at{1, 2, 3};
    const auto sides = PointShadowMatrices(at, 10);
    const Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t i = 0; i < 6; ++i) {
        const auto p = ProjectToNdc(sides[i], at + axes[i] * 3.f);
        CHECK(p && NearF(p->x, 0) && NearF(p->y, 0) && p->z > 0 && p->z < 1);
        // 軸から少し (45 度) ずれた点も、まだ中にある: 面はつながっている。
        const Vec3 other = axes[(i + 2) % 6];
        const auto q = ProjectToNdc(sides[i], at + (axes[i] + other) * 2.f);
        CHECK(q && std::abs(q->x) <= 1 && std::abs(q->y) <= 1);
    }
}

RM_TEST(Lighting_SpotShadowMatrix) {
    // (0, 5, 0) から真下へ、60 度の円錐で届く距離 10: 円錐の真ん中はマップの真ん中。
    // その縁 (30 度ずれ) はマップの中。
    const Mat4 m = SpotShadowMatrix({0, 5, 0}, {0, -1, 0}, 60, 10);
    const auto mid = ProjectToNdc(m, {0, 0, 0});
    CHECK(mid && NearF(mid->x, 0) && NearF(mid->y, 0) && mid->z > 0 && mid->z < 1);
    const float edge = 5.f * std::tan(30.f * 3.14159265f / 180.f);
    const auto e = ProjectToNdc(m, {edge, 0, 0});
    CHECK(e && std::abs(e->x) < 1 && std::abs(e->x) > 0.5f);
    // 届かない先: 深さ 1 を越える。
    const auto far = ProjectToNdc(m, {0, -6, 0});
    CHECK(far && far->z > 1);
}

RM_TEST(Lighting_CascadeSplits) {
    const auto s = CascadeSplits(0.1f, 100);
    CHECK(s[0] > 0.1f && s[0] < s[1] && s[1] < s[2] && NearF(s[2], 100));
    // 目の近くは、等間隔のときより密になる。
    CHECK(s[0] < 100.f / kCascades);
}

RM_TEST(Lighting_SunShadowPlan) {
    const Mat4 view = LookAt({0, 2, 10}, {0, 0, 0}, {0, 1, 0});
    const Mat4 projection = Perspective(60, 16.f / 9.f, 0.1f, 1000);
    const Vec3 dir{-0.45f, -1.f, -0.3f};
    // 小さなシーン: 影を落とすものに合わせた 1 枚のマップ (カスケードの前のとおり)。
    const Bounds small{{-2, 0, -2}, {2, 3, 2}};
    {
        // 雨・雪・濡れ が降る向きのマップ。真下: 目の下の場所が真ん中、高い物ほど 0 に近く
        // (先に当たる)、深さはワールド単位 / 範囲。
        const Vec3 eye{100.03f, 5, -40.02f};
        const CoverPlan cover = PlanCover(eye, {0, -1, 0});
        const Vec3 under = TransformPoint(cover.viewProj, {eye.x, 0, eye.z});
        CHECK(std::fabs(under.x) < 0.01f && std::fabs(under.y) < 0.01f);
        const float high = TransformPoint(cover.viewProj, {eye.x, eye.y + kCoverAbove - 4.f, eye.z}).z;
        CHECK(high >= 0.f && high < 0.05f);  // (目の上の kCoverAbove から、4 刻み)
        CHECK(TransformPoint(cover.viewProj, {eye.x, 3, eye.z}).z < TransformPoint(cover.viewProj, {eye.x, 1, eye.z}).z);
        CHECK(std::fabs((TransformPoint(cover.viewProj, {eye.x, -5, eye.z}).z - TransformPoint(cover.viewProj, {eye.x, 5, eye.z}).z) * cover.range - 10.f) < 1e-3f);
        // 縁: 辺の半分だけ離れる。
        CHECK(std::fabs(std::fabs(TransformPoint(cover.viewProj, {eye.x + kCoverSide * 0.5f, 0, eye.z}).x) - 1.f) < 0.01f);
        // 風で斜めに: 向きに沿った点は、マップの同じ場所に落ち、その距離だけ深くなる。
        const Vec3 way = Normalize(Vec3{1, -2, 0.5f});
        const CoverPlan slant = PlanCover(eye, way);
        const Vec3 p0 = TransformPoint(slant.viewProj, {eye.x, 2, eye.z}), p1 = TransformPoint(slant.viewProj, Vec3{eye.x, 2, eye.z} + way * 6.f);
        CHECK(std::fabs(p0.x - p1.x) < 1e-4f && std::fabs(p0.y - p1.y) < 1e-4f && std::fabs((p1.z - p0.z) * slant.range - 6.f) < 1e-3f);
        // テクセルより小さい動きでは、マップは元の場所のまま。
        const CoverPlan same = PlanCover({eye.x + 0.001f, eye.y + 0.5f, eye.z}, {0, -1, 0});
        CHECK(std::memcmp(&same.viewProj, &cover.viewProj, sizeof(Mat4)) == 0);
    }
    const SunShadowPlan one = PlanSunShadows(dir, small, view, projection, 2048);
    CHECK(one.count == 1);
    const Mat4 fitted = DirectionalShadowMatrix(Normalize(dir), small);
    CHECK(std::memcmp(&one.viewProj[0], &fitted, sizeof(Mat4)) == 0);
    CHECK(one.texel[0] > 0 && one.depthRange[0] > 0);
    // 広いシーン: 視線に沿ったカスケード。最後のものは目の前方 kShadowDistance で終わる。
    const Bounds wide{{-200, 0, -200}, {200, 5, 200}};
    const SunShadowPlan plan = PlanSunShadows(dir, wide, view, projection, 2048);
    CHECK(plan.count == kCascades);
    CHECK(plan.splitFar[0] < plan.splitFar[1] && plan.splitFar[1] < plan.splitFar[2] && NearF(plan.splitFar[2], kShadowDistance));
    CHECK(plan.texel[0] < plan.texel[2]);  // 近いほどくっきり
    // 各カスケードの範囲内で視線上にある点は、そのカスケードのマップの中にある。
    const Vec3 eye{0, 2, 10};
    const Vec3 forward = Normalize(Vec3{0, -2, -10});
    float start = 0.1f;
    for (size_t c = 0; c < static_cast<size_t>(kCascades); ++c) {
        const float mid = (start + plan.splitFar[c]) * 0.5f;
        const auto p = ProjectToNdc(plan.viewProj[c], eye + forward * mid);
        CHECK(p && std::abs(p->x) <= 1 && std::abs(p->y) <= 1 && p->z >= 0 && p->z <= 1);
        start = plan.splitFar[c];
    }
    // テクセル単位で動かす: カメラが少し滑っても、各マップの移動はテクセルの整数個ぶんになる。
    const SunShadowPlan moved = PlanSunShadows(dir, wide, LookAt({0.37f, 2, 10}, {0.37f, 0, 0}, {0, 1, 0}), projection, 2048);
    for (size_t c = 0; c < static_cast<size_t>(kCascades); ++c) {
        const float shift = (moved.viewProj[c].m[3][0] - plan.viewProj[c].m[3][0]) * 2048.f / 2.f;
        CHECK(std::abs(shift - std::round(shift)) < 0.01f);
    }
    // どの向きに回しても、どこから見ても、マップの大きさ (テクセル) は変わらない:
    // カメラを回しても影がちらつかない。
    const Mat4 wideView = Perspective(60.f, 16.f / 9.f, 0.1f, 2000.f);
    const SunShadowPlan first = PlanSunShadows(dir, wide, LookAt({0, 2, 10}, {0, 0, 0}, {0, 1, 0}), wideView, 2048);
    bool sameSize = true;
    for (int i = 0; i < 360; ++i) {
        const float a = static_cast<float>(i) * 0.0174533f, b = std::sin(static_cast<float>(i) * 0.37f) * 0.6f;
        const Vec3 at{std::cos(a) * 37.3f, 3.1f + static_cast<float>(i % 7), std::sin(a) * 41.7f};
        const Vec3 look = at + Vec3{std::cos(a * 3.f) * std::cos(b), std::sin(b), std::sin(a * 3.f) * std::cos(b)};
        const SunShadowPlan turned = PlanSunShadows(dir, wide, LookAt(at, look, {0, 1, 0}), wideView, 2048);
        for (size_t c = 0; c < static_cast<size_t>(kCascades); ++c) {
            sameSize = sameSize && turned.texel[c] == first.texel[c] && turned.splitFar[c] == first.splitFar[c];
        }
    }
    CHECK(sameSize);
}

RM_TEST(Light_Ambient_ToRender) {
    game_x_3d::AmbientLight a;
    const Ambient d = game_x_3d::ToRenderAmbient(a);  // 部品の初期値はレンダラーの初期値
    const Ambient r;
    CHECK(NearF(d.sky.r, r.sky.r) && NearF(d.ground.g, r.ground.g) && NearF(d.intensity, r.intensity));
    a.intensity = 3;
    CHECK(NearF(game_x_3d::ToRenderAmbient(a).intensity, 3));
}

// --- 細工したファイル: クラッシュせず、固まらず、メモリを埋めない (ReadBudget) --------

namespace {

double SecondsSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count(); }

}  // namespace

RM_TEST(ReadBudget_LimitsAndCancel) {
    std::string error;
    ReadBudget b(nullptr, 100, 1000, 3);
    CHECK(b.Take(60, 500, &error) && b.Take(40, 500, &error));
    CHECK(!b.Take(1, 0, &error) && error.find("頂点") != std::string::npos);
    CHECK(!b.Take(0, 1, &error) && error.find("メモリー") != std::string::npos);
    CHECK(!b.Take(0, UINT64_MAX, &error));  // 一周して戻らない
    CHECK(b.Vertices() == 100 && b.Bytes() == 1000);
    CHECK(b.Step(&error) && b.Step(&error) && b.Step(&error) && !b.Step(&error));
    std::atomic<bool> cancel{false};
    ReadBudget c(&cancel);
    CHECK(c.Take(10, 10, &error));
    cancel = true;
    CHECK(!c.Take(0, 0, &error) && !c.Step(&error) && error.find("取り消") != std::string::npos);
}

RM_TEST(Crafted_Gltf_AccessorWithoutDataCannotAskForMillions) {
    // 数バイトに書かれた 5000 万の頂点 (bufferView のないアクセサーは全部 0)。
    const std::string doc = R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
                            R"("accessors":[{"componentType":5126,"count":50000000,"type":"VEC3"}]})";
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    CHECK(!ParseGltf(Bytes(doc), nullptr, &error));
    CHECK(error.find("頂点が多すぎる") != std::string::npos);
    CHECK(SecondsSince(start) < 2);  // 場所を作る前に知らせる
}

RM_TEST(Crafted_Gltf_SameChildTwiceAtEveryLevel) {
    // ノード i がノード i + 1 を 2 回名指しし、40 段下る: 毎回たどると 2^40 回歩く。
    std::string nodes = "{\"mesh\":0,\"children\":[1,1]}";
    for (int i = 1; i < 40; ++i) nodes += ",{\"children\":[" + std::to_string(i + 1) + "," + std::to_string(i + 1) + "]}";
    nodes += ",{}";
    const std::string doc = std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[)") + nodes + "]," +
                            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" + R"("buffers":[{"byteLength":44)" +
                            Embedded(TriangleBin()) + "}]," +
                            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)" +
                            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)" +
                            R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    CHECK(!ParseGltf(Bytes(doc), nullptr, &error));
    CHECK(error.find("ノードやつながりが多すぎる") != std::string::npos);
    CHECK(SecondsSince(start) < 10);
}

RM_TEST(Crafted_Gltf_OneMeshPlacedByManyNodes) {
    // 同じ三角形を 5000 ノードに。頂点 1000 個ぶんの余裕で読む: 理由を添えて止まる。
    std::string list, nodes;
    for (int i = 0; i < 5000; ++i) {
        list += (i ? "," : "") + std::to_string(i);
        nodes += std::string(i ? "," : "") + "{\"mesh\":0}";
    }
    const std::string doc = std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[)") + list + "]}],\"nodes\":[" + nodes + "]," +
                            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)" + R"("buffers":[{"byteLength":44)" +
                            Embedded(TriangleBin()) + "}]," +
                            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)" +
                            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)" +
                            R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    std::string error;
    ReadBudget small(nullptr, 1000);
    CHECK(!ParseGltf(Bytes(doc), nullptr, &error, &small));
    CHECK(error.find("頂点が多すぎる") != std::string::npos);
    ReadBudget enough;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error, &enough);  // ふつうの余裕なら、三角形 5000 個のモデル
    CHECK(m && m->indices.size() == 15000 && enough.Vertices() == 15000);
}

RM_TEST(Crafted_Obj_TooManyVertices) {
    std::string text;
    for (int i = 0; i < 400; ++i) text += "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
    std::string error;
    ReadBudget small(nullptr, 500);
    CHECK(!ParseObj(text, nullptr, &error, &small) && error.find("頂点が多すぎる") != std::string::npos);
    CHECK(ParseObj(text, &error).has_value());
}

RM_TEST(Crafted_Fbx_BadIndicesAreNotRead) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    // UV のインデックスが NaN、巨大、終わりの先、負の四角形。マテリアルの番号は巨大。
    TNode geometry{"Geometry", {Prop<int64_t>('L', 100), PropS(FbxName("Q", "Geometry")), PropS("Mesh")},
                   {{"Vertices", {PropD({0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0})}, {}},
                    {"PolygonVertexIndex", {PropI({0, 1, 2, -4})}, {}},
                    {"LayerElementUV", {Prop<int32_t>('I', 0)},
                     {{"MappingInformationType", {PropS("ByPolygonVertex")}, {}},
                      {"ReferenceInformationType", {PropS("IndexToDirect")}, {}},
                      {"UV", {PropD({0.5, 0.5})}, {}},
                      {"UVIndex", {PropD({nan, 1e300, 4294967296.0 * 4, -1})}, {}}}},
                    {"LayerElementNormal", {Prop<int32_t>('I', 0)},
                     {{"MappingInformationType", {PropS("ByPolygonVertex")}, {}},
                      {"ReferenceInformationType", {PropS("IndexToDirect")}, {}},
                      {"Normals", {PropD({0, 0, 1})}, {}},
                      {"NormalsIndex", {PropD({0, 1e19, nan, 18446744073709551616.0})}, {}}}},
                    {"LayerElementMaterial", {Prop<int32_t>('I', 0)},
                     {{"MappingInformationType", {PropS("AllSame")}, {}}, {"Materials", {PropD({1e30})}, {}}}}}};
    TNode model{"Model", {Prop<int64_t>('L', 200), PropS(FbxName("Q", "Model")), PropS("Mesh")}, {}};
    auto oo = [](int64_t child, int64_t parent) { return TNode{"C", {PropS("OO"), Prop<int64_t>('L', child), Prop<int64_t>('L', parent)}, {}}; };
    std::string error;
    const auto m = ParseFbx(WriteFbx({{"Objects", {}, {geometry, model}}, {"Connections", {}, {oo(200, 0), oo(100, 200)}}}), &error);
    CHECK(m.has_value());
    if (m) {
        for (const MeshVertex& v : m->vertices) CHECK(NearF(v.uv.x, 0) && NearF(v.uv.y, 1));  // 何も指さない: 初期値
        CHECK(m->parts.size() == 1 && m->parts[0].material == -1);
    }
    // 頂点が持てる整数ではない角の番号。
    TNode bad = geometry;
    bad.kids[1] = {"PolygonVertexIndex", {PropD({0, nan, 1e300, -4})}, {}};
    CHECK(!ParseFbx(WriteFbx({{"Objects", {}, {bad, model}}, {"Connections", {}, {oo(200, 0), oo(100, 200)}}}), &error));
}

RM_TEST(Crafted_Fbx_SameLinkManyTimes) {
    // ジオメトリを 200 回モデルにつなぐ: 1 回だけ入れる。
    std::vector<uint8_t> once = SampleFbx();
    const auto one = ParseFbx(once, nullptr);
    CHECK(one.has_value());
    TNode geometry{"Geometry", {Prop<int64_t>('L', 100), PropS(FbxName("T", "Geometry")), PropS("Mesh")},
                   {{"Vertices", {PropD({0, 0, 0, 1, 0, 0, 0, 1, 0})}, {}}, {"PolygonVertexIndex", {PropI({0, 1, -3})}, {}}}};
    TNode model{"Model", {Prop<int64_t>('L', 200), PropS(FbxName("T", "Model")), PropS("Mesh")}, {}};
    TNode links{"Connections", {}, {}};
    for (int i = 0; i < 200; ++i) links.kids.push_back({"C", {PropS("OO"), Prop<int64_t>('L', 100), Prop<int64_t>('L', 200)}, {}});
    std::string error;
    const auto m = ParseFbx(WriteFbx({{"Objects", {}, {geometry, model}}, links}), &error);
    CHECK(m && m->indices.size() == 3);
}

RM_TEST(Crafted_Fbx_ArraysTooBig) {
    // 倍精度 2 億個 (1.6 GB 保持) があると書く配列を、数バイトの圧縮で。
    std::vector<uint8_t> arr{'d'};
    PutBytes<uint32_t>(arr, {200'000'000u, 1u, 8u});
    PutBytes<uint64_t>(arr, {0});
    TNode geometry{"Geometry", {Prop<int64_t>('L', 1), PropS("G"), PropS("Mesh")}, {{"Vertices", {arr}, {}}}};
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    CHECK(!ParseFbx(WriteFbx({{"Objects", {}, {geometry}}}), &error) && error.find("大きすぎ") != std::string::npos);
    // 4 バイトの数は、変換後の倍精度として数える: int 1.5 億個は 1.2 GB 保持。
    std::vector<uint8_t> ints{'i'};
    PutBytes<uint32_t>(ints, {150'000'000u, 1u, 8u});
    PutBytes<uint64_t>(ints, {0});
    TNode g2{"Geometry", {Prop<int64_t>('L', 1), PropS("G"), PropS("Mesh")}, {{"PolygonVertexIndex", {ints}, {}}}};
    CHECK(!ParseFbx(WriteFbx({{"Objects", {}, {g2}}}), &error) && error.find("大きすぎ") != std::string::npos);
    // ファイル全体で合計する: モデル自身には収まるが、与えられた余裕には収まらない。
    ReadBudget small(nullptr, kMaxModelVertices, 1000);
    CHECK(!ParseFbx(SampleFbx(), &error, &small) && error.find("メモリー") != std::string::npos);
    CHECK(SecondsSince(start) < 5);
}

RM_TEST(MeshLoader_WhatTheReaderThrowsIsAnError) {
    MeshLoader loader([](const std::filesystem::path& file, std::string*, ReadBudget&) -> std::optional<MeshData> {
        if (file == "memory.glb") throw std::bad_alloc();
        throw std::runtime_error("boom");
    });
    const auto a = loader.Request("memory.glb");
    const auto b = loader.Request("other.glb");
    const auto ra = WaitFor(loader, a), rb = WaitFor(loader, b);
    CHECK(ra && !ra->mesh && ra->error.find("メモリー") != std::string::npos);
    CHECK(rb && !rb->mesh && rb->error.find("boom") != std::string::npos);
}

RM_TEST(MeshLoader_CancelStopsAReadingInTheMiddle) {
    std::atomic<int> started{0};
    auto endless = [&](const std::filesystem::path& file, std::string* error, ReadBudget& budget) -> std::optional<MeshData> {
        if (file == "quick.obj") return ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", error);
        ++started;
        for (;;) {  // 終わらない読み込み。読む側と同じように予算を聞く
            if (!budget.Take(0, 0, error)) return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    {
        MeshLoader loader(endless);
        const auto big = loader.Request("big.glb");
        while (started == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        loader.Cancel(big);
        const auto quick = loader.Request("quick.obj");
        CHECK(WaitFor(loader, quick).has_value());  // 終わらない方は止まったので、これは読めた
        // 読んでいる最中に閉じる: それも止まる (そうしないとテストが固まる)。
        loader.Request("big2.glb");
        while (started < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(started == 2);
}

// --- 大きいが正直なファイル、小さな物、よそのメッシュ ------------------------------------

RM_TEST(Fbx_ManyLinksStayQuick) {
    // 1 つのルートの下に 3 万 3 千のモデル。どれも同じジオメトリとマテリアル: リンクは約 10 万。
    // ジオメトリごとにリンクを全部見ると何分もかかる。インデックスがあれば一瞬。
    constexpr int kModels = 33'000;
    std::vector<TNode> objects;
    objects.push_back({"Geometry", {Prop<int64_t>('L', 1), PropS(FbxName("T", "Geometry")), PropS("Mesh")},
                       {{"Vertices", {PropD({0, 0, 0, 1, 0, 0, 0, 1, 0})}, {}}, {"PolygonVertexIndex", {PropI({0, 1, -3})}, {}}}});
    objects.push_back({"Material", {Prop<int64_t>('L', 2), PropS(FbxName("M", "Material")), PropS("")}, {}});
    objects.push_back({"Model", {Prop<int64_t>('L', 10), PropS(FbxName("Root", "Model")), PropS("Null")}, {}});
    TNode links{"Connections", {}, {{"C", {PropS("OO"), Prop<int64_t>('L', 10), Prop<int64_t>('L', 0)}, {}}}};
    for (int i = 0; i < kModels; ++i) {
        const int64_t id = 100 + i;
        objects.push_back({"Model", {Prop<int64_t>('L', id), PropS(FbxName("T", "Model")), PropS("Mesh")}, {}});
        links.kids.push_back({"C", {PropS("OO"), Prop<int64_t>('L', id), Prop<int64_t>('L', 10)}, {}});
        links.kids.push_back({"C", {PropS("OO"), Prop<int64_t>('L', 1), Prop<int64_t>('L', id)}, {}});
        links.kids.push_back({"C", {PropS("OO"), Prop<int64_t>('L', 2), Prop<int64_t>('L', id)}, {}});
    }
    const std::vector<uint8_t> file = WriteFbx({{"Objects", {}, objects}, links});
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    const auto m = ParseFbx(file, &error);
    CHECK(m && m->indices.size() == size_t{kModels} * 3 && m->materials.size() == 1);
    CHECK(SecondsSince(start) < 10);
}

RM_TEST(Space3d_Inverse_TinyThingsStillInvert) {
    for (const float s : {1e-4f, 1e-6f, 1e-8f}) {
        // 原点で: そこから遠いと、float 自身が 1e-6 の刻みを持てない (100 + 1e-6 は 100)。
        Transform t;
        t.rotation = {10, 20, 30};
        t.scale = {s, s, s};
        const Mat4 m = LocalMatrix(t);
        const auto inv = Inverse(m);
        CHECK(inv.has_value());
        if (!inv) continue;
        const Vec3 p{0.5f, -0.25f, 0.125f};  // その物自身の空間で
        const Vec3 back = TransformPoint(*inv, TransformPoint(m, p));
        CHECK(Length(back - p) < 1e-3f);
        // クリックが当たり、F で枠に収まる (境界を置いたもの)。
        const Ray ray{{0, 0, 50}, {0, 0, -1}};
        CHECK(IntersectPlacedCube(ray, m).has_value());
    }
    // 不均一だが平らではない: 細長い棒。
    Transform rod;
    rod.scale = {1, 1e-4f, 1e-4f};
    CHECK(Inverse(LocalMatrix(rod)).has_value());
    // 平ら (スケール 0) とほぼ平ら (2 つの列がほぼ平行): 逆行列を作れない。
    Transform flat;
    flat.scale = {1, 0, 1};
    CHECK(!Inverse(LocalMatrix(flat)).has_value());
    Mat4 sheared;
    sheared.m[1][0] = 1, sheared.m[1][1] = 1e-9f;  // Y の列 = (1, 1e-9, 0): X の列とほぼ同じ
    CHECK(!Inverse(sheared).has_value());
    Mat4 broken;
    broken.m[0][0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!Inverse(broken).has_value());
}

RM_TEST(Space3d_FromWorld_UnderATinyParent) {
    // 1e-5 にスケールされた親の下の子: ワールド位置を入力しても、そこに置ける。
    Transform parent;
    parent.scale = {1e-5f, 1e-5f, 1e-5f};
    const Mat4 pw = LocalMatrix(parent);
    Transform child;
    WorldPlacement want = Decompose(WorldMatrix(pw, child));
    want.position = {2e-5f, 0, 0};
    const Transform t = FromWorld(child, pw, want);
    CHECK(std::abs(t.position.x - 2.f) < 1e-3f);
}

RM_TEST(Gizmo_ScaleOneAxisNeverReachesZero) {
    using gizmo::Handle;
    using gizmo::Tool;
    const OrbitView v = FrontView();
    const gizmo::Screen s = ScreenOf(v);
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, Tool::Scale, gizmo::Space::World, v, 600);
    const gizmo::Snap smooth;
    const auto x = gizmo::BeginDrag(Tool::Scale, Handle::X, f, s, v.Eye(), {490, 300});
    CHECK(x.has_value());
    if (!x) return;
    // 真ん中を通って先までドラッグ: 係数は、均等な箱と同じく最小で止まる。
    for (const float to : {400.f, 300.f, 100.f}) {
        const auto c = gizmo::Update(*x, s, {to, 300}, smooth);
        CHECK(c && c->factor >= gizmo::kMinScaleFactor && c->axis == 0);
    }
    const auto all = gizmo::BeginDrag(Tool::Scale, Handle::Uniform, f, s, v.Eye(), {400, 300});
    const auto c = gizmo::Update(*all, s, {100, 300}, smooth);
    CHECK(c && c->factor == gizmo::kMinScaleFactor);
}

RM_TEST(Mesh_IndicesInRange) {
    MeshData m;
    m.vertices.resize(3);
    m.indices = {0, 1, 2};
    std::string error;
    CHECK(IndicesInRange(m, &error));
    m.indices = {0, 1, 3};
    CHECK(!IndicesInRange(m, &error) && error.find("超えて") != std::string::npos);
    m.indices = {0, 1, 0xFFFFFFFFu};
    CHECK(!IndicesInRange(m, &error));
}

RM_TEST(Lighting_ChoosesNearAndBright) {
    // X に沿って並ぶ点光源 12 個 (範囲 2)、目は x = 0: 近い 8 個を、近い順に描く。
    std::vector<Light> row(12);
    for (size_t i = 0; i < row.size(); ++i) {
        row[i].type = LightType::Point;
        row[i].range = 2;
        row[i].position = {static_cast<float>(11 - i) * 5.f, 0, 0};  // 遠い順に並べた
    }
    const LightSetup s = MakeLights(row, {0, -1, 0}, {}, {0, 0, 0});
    CHECK(s.dropped == 4);
    for (size_t i = 0; i < kMaxLights; ++i) CHECK(NearF(s.block.position[i][0], static_cast<float>(i) * 5.f));
    // 目を動かすと、選ばれるものが変わる。
    const LightSetup far = MakeLights(row, {0, -1, 0}, {}, {55, 0, 0});
    CHECK(NearF(far.block.position[0][0], 55));
    // 少し遠い明るい光は、近い暗い光より先に来る。
    std::vector<Light> two(2);
    two[0].type = two[1].type = LightType::Point;
    two[0].range = two[1].range = 1;
    two[0].position = {3, 0, 0};
    two[0].intensity = 0.1f;
    two[1].position = {4, 0, 0};
    two[1].intensity = 10;
    CHECK(LightWeight(two[1], {}) > LightWeight(two[0], {}));
    CHECK(NearF(MakeLights(two, {0, -1, 0}, {}, {0, 0, 0}).block.position[0][0], 4));
    // 光の届く範囲の中では、距離で弱くならない。
    CHECK(NearF(LightWeight(two[0], {3, 0, 0}), LightWeight(two[0], {3.5f, 0, 0})));
    // 一番近い点光源が影のマップ (6 枚) を使う: 影を持てる点光源は 1 つだけ。
    std::vector<Light> points(3);
    for (size_t i = 0; i < points.size(); ++i) {
        points[i].type = LightType::Point;
        points[i].position = {static_cast<float>(2 - i) * 30.f, 0, 0};
    }
    const LightSetup sp = MakeLights(points, {0, -1, 0}, {}, {0, 0, 0});
    CHECK(NearF(sp.block.position[0][0], 0) && NearF(sp.block.shadow[0][0], 0) && NearF(sp.block.shadow[1][0], -1) && sp.shadowsLeftOut == 2);
}

// --- 描く順 (インスタンシング) と、多くの箱を 1 つのビューに対して -----------------------

RM_TEST(DrawOrder_SortsShaderMaterialMesh) {
    // ばらばらに与える: シェーダー 1 を最後に、次に絵ごと、次にメッシュごと。同じキーは順を保つ。
    const std::vector<DrawKey> keys{
        {0, 2, 10, 0},  // 0
        {1, 0, 5, 0},   // 1
        {0, 0, 10, 0},  // 2
        {0, 2, 7, 0},   // 3
        {0, 0, 10, 0},  // 4 (2 と同じ)
        {0, 0, 10, 1},  // 5 (メッシュ 10 の別のパーツ)
        {0, 2, 10, 0},  // 6 (0 と同じ)
    };
    std::vector<uint32_t> order;
    SortDraws(keys, order);
    CHECK(order == std::vector<uint32_t>({2, 4, 5, 3, 0, 6, 1}));
    CHECK(CountRuns(keys, order) == 5);  // {2,4} {5} {3} {0,6} {1}
    // すでに順になっている: そのまま。描くものがない: 何もしない。
    const std::vector<DrawKey> sorted{{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 2, 0}};
    SortDraws(sorted, order);
    CHECK(order == std::vector<uint32_t>({0, 1, 2}) && CountRuns(sorted, order) == 2);
    SortDraws({}, order);
    CHECK(order.empty() && CountRuns({}, order) == 0);
}

RM_TEST(DrawOrder_ThousandOfOneMeshIsOneDraw) {
    // 1 つのメッシュの物 1000 個で絵なし。間に別のメッシュ 3 個が並ぶ: 描画は 4 回。
    std::vector<DrawKey> keys;
    for (uint32_t i = 0; i < 1000; ++i) keys.push_back({0, 0, i % 250 == 0 ? 100u + i / 250 : 1u, 0});
    std::vector<uint32_t> order;
    SortDraws(keys, order);
    CHECK(CountRuns(keys, order) == 5);  // メッシュ 1 と、メッシュ 100, 101, 102, 103
    // 安定: メッシュ 1 の物は、与えられた順を保つ。
    bool increasing = true;
    for (size_t i = 1; i < order.size(); ++i) {
        if (keys[order[i]].mesh == 1 && keys[order[i - 1]].mesh == 1 && order[i] < order[i - 1]) increasing = false;
    }
    CHECK(increasing);
}

RM_TEST(Frustum_SameAnswerAsBoxInView) {
    // カメラのまわりの多くの箱と、光の正射影ビュー: 平面のテストは、どこでも角のテストと一致する。
    const Mat4 cameras[] = {Perspective(60, 16.f / 9.f, 0.1f, 100) * LookAt({3, 5, 12}, {0, 0, 0}, {0, 1, 0}),
                            Orthographic(-10, 10, -6, 6, 1, 40) * LookAt({5, 20, 5}, {0, 0, 0}, {0, 0, 1})};
    int seen = 0, agreed = 0, total = 0;
    uint32_t seed = 12345;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);  // 0 .. 1
    };
    for (const Mat4& vp : cameras) {
        const Frustum f = FrustumOf(vp);
        for (int i = 0; i < 4000; ++i) {
            const Vec3 c{rnd() * 80 - 40, rnd() * 80 - 40, rnd() * 80 - 40};
            const Vec3 h{rnd() * 3 + 0.01f, rnd() * 3 + 0.01f, rnd() * 3 + 0.01f};
            const Bounds b{c - h, c + h};
            const bool a = BoxInView(vp, b);
            if (a == BoxInFrustum(f, b)) ++agreed;
            if (a) ++seen;
            ++total;
        }
    }
    CHECK(agreed == total);
    CHECK(seen > 100 && seen < total - 100);  // 両方の答えを聞いた
}

RM_TEST(SunFitBounds_MovingThingsWidenInSteps) {
    const Bounds still{{-15, 0, -15}, {15, 3, 15}};  // 大きさ約 42.5: 4 刻み (42.5 / 16 = 2.7 -> 4)
    // 何も動かない、または何も残らない: そのまま / 何もなし。
    CHECK(!SunFitBounds(std::nullopt, std::nullopt).has_value());
    const auto same = SunFitBounds(still, std::nullopt);
    CHECK(same && same->min == still.min && same->max == still.max);
    // 残るものの内側で動く: 何も動かないのとまったく同じ (マップも層も残る)。
    const auto inside = SunFitBounds(still, Bounds{{-3, 0, -3}, {-2, 1, -2}});
    CHECK(inside && inside->min == still.min && inside->max == still.max);
    // 左側の先までドラッグ: 1 刻みぶん広げる ...
    const auto out = SunFitBounds(still, Bounds{{-15.3f, 0, 0}, {-14.3f, 1, 1}});
    CHECK(out && out->min.x == -19 && out->max == still.max && out->min.y == 0 && out->min.z == -15);
    // ... 同じ刻みの中で動き続ける間は同じ箱 (毎フレーム新しい行列にはしない)。
    for (float x = -15.3f; x > -18.9f; x -= 0.1f) {
        const auto again = SunFitBounds(still, Bounds{{x, 0, 0}, {x + 1, 1, 1}});
        CHECK(again && again->min == out->min && again->max == out->max);
    }
    // 刻みを越える: 次の刻みへ。
    const auto further = SunFitBounds(still, Bounds{{-19.5f, 0, 0}, {-18.5f, 1, 1}});
    CHECK(further && further->min.x == -23);
    // 上と右にも (各辺は別々に)。
    const auto up = SunFitBounds(still, Bounds{{0, 0, 0}, {16, 3.5f, 1}});
    CHECK(up && up->max.x == 19 && up->max.y == 7 && up->max.z == 15 && up->min == still.min);
    // 全部が動く: その箱自身を、刻みの整数倍まで広げる (小さな動きなら保つ)。
    const auto all = SunFitBounds(std::nullopt, Bounds{{0.2f, 0, 0.2f}, {1.2f, 1, 1.2f}});
    const auto allMoved = SunFitBounds(std::nullopt, Bounds{{0.22f, 0, 0.22f}, {1.22f, 1, 1.22f}});
    CHECK(all && allMoved && all->min == allMoved->min && all->max == allMoved->max);
    CHECK(all->min.x <= 0.2f && all->max.x >= 1.2f);
}

// 単純な形 (Primitives.h): 大きさ、外を向く法線 (長さ 1)、どの三角形も表が外を向く、
// インデックスは範囲内。MeshRenderer の 形 がそれを指す (モデル: なし)。
RM_TEST(Space3d_Shapes) {
    using rm::space3d::Shape;
    struct Want {
        Shape shape;
        rm::Vec3 min, max;
    };
    const Want wants[] = {
        {Shape::Box, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
        {Shape::Sphere, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
        {Shape::Cylinder, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
        {Shape::Capsule, {-0.5f, -1.f, -0.5f}, {0.5f, 1.f, 0.5f}},
        {Shape::Plane, {-5.f, 0.f, -5.f}, {5.f, 0.f, 5.f}},
    };
    auto near = [](rm::Vec3 a, rm::Vec3 b) { return std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.y - b.y) < 1e-4f && std::fabs(a.z - b.z) < 1e-4f; };
    for (const Want& w : wants) {
        const rm::space3d::MeshData m = rm::space3d::ShapeMesh(w.shape);
        CHECK(!m.vertices.empty() && m.indices.size() >= 6 && m.indices.size() % 3 == 0);
        CHECK(rm::space3d::IndicesInRange(m, nullptr));
        CHECK(near(m.min, w.min) && near(m.max, w.max));
        bool unit = true, out = true, uv = true;
        for (const auto& v : m.vertices) {
            const float len = std::sqrt(v.normal.x * v.normal.x + v.normal.y * v.normal.y + v.normal.z * v.normal.z);
            unit = unit && std::fabs(len - 1.f) < 1e-3f;
            uv = uv && v.uv.x >= -1e-4f && v.uv.x <= 1.0001f && v.uv.y >= -1e-4f && v.uv.y <= 1.0001f;
        }
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const auto& a = m.vertices[m.indices[i]];
            const auto& b = m.vertices[m.indices[i + 1]];
            const auto& c = m.vertices[m.indices[i + 2]];
            const rm::Vec3 e1{b.position.x - a.position.x, b.position.y - a.position.y, b.position.z - a.position.z};
            const rm::Vec3 e2{c.position.x - a.position.x, c.position.y - a.position.y, c.position.z - a.position.z};
            const rm::Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            const rm::Vec3 s{a.normal.x + b.normal.x + c.normal.x, a.normal.y + b.normal.y + c.normal.y, a.normal.z + b.normal.z + c.normal.z};
            // (どの三角形にも面積がある: 極にはない)
            out = out && n.x * s.x + n.y * s.y + n.z * s.z > 0;
        }
        CHECK(unit);
        CHECK(out);
        CHECK(uv);
    }
    // 丸い形は丸い: 球の頂点は全部、真ん中から 0.5。
    bool round = true;
    for (const auto& v : rm::space3d::ShapeMesh(Shape::Sphere).vertices) {
        round = round && std::fabs(std::sqrt(v.position.x * v.position.x + v.position.y * v.position.y + v.position.z * v.position.z) - 0.5f) < 1e-4f;
    }
    CHECK(round);
    CHECK(!rm::game_x_3d::ShapeOf(rm::game_x_3d::MeshShape::Model));
    CHECK(rm::game_x_3d::ShapeOf(rm::game_x_3d::MeshShape::Cylinder) == Shape::Cylinder);
    // 形 が加わる前に書いた メッシュ は、モデルとして読む (描いていたとおり)。
    CHECK(rm::game_x_3d::MeshRenderer{}.shape == rm::game_x_3d::MeshShape::Model);
}
