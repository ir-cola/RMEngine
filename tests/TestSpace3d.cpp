// Tests for the 3D space plugin's math (plugins/rm_engine/space3d/runtime/Transform, View), models
// (Mesh: shapes, materials and parts; MeshFile: which related files may be read) and lights (Lighting),
// and the connector's camera and light components (game_x_3d/runtime).

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_x_3d/editor/SceneViewState.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/MeshLoader.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <atomic>
#include <chrono>
#include <cmath>
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
    CHECK(Near(TransformPoint(LocalMatrix({}), {1, 2, 3}), {1, 2, 3}));  // the defaults change nothing
    Transform t;
    t.position = {1, 2, 3};
    t.rotation = {0, 90, 0};
    t.scale = {2, 2, 2};
    // (1,0,0) -> scaled (2,0,0) -> 90 degrees about Y (0,0,-2) -> moved (1,2,1)
    CHECK(Near(TransformPoint(LocalMatrix(t), {1, 0, 0}), {1, 2, 1}));
    Transform rx;
    rx.rotation = {90, 0, 0};
    CHECK(Near(TransformPoint(LocalMatrix(rx), {0, 1, 0}), {0, 0, 1}));
    Transform rz;
    rz.rotation = {0, 0, 90};
    CHECK(Near(TransformPoint(LocalMatrix(rz), {1, 0, 0}), {0, 1, 0}));
    // X first, then Y: (0,1,0) -X90-> (0,0,1) -Y90-> (1,0,0)
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
    CHECK(Near(TransformPoint(world, {0, 0, 0}), {10, 1, 0}));  // the child's x turns into the parent's y
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
    CHECK(!Inverse(LocalMatrix(flat)).has_value());  // a zero scale cannot be undone
}

RM_TEST(Space3d_Camera_ProjectsThePivotToTheMiddle) {
    OrbitView v;
    v.pivot = {1, 2, 3};
    const Mat4 vp = v.ProjectionMatrix(16.f / 9.f) * v.ViewMatrix();
    float c[4];
    const Vec3 p = v.pivot;
    for (int row = 0; row < 4; ++row) c[row] = vp.m[0][row] * p.x + vp.m[1][row] * p.y + vp.m[2][row] * p.z + vp.m[3][row];
    CHECK(NearF(c[0] / c[3], 0) && NearF(c[1] / c[3], 0));
    CHECK(c[2] / c[3] > 0 && c[2] / c[3] < 1);  // depth inside 0 .. 1
    // The ray through the middle of the screen goes from the eye to the pivot.
    const Ray r = ScreenRay(*Inverse(vp), 0, 0);
    CHECK(Near(r.dir, v.Forward()));
    // Default camera: looking down a little, towards -Z and -X.
    CHECK(v.Forward().y < 0 && v.Forward().z < 0);
    CHECK(NearF(Length(v.Forward()), 1) && NearF(Dot(v.Forward(), v.Right()), 0) && NearF(Dot(v.Up(), v.Forward()), 0));
}

RM_TEST(Space3d_Rays_HitBoxes) {
    const Ray r{{0, 0, 10}, {0, 0, -1}};
    auto t = IntersectBox(r, {-1, -1, -1}, {1, 1, 1});
    CHECK(t && NearF(*t, 9));
    CHECK(!IntersectBox({{5, 0, 10}, {0, 0, -1}}, {-1, -1, -1}, {1, 1, 1}));
    CHECK(!IntersectBox({{0, 0, 10}, {0, 0, 1}}, {-1, -1, -1}, {1, 1, 1}));  // behind
    // A cube moved to x = 4 and made 2 wide: hit at its face, 9 away.
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
    Look(v, 30, -10);  // the head turns: the eye stays
    CHECK(Near(v.Eye(), eye));
    const Vec3 pivot = v.pivot;
    Orbit(v, 45, 20);  // around the pivot: the pivot stays
    CHECK(Near(v.pivot, pivot) && NearF(Length(v.Eye() - v.pivot), v.distance));
    Orbit(v, 0, 500);
    CHECK(v.pitch == 89.f);  // never over the top
    const float d = v.distance;
    Dolly(v, 1);
    CHECK(v.distance < d);
    const Vec3 before = v.pivot;
    Pan(v, 100, 0, 500);  // dragging right moves the camera left
    CHECK(Dot(v.pivot - before, v.Right()) < 0);
    const Vec3 at = v.pivot;
    Fly(v, {0, 0, 2});
    CHECK(Near(v.pivot - at, v.Forward() * 2));
    Frame(v, {7, 0, 0}, 1);
    CHECK(Near(v.pivot, {7, 0, 0}) && v.distance >= 2.5f);
}

// --- Rotation math and the gizmo (space3d/runtime/Transform, space3d/editor/Gizmo) --------------

namespace {

bool NearM(const Mat4& a, const Mat4& b) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a.m[c][r] - b.m[c][r]) > 1e-4f) return false;
        }
    }
    return true;
}

// A camera in front of the origin looking along -Z: X goes right on the screen, Y up.
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
    CHECK(NearM(AxisAngle({0, 0, 5}, 30), RotationMatrix({0, 0, 30})));  // any length of axis
    CHECK(NearM(AxisAngle({0, 0, 0}, 30), Mat4{}));                    // no axis: no turn
    // LocalMatrix is built from the same rotation.
    Transform t;
    t.rotation = {10, 20, 30};
    CHECK(NearM(LocalMatrix(t), RotationMatrix({10, 20, 30})));
}

RM_TEST(Space3d_EulerFromRotation) {
    for (Vec3 a : {Vec3{0, 0, 0}, Vec3{30, -50, 120}, Vec3{-170, 80, 5}, Vec3{45, 0, -90}}) {
        CHECK(Near(EulerFromRotation(RotationMatrix(a), a), a));
        CHECK(NearM(RotationMatrix(EulerFromRotation(RotationMatrix(a))), RotationMatrix(a)));  // from 0: maybe another set, same turn
    }
    // Continuous while dragging: past 180 stays past 180 when the angles shown are near it.
    CHECK(Near(EulerFromRotation(RotationMatrix({0, 181, 0}), {0, 179, 0}), {0, 181, 0}));
    CHECK(Near(EulerFromRotation(RotationMatrix({0, 120, 0}), {0, 119, 0}), {0, 120, 0}));
    CHECK(Near(EulerFromRotation(RotationMatrix({0, 0, 400}), {0, 0, 399}), {0, 0, 400}));
    // Straight up (gimbal lock): the same turn, z kept.
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
    // Nothing typed: nothing changes (to the last bit).
    const Transform same = FromWorld(child, pw, now);
    CHECK(same.position == child.position && same.rotation == child.rotation && same.scale == child.scale);
    // The world position: only the position moves.
    WorldPlacement want = now;
    want.position = {5, 0, -4};  // 2 along the parent's X (turned to -Z), which is 2x as big
    const Transform moved = FromWorld(child, pw, want);
    CHECK(Near(moved.position, {2, 0, 0}) && moved.rotation == child.rotation && moved.scale == child.scale);
    // The world rotation and scale come out as typed.
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
    CHECK(Near(p.position, {0, 0, -2}));  // (1,0,0) scaled 2, turned 90 about Y
    CHECK(Near(p.scale, {2, 2, 2}));
    CHECK(NearM(RotationMatrix(p.rotation), RotationMatrix({0, 90, 0}) * RotationMatrix({0, 0, 30})));
    // A mirror is a negative X scale.
    Transform m;
    m.scale = {-1, 1, 1};
    CHECK(Near(Decompose(LocalMatrix(m)).scale, {-1, 1, 1}));
    CHECK(Near(Decompose(LocalMatrix(m)).rotation, {0, 0, 0}));
}

RM_TEST(Gizmo_LinesPlanesSegments) {
    const Ray down{{0, 5, 0}, {0, -1, 0}};
    CHECK(!gizmo::ClosestOnLine(down, {0, 0, 0}, {0, 1, 0}));  // parallel
    const Ray across{{3, 1, 5}, {0, 0, -1}};
    CHECK(std::abs(*gizmo::ClosestOnLine(across, {0, 0, 0}, {1, 0, 0}) - 3) < 1e-4f);
    CHECK(Near(*gizmo::IntersectPlane(down, {0, 0, 0}, {0, 1, 0}), {0, 0, 0}));
    CHECK(!gizmo::IntersectPlane(down, {0, 10, 0}, {0, 1, 0}));  // behind the ray
    CHECK(!gizmo::IntersectPlane(across, {0, 0, 0}, {1, 0, 0}));  // along the plane
    CHECK(std::abs(gizmo::DistanceToSegment({5, 3}, {0, 0}, {10, 0}) - 3) < 1e-4f);
    CHECK(std::abs(gizmo::DistanceToSegment({-4, 3}, {0, 0}, {10, 0}) - 5) < 1e-4f);
}

RM_TEST(Gizmo_FrameAndScreen) {
    const OrbitView v = FrontView();
    const gizmo::Screen s = ScreenOf(v);
    const auto mid = s.ToScreen({0, 0, 0});
    CHECK(mid && std::abs(mid->x - 400) < 1e-2f && std::abs(mid->y - 300) < 1e-2f);
    CHECK(!s.ToScreen({0, 0, 20}));  // behind the eye
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, gizmo::Tool::Move, gizmo::Space::World, v, 600);
    const auto end = s.ToScreen(gizmo::AxisEnd(f, 0));
    CHECK(end && std::abs(end->x - 490) < 0.5f && std::abs(end->y - 300) < 0.5f);  // 90 pixels to the right
    CHECK(!gizmo::AxisShown(f, s, 2));  // Z points at the eye
    CHECK(gizmo::AxisShown(f, s, 0) && gizmo::AxisShown(f, s, 1));
    CHECK(gizmo::PlaneShown(f, s, gizmo::Handle::XY));  // faces the eye
    CHECK(!gizmo::PlaneShown(f, s, gizmo::Handle::YZ) && !gizmo::PlaneShown(f, s, gizmo::Handle::ZX));  // edge-on
    // Local: a turned thing's own axes (scale is always local).
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
    const auto facing = gizmo::RingArcs(f, 2, v.Eye(), true, 64);  // about Z: faces the eye
    CHECK(facing.size() == 1 && facing[0].size() == 65);
    CHECK(gizmo::RingArcs(f, 2, v.Eye(), false, 64).empty());  // ... so it has no back
    const auto back = gizmo::RingArcs(f, 1, v.Eye(), false, 64);  // about Y: the other half is behind
    size_t backPoints = 0;
    for (const auto& arc : back) {
        backPoints += arc.size();
        for (Vec3 p : arc) CHECK(p.z <= 1e-3f);
    }
    CHECK(back.size() == 1 && backPoints >= 32 && backPoints <= 34);
    const auto edge = gizmo::RingArcs(f, 1, v.Eye(), true, 64);  // about Y: half of it is in front
    size_t points = 0;
    for (const auto& arc : edge) {
        points += arc.size();
        for (Vec3 p : arc) CHECK(p.z >= -1e-3f);  // nothing behind the middle
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
    CHECK(gizmo::HitTest(Tool::Move, f, s, v.Eye(), {401, 240}) == Handle::Y);  // y up = screen up
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
    {  // move along X by 1 world unit
        const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, Tool::Move, gizmo::Space::World, v, 600);
        const auto drag = gizmo::BeginDrag(Tool::Move, Handle::X, f, s, v.Eye(), {490, 300});
        CHECK(drag.has_value());
        const float px = 90.f / f.size;  // pixels per world unit at the origin
        const auto c = gizmo::Update(*drag, s, {490 + px, 300}, smooth);
        CHECK(c && Near(c->move, {1, 0, 0}));
        const auto snapped = gizmo::Update(*drag, s, {490 + px * 0.9f, 300}, steps);
        CHECK(snapped && Near(snapped->move, {1, 0, 0}));  // 0.9 -> 1.0 in steps of 0.25
        // In a plane: both directions.
        const auto plane = gizmo::BeginDrag(Tool::Move, Handle::XY, f, s, v.Eye(), {428, 272});
        CHECK(plane.has_value());
        const auto pc = gizmo::Update(*plane, s, {428 + px, 272 - px}, smooth);
        CHECK(pc && Near(pc->move, {1, 1, 0}));
    }
    {  // rotate about Z by a quarter of the ring's length under the mouse
        const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, Tool::Rotate, gizmo::Space::World, v, 600);
        const float r = 90.f * 0.70710678f;
        const auto drag = gizmo::BeginDrag(Tool::Rotate, Handle::Z, f, s, v.Eye(), {400 + r, 300 - r});
        CHECK(drag.has_value());
        // Forward about Z (counter-clockwise on the screen) at the upper right = up and to the left.
        CHECK(drag->tangent.x < 0 && drag->tangent.y < 0);
        const float quarter = 90.f * 3.14159265f / 2.f;
        const Vec2 to{400 + r + drag->tangent.x * quarter, 300 - r + drag->tangent.y * quarter};
        const auto c = gizmo::Update(*drag, s, to, smooth);
        CHECK(c && std::abs(c->degrees - 90) < 0.5f && c->axis == 2);
        const Transform t = gizmo::Apply({}, Mat4{}, Mat4{}, *c, gizmo::Space::World, {});
        CHECK(NearM(RotationMatrix(t.rotation), AxisAngle({0, 0, 1}, c->degrees)));
        const auto s15 = gizmo::Update(*drag, s, {400 + r + drag->tangent.x * 20, 300 - r + drag->tangent.y * 20}, steps);
        CHECK(s15 && std::abs(s15->degrees - 15) < 1e-3f);  // 12.7 degrees -> 15
    }
    {  // scale: the middle box evenly, an axis alone
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
    // Moving 2 along world X is 1 along the parent's +Z (turned 90 about Y, twice as big).
    gizmo::Change move;
    move.tool = gizmo::Tool::Move;
    move.move = {2, 0, 0};
    CHECK(Near(gizmo::Apply(child, pw, cw, move, gizmo::Space::World, {}).position, {1, 0, 1}));
    // Turning about world X vs. the thing's own X.
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
    CHECK(Near(local.rotation, {90, 90, 0}));  // X first, then Y: just as the inspector would write it
}

RM_TEST(Gizmo_ViewRingAndFreeRotation) {
    const OrbitView v = FrontView();  // the eye on +Z looking at the origin
    const gizmo::Screen s = ScreenOf(v);
    const gizmo::Frame f = *gizmo::MakeFrame(Mat4{}, gizmo::Tool::Rotate, gizmo::Space::World, v, 600);
    using gizmo::Handle;
    using gizmo::Tool;
    const float outer = 90.f * gizmo::kViewRing;
    // The outer ring facing the eye, and inside the ball (away from the rings): turn freely.
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {400 + outer, 300}) == Handle::View);
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {430, 320}) == Handle::Free);
    CHECK(gizmo::HitTest(Tool::Rotate, f, s, v.Eye(), {400 + outer + 30, 300}) == Handle::None);
    const float quarter = 3.14159265f / 2.f;
    gizmo::Snap smooth;
    {  // the outer ring: about the line of sight (+Z here), a quarter of its length = 90 degrees
        const auto drag = gizmo::BeginDrag(Tool::Rotate, Handle::View, f, s, v.Eye(), {400 + outer, 300});
        CHECK(drag.has_value());
        CHECK(std::abs(drag->tangent.x) < 1e-3f && drag->tangent.y < -0.99f);  // forward = up the screen
        const auto c = gizmo::Update(*drag, s, {400 + outer, 300 - outer * quarter}, smooth);
        CHECK(c && std::abs(c->degrees - 90) < 0.5f && c->axis == -1 && Near(c->worldAxis, {0, 0, 1}));
        const Transform t = gizmo::Apply({}, Mat4{}, Mat4{}, *c, gizmo::Space::Local, {});
        CHECK(NearM(RotationMatrix(t.rotation), AxisAngle({0, 0, 1}, c->degrees)));
    }
    {  // inside: dragging right rolls the front to the right = about +Y; the length of a quarter = 90 degrees
        const auto drag = gizmo::BeginDrag(Tool::Rotate, Handle::Free, f, s, v.Eye(), {430, 320});
        CHECK(drag.has_value());
        const auto c = gizmo::Update(*drag, s, {430 + 90.f * quarter, 320}, smooth);
        CHECK(c && std::abs(c->degrees - 90) < 0.5f && Near(c->worldAxis, {0, 1, 0}));
        const auto upward = gizmo::Update(*drag, s, {430, 320 - 90.f * quarter}, smooth);  // up: the front goes up = about -X
        CHECK(upward && std::abs(upward->degrees - 90) < 0.5f && Near(upward->worldAxis, {-1, 0, 0}));
        const auto still = gizmo::Update(*drag, s, {430, 320}, smooth);
        CHECK(still && still->degrees == 0);
    }
}

RM_TEST(Gizmo_PivotCenterMovesThePlaces) {
    // Several things about one pivot (the middle of them): their places turn / scale with them.
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
    CHECK(Near(gizmo::Apply(start, Mat4{}, sw, rot, gizmo::Space::World, {}).position, {1, 0, 0}));  // no pivot: stays
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
    // The gizmo put in the middle of them, with the axes it is given.
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
    CHECK(back.snapScale == 0.1f);  // not changed: the default
    // A value out of range is brought back in; a key of a newer version is written back.
    j.Find("camera")->Set("pitch", 500.0);
    j.Set("future", 1);
    reflect::Unknowns u;
    game_x_3d::SceneViewState clamped;
    reflect::Load(clamped, j, &u);
    CHECK(clamped.camera.pitch == 89);
    j.Set("snapDegrees", 0.0);  // a step of nothing would never move: brought up to the least
    game_x_3d::SceneViewState least;
    reflect::Load(least, j);
    CHECK(std::abs(least.snapDegrees - 0.1f) < 1e-6f);
    CHECK(reflect::Save(clamped, &u).Find("future") != nullptr);
}

// --- Models (space3d/runtime/Mesh) and what the scene view does with their bounds ---------------

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

// One triangle (0,0,0) (1,0,0) (0,1,0), counter-clockwise seen from +Z, with ushort indices: 44 bytes.
std::vector<uint8_t> TriangleBin(uint16_t lastIndex = 2) {
    std::vector<uint8_t> b;
    PutBytes<float>(b, {0, 0, 0, 1, 0, 0, 0, 1, 0});
    PutBytes<uint16_t>(b, {0, 1, lastIndex, 0});
    return b;
}

// The glTF of that triangle; `buffer` = more keys of "buffers":[{...}], `node` = the node's keys.
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
    CHECK(!DecodeBase64("TW*u"));   // not base64
    CHECK(!DecodeBase64("TWFuT"));  // one character alone
    CHECK(DecodePercent("my%20model.bin") == "my model.bin");
    CHECK(DecodePercent("100%") == "100%");  // not a code: left as it is
}

RM_TEST(Mesh_Gltf_EmbeddedTriangleIsPlacedByItsNode) {
    std::string error;
    const std::string doc = TriangleGltf(Embedded(TriangleBin()));
    const auto m = ParseMesh(".gltf", Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->vertices.size() == 3 && m->indices.size() == 3);
    CHECK(Near(m->vertices[1].position, {1, 0, 5}));  // moved by the node
    CHECK(Near(m->vertices[0].normal, {0, 0, 1}));    // no normals in the file: made, facing the front
    CHECK(Near(m->min, {0, 0, 5}) && Near(m->max, {1, 1, 5}));
}

RM_TEST(Mesh_Gltf_MirroredNodeKeepsTheFront) {
    std::string error;
    const auto m = ParseGltf(Bytes(TriangleGltf(Embedded(TriangleBin()), "\"scale\":[-1,1,1]")), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    const Vec3 a = m->vertices[m->indices[0]].position, b = m->vertices[m->indices[1]].position, c = m->vertices[m->indices[2]].position;
    CHECK(Near(m->vertices[1].position, {-1, 0, 0}));
    // Counter-clockwise still turns towards the normal.
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
    std::string json = TriangleGltf("");  // buffer 0 without a uri: the BIN chunk
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
    // Cut short: told, not read past the end.
    glb.resize(glb.size() - 20);
    CHECK(!ParseMesh(".glb", glb, nullptr, &error));
}

RM_TEST(Mesh_Gltf_BrokenFilesAreToldWhy) {
    std::string error;
    // An index past the last vertex.
    CHECK(!ParseGltf(Bytes(TriangleGltf(Embedded(TriangleBin(7)))), nullptr, &error));
    CHECK(error.find("インデックス") != std::string::npos);
    // A buffer shorter than it says.
    std::vector<uint8_t> shortBin = TriangleBin();
    shortBin.resize(20);
    CHECK(!ParseGltf(Bytes(TriangleGltf(Embedded(shortBin))), nullptr, &error));
    // Not JSON at all; glTF 1.0; a format not read.
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
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);  // no scenes, no nodes: the meshes as they are
    CHECK(m.has_value() && m->indices.size() == 6);
    if (!m) return;
    for (size_t t = 0; t < 6; t += 3) {
        const Vec3 a = m->vertices[m->indices[t]].position, b = m->vertices[m->indices[t + 1]].position, c = m->vertices[m->indices[t + 2]].position;
        CHECK(Cross(b - a, c - a).z > 0);  // both face the front
    }
}

RM_TEST(Mesh_Obj) {
    std::string error;
    // A square (one polygon of 4) with UVs and normals; the second face counts back from the end.
    const char* square = "# a square\nv 0 0 0\nv 1 0 0\nv 1 1 0\r\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                         "o Square\nusemtl none\nf 1/1/1 2/2/1 3/3/1 4/4/1\nf -4/-4/-1 -2/-2/-1 -1/-1/-1\n";
    const auto m = ParseObj(square, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->indices.size() == 9);
    CHECK(m->vertices.size() == 4);  // the same corner (v/vt/vn) is one vertex
    CHECK(NearF(m->vertices[0].uv.y, 1.f) && NearF(m->vertices[2].uv.y, 0.f));  // OBJ's V turned over
    CHECK(Near(m->min, {0, 0, 0}) && Near(m->max, {1, 1, 0}));
    // Without normals: made from the faces.
    const auto flat = ParseObj("v 0 0 0\nv 0 0 1\nv 1 0 0\nf 1 2 3\n", &error);
    CHECK(flat && Near(flat->vertices[0].normal, {0, 1, 0}));
    // A wrong number names its line.
    CHECK(!ParseObj("v 0 0 0\nv 1 0 0\nf 1 2 9\n", &error));
    CHECK(error.find("3 行目") != std::string::npos);
    CHECK(!ParseObj("v 0 0 0\n", &error));  // no faces
}

// --- Materials (colours and pictures) and the parts they cut a model into --------------------

namespace {

// The triangle twice, each primitive with its own material; `more` = the rest of the document
// ("materials", "textures", "images" ...); `extraViews` = more buffer views after the two.
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
    CHECK(std::abs(LinearToScreen(0.5f) - 0.7354f) < 1e-3f);  // half the light looks much brighter than half
    CHECK(NearF(LinearToScreen(-1), 0) && NearF(LinearToScreen(3), 1));
    CHECK(NearF(ScreenToLinear(0), 0) && NearF(ScreenToLinear(1), 1) && std::abs(ScreenToLinear(0.7354f) - 0.5f) < 1e-3f);
    for (const float v : {0.001f, 0.02f, 0.3f, 0.9f}) CHECK(std::abs(ScreenToLinear(LinearToScreen(v)) - v) < 1e-4f);  // there and back
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
    CHECK(m->images.size() == 1 && m->images[0].file == "tex a.png" && m->images[0].bytes.empty());  // a file next to the model
    // The factor is linear light, kept as it is (the renderer works in linear light).
    CHECK(NearF(m->materials[1].baseColor.r, 0.5f) && NearF(m->materials[1].baseColor.a, 0.5f));
    CHECK(m->materials[1].alpha == AlphaMode::Mask && NearF(m->materials[1].alphaCutoff, 0.3f) && m->materials[1].image == -1);
    CHECK(m->materials[0].alpha == AlphaMode::Opaque);
    // Nothing said about metal and roughness: glTF's defaults (a rough metal); no normal map, no light of its own.
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
    CHECK(NearF(a.emissive.r, 4) && NearF(a.emissive.g, 2) && NearF(a.emissive.b, 0));  // x the strength
    CHECK(a.image == -1);  // no colour picture
    // Out of 0..1: brought in.
    CHECK(NearF(m->materials[1].metallic, 1) && NearF(m->materials[1].roughness, 0));
    // A normal map's texture that is not there is told like the others.
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{"normalTexture":{"index":5}},{}])")), nullptr, &error));
    CHECK(error.find("テクスチャ 5") != std::string::npos);
}

RM_TEST(Mesh_Gltf_PicturesInsideTheFile) {
    // One picture in a buffer view (as in a .glb), one in a data: URI; a third material shares the first.
    const std::string doc = TwoMaterialGltf(
        R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{"pbrMetallicRoughness":{"baseColorTexture":{"index":1}}},)"
        R"({"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
        R"("textures":[{"source":0},{"source":1}],"images":[{"bufferView":2,"mimeType":"image/png"},{"uri":"data:image/png;base64,AAEC"}])",
        R"(,{"buffer":0,"byteOffset":0,"byteLength":4})", "0", "2");
    std::string error;
    const auto m = ParseGltf(Bytes(doc), nullptr, &error);
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->images.size() == 2);  // read once each, whoever uses them
    CHECK(m->materials.size() == 3 && m->materials[0].image == m->materials[2].image);
    CHECK(m->images[m->materials[0].image].bytes == std::vector<uint8_t>({0, 0, 0, 0}));  // the triangle's first float, 0
    CHECK(m->images[m->materials[1].image].bytes == std::vector<uint8_t>({0, 1, 2}));
    // Both triangles use pictures 0 (materials 0 and 2): two parts.
    CHECK(m->parts.size() == 2 && m->parts[0].material == 0 && m->parts[1].material == 2);
}

RM_TEST(Mesh_Gltf_BrokenMaterialsAreToldWhy) {
    std::string error;
    // A material that is not there.
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{}])")), nullptr, &error));
    CHECK(error.find("材質 1") != std::string::npos);
    // A texture that is not there; a picture's buffer view outside its buffer.
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":3}}},{}])")), nullptr, &error));
    CHECK(error.find("テクスチャ 3") != std::string::npos);
    CHECK(!ParseGltf(Bytes(TwoMaterialGltf(R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{}],)"
                                           R"("textures":[{"source":0}],"images":[{"bufferView":2}])",
                                           R"(,{"buffer":0,"byteOffset":40,"byteLength":40})")),
                     nullptr, &error));
    CHECK(error.find("はみ出して") != std::string::npos);
    // No material at all: one part, drawn white.
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
    CHECK(asked.size() == 1 && asked[0] == "mats/colors.mtl");  // '\' written by Windows tools
    CHECK(m->materials.size() == 3);
    if (m->materials.size() != 3) return;
    // Kd is the screen's colour: turned into linear light.
    const Color brick{ScreenToLinear(0.8f), ScreenToLinear(0.2f), ScreenToLinear(0.1f), 1};
    CHECK(m->materials[0].name == "Brick Red" && m->materials[0].baseColor == brick);
    CHECK(m->materials[0].image == 0 && m->images.size() == 3 && m->images[0].file == "mats/tex/brick wall.png");  // next to the .mtl
    CHECK(NearF(m->materials[0].roughness, 0.5f) && NearF(m->materials[0].metallic, 0));  // Ns 250: Blender's 0.5
    CHECK(m->materials[0].normalImage == 1 && m->images[1].file == "mats/tex/brick_n.png" && NearF(m->materials[0].normalScale, 0.5f));
    CHECK(m->materials[0].emissiveImage == 2 && NearF(m->materials[0].emissive.r, ScreenToLinear(0.5f)));
    CHECK(m->materials[1].name == "Plain" && NearF(m->materials[1].baseColor.g, ScreenToLinear(0.5f)) && NearF(m->materials[1].baseColor.a, 0.25f));
    CHECK(NearF(m->materials[1].metallic, 0.8f) && NearF(m->materials[1].roughness, 0.3f));
    CHECK(m->materials[1].alpha == AlphaMode::Blend && m->materials[0].alpha == AlphaMode::Opaque);
    CHECK(m->materials[2].name == "Ghost" && m->materials[2].baseColor == (Color{1, 1, 1, 1}));  // not in the .mtl: white
    CHECK(m->warnings.size() == 1 && m->warnings[0].find("Ghost") != std::string::npos);
    // The triangles sorted by material, in the order each was first used (Brick Red's two stay in order).
    CHECK(m->parts.size() == 3);
    if (m->parts.size() != 3) return;
    CHECK((m->parts[0] == MeshPart{0, 6, 0}) && (m->parts[1] == MeshPart{6, 3, 1}) && (m->parts[2] == MeshPart{9, 3, 2}));
    CHECK(Near(m->vertices[m->indices[5]].position, {0, 1, 0}));  // Brick Red's second triangle (1 2 4) is at 3..5
    // A missing .mtl is not a broken model: white, with a warning.
    const auto missing = ParseObj(obj, [](std::string_view) -> std::optional<std::vector<uint8_t>> { return std::nullopt; }, &error);
    CHECK(missing && missing->materials.size() == 3 && missing->warnings.size() == 1 && missing->warnings[0].find("colors.mtl") != std::string::npos);
}

RM_TEST(Mesh_SortByMaterial_KeepsTheOrderInside) {
    MeshData m;
    m.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    m.materials.resize(2);
    const int of[] = {1, -1, 1, 7};  // 7 is not a material: none
    SortByMaterial(m, of);
    CHECK(m.indices == std::vector<uint32_t>({0, 1, 2, 6, 7, 8, 3, 4, 5, 9, 10, 11}));
    CHECK(m.parts.size() == 2 && (m.parts[0] == MeshPart{0, 6, 1}) && (m.parts[1] == MeshPart{6, 6, -1}));
}

RM_TEST(MeshFile_RelatedFileStaysInside) {
    const std::filesystem::path root = L"C:/P/Assets";
    const std::filesystem::path model = L"C:/P/Assets/Models/house.gltf";
    CHECK(RelatedFile(model, "tex/wood.png", root) == std::filesystem::path(L"C:/P/Assets/Models/tex/wood.png").lexically_normal());
    CHECK(RelatedFile(model, "../Textures/wood.png", root) == std::filesystem::path(L"C:/P/Assets/Textures/wood.png").lexically_normal());
    CHECK(RelatedFile(model, "../../secret.png", root).empty());  // outside the project's Assets
    CHECK(RelatedFile(model, "C:/elsewhere/a.png", root).empty());
    CHECK(RelatedFile(model, "/a.png", root).empty());
    CHECK(RelatedFile(model, "", root).empty());
    // Only below the model's own folder (a .gltf's buffers, an OBJ's .mtl).
    CHECK(RelatedFile(model, "../x.bin", model.parent_path()).empty());
    CHECK(!RelatedFile(model, "bin/x.bin", model.parent_path()).empty());
}

RM_TEST(Space3d_BoundsAndShadowMatrix) {
    // A model's bounds placed in the world, and a ray hitting them where the unit cube is not.
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
    // Orthographic: the box's corners land on the edges.
    const Mat4 o = Orthographic(-2, 2, -1, 1, 1, 11);
    const auto back = ProjectToNdc(o, {2, 1, -11});
    CHECK(back && Near(*back, {1, 1, 1}));
    const auto front = ProjectToNdc(o, {-2, -1, -1});
    CHECK(front && Near(*front, {-1, -1, 0}));
    // The light's matrix holds every corner of what casts.
    const Bounds casters{{-1, 0, -1}, {1, 3, 2}};
    const Vec3 lightDir{-0.45f, -1.f, -0.3f};
    const Mat4 light = DirectionalShadowMatrix(lightDir, casters);
    for (int i = 0; i < 8; ++i) {
        const Vec3 p{(i & 1) ? casters.max.x : casters.min.x, (i & 2) ? casters.max.y : casters.min.y, (i & 4) ? casters.max.z : casters.min.z};
        const auto q = ProjectToNdc(light, p);
        CHECK(q && std::abs(q->x) <= 1 && std::abs(q->y) <= 1 && q->z >= 0 && q->z <= 1);
    }
    // The shadow of the top corner falls on the floor at the same place of the map, deeper.
    const Vec3 top{1, 3, 2};
    const Vec3 dir = Normalize(lightDir);
    const Vec3 onFloor = top + dir * (3.f / -dir.y);
    const auto a = ProjectToNdc(light, top), f = ProjectToNdc(light, onFloor);
    CHECK(a && f && NearF(a->x, f->x) && NearF(a->y, f->y) && f->z > a->z);
    // Straight down works too (another up is taken).
    const auto down = ProjectToNdc(DirectionalShadowMatrix({0, -1, 0}, casters), {0, 1, 0});
    CHECK(down && std::abs(down->x) <= 1 && down->z >= 0 && down->z <= 1);
}

RM_TEST(MeshRenderer_SavesItsModel) {
    game_x_3d::MeshRenderer m;
    CHECK(m.model.Empty() && m.castShadows);
    m.model.guid = "0123456789abcdef0123456789abcdef";  // an asset reference: saved as the id
    m.castShadows = false;
    const json::Value j = reflect::Save(m);
    CHECK(j.Get("model").AsString() == m.model.guid);
    game_x_3d::MeshRenderer back;
    reflect::Load(back, j);
    CHECK(back.model == m.model && !back.castShadows);
    CHECK(std::string_view(reflect::TypeOf<game_x_3d::MeshRenderer>().fields[0].assetKinds) == "model");
    // Data from before references (the path as a string) is read as it is (SoftwareHost::AssetFile finds the file).
    json::Value old = j;
    old.Set("model", "Assets/Models/robot.glb");
    reflect::Load(back, old);
    CHECK(back.model.guid == "Assets/Models/robot.glb");
}

RM_TEST(SceneViewState_KeepsShadows) {
    game_x_3d::SceneViewState st;
    CHECK(st.shadows);  // on at first
    st.shadows = false;
    game_x_3d::SceneViewState back;
    reflect::Load(back, reflect::Save(st));
    CHECK(!back.shadows);
}

RM_TEST(Space3d_ViewFromWorld_LooksAlongMinusZ) {
    // At (0, 0, 5) unturned: the world's middle is 5 in front (view space looks along -Z).
    Transform t;
    t.position = {0, 0, 5};
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(t)), {0, 0, 0}), {0, 0, -5}));
    // Turned 90 degrees about Y it looks along -X; a scale changes nothing.
    Transform turned;
    turned.rotation = {0, 90, 0};
    turned.scale = {2, 3, 4};
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(turned)), {-3, 0, 0}), {0, 0, -3}));
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(turned)), {0, 1, 0}), {0, 1, 0}));  // up stays up
    // Looking straight down still has a view.
    Transform down;
    down.rotation = {-90, 0, 0};
    CHECK(Near(TransformPoint(ViewFromWorld(LocalMatrix(down)), {0, -2, 0}), {0, 0, -2}));
}

RM_TEST(Space3d_FrustumCorners) {
    // 90 degrees, square, near 1, far 10, looking along -Z from the middle.
    const auto k = FrustumCorners(Perspective(90, 1, 1, 10));
    CHECK(k.has_value());
    if (!k) return;
    CHECK(Near((*k)[0], {-1, -1, -1}) && Near((*k)[1], {1, -1, -1}) && Near((*k)[2], {1, 1, -1}) && Near((*k)[3], {-1, 1, -1}));
    CHECK(Near((*k)[4], {-10, -10, -10}) && Near((*k)[6], {10, 10, -10}));
    // A matrix that cannot be inverted has none.
    Mat4 flat;
    flat.m[0][0] = 0;
    CHECK(!FrustumCorners(flat).has_value());
}

RM_TEST(Camera_Projection) {
    game_x_3d::Camera cam;  // perspective 60 degrees, 0.1 .. 1000
    const Mat4 p = game_x_3d::ProjectionOf(cam, 2.f);
    const Mat4 want = Perspective(60, 2.f, 0.1f, 1000.f);
    CHECK(std::memcmp(&p, &want, sizeof(Mat4)) == 0);
    // Orthographic: half the height is the size, the width follows the aspect; the same size at any depth.
    cam.projection = game_x_3d::CameraProjection::Orthographic;
    cam.orthographicSize = 5;
    const Mat4 o = game_x_3d::ProjectionOf(cam, 2.f);
    const auto near1 = ProjectToNdc(o, {10, 5, -1}), far1 = ProjectToNdc(o, {10, 5, -500});
    CHECK(near1 && far1 && NearF(near1->x, 1) && NearF(near1->y, 1) && NearF(far1->x, 1) && NearF(far1->y, 1));
    CHECK(near1 && far1 && near1->z >= 0 && far1->z <= 1 && far1->z > near1->z);
    // A far plane not beyond the near one (while typing) still gives a picture: no NaN.
    cam.projection = game_x_3d::CameraProjection::Perspective;
    cam.nearClip = 5;
    cam.farClip = 1;
    const auto q = ProjectToNdc(game_x_3d::ProjectionOf(cam, 1.f), {0, 0, -5});
    CHECK(q && std::isfinite(q->z));
    // The scene view draws its shape at most so far.
    game_x_3d::Camera wide;
    CHECK(NearF(game_x_3d::ShownNear(wide, 10).farClip, 10) && NearF(game_x_3d::ShownNear(wide, 10).nearClip, 0.1f));
    wide.farClip = 3;
    CHECK(NearF(game_x_3d::ShownNear(wide, 10).farClip, 3));
}

RM_TEST(Camera_Background) {
    game_x_3d::Camera cam;
    const Color game{0.2f, 0.3f, 0.4f, 1};
    const Color a = game_x_3d::BackgroundOf(cam, game);  // the game settings' 背景の色 unless its own
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
    CHECK(j.Get("projection").AsString() == "Orthographic");  // enums by name
    game_x_3d::Camera back;
    reflect::Load(back, j);
    CHECK(back.projection == game_x_3d::CameraProjection::Orthographic);
    game_x_3d::Light light;
    light.kind = game_x_3d::LightKind::Spot;
    game_x_3d::Light lb;
    reflect::Load(lb, reflect::Save(light));
    CHECK(lb.kind == game_x_3d::LightKind::Spot);
    json::Value big = reflect::Save(light);
    big.Set("intensity", 1000.0);  // clamped like every number (Max=100)
    reflect::Load(lb, big);
    CHECK(NearF(lb.intensity, 100));
}

RM_TEST(Lighting_DefaultLight) {
    // No light: the one white directional light (going the default way), with shadows.
    const LightSetup s = MakeLights({}, {0, -2, 0});
    CHECK(NearF(s.block.info[0], 1) && s.dropped == 0);
    CHECK(NearF(s.block.position[0][3], 0));    // directional
    CHECK(NearF(s.block.direction[0][1], -1));  // made length 1
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
    lights[1].type = LightType::Directional;  // casts no shadow
    lights[1].castShadow = false;
    lights[2].type = LightType::Directional;  // the first that casts: the shadow map is its
    lights[2].direction = {1, 0, 0};
    const LightSetup s = MakeLights(lights, {0, -1, 0});
    CHECK(NearF(s.block.info[0], 3));
    CHECK(NearF(s.block.position[0][0], 1) && NearF(s.block.position[0][2], 3) && NearF(s.block.position[0][3], 1));
    CHECK(NearF(s.block.color[0][0], 2) && NearF(s.block.color[0][1], 1) && NearF(s.block.color[0][2], 0));  // colour x strength
    CHECK(NearF(s.block.direction[0][3], 4));  // the range
    CHECK(NearF(s.block.color[0][3], 0) && NearF(s.block.color[1][3], 0) && NearF(s.block.color[2][3], 1));
    CHECK(s.shadowLight == 2 && Near(s.shadowDirection, {1, 0, 0}));
    // A point light never takes the shadow map (directional only).
    std::vector<Light> points(1);
    points[0].type = LightType::Point;
    CHECK(MakeLights(points, {0, -1, 0}).shadowLight == -1);
    // A spot: its cone's edge at half the angle, the fade from 3/4 of it.
    Light spot;
    spot.type = LightType::Spot;
    spot.spotAngle = 60;
    spot.intensity = -3;  // no negative light
    const LightSetup sp = MakeLights(std::span<const Light>(&spot, 1), {0, -1, 0});
    CHECK(NearF(sp.block.position[0][3], 2));
    CHECK(NearF(sp.block.spot[0][0], std::cos(30.f * 3.14159265f / 180.f)) && NearF(sp.block.spot[0][1], std::cos(22.5f * 3.14159265f / 180.f)));
    CHECK(NearF(sp.block.color[0][0], 0));
}

RM_TEST(Lighting_AtMostEight) {
    std::vector<Light> many(11);
    for (Light& l : many) l.type = LightType::Point;
    many[9].type = LightType::Directional;  // past the eighth: left out (no shadows)
    const LightSetup s = MakeLights(many, {0, -1, 0});
    CHECK(NearF(s.block.info[0], static_cast<float>(kMaxLights)) && s.dropped == 3 && s.shadowLight == -1);
}

RM_TEST(Light_FromItsEntity) {
    // Turned -90 degrees about X, the light's -Z points down; it is where its entity is.
    game_x_3d::Light light;
    light.kind = game_x_3d::LightKind::Spot;
    light.intensity = 3;
    Transform t;
    t.position = {4, 5, 6};
    t.rotation = {-90, 0, 0};
    const space3d::Light l = game_x_3d::ToRenderLight(light, LocalMatrix(t));
    CHECK(l.type == LightType::Spot && Near(l.position, {4, 5, 6}) && Near(l.direction, {0, -1, 0}) && NearF(l.intensity, 3));
    t.scale = {3, 3, 3};  // a scale does not make it longer
    CHECK(NearF(Length(game_x_3d::ToRenderLight(light, LocalMatrix(t)).direction), 1));
}

RM_TEST(Space3d_BoxInView_Culling) {
    // Looking along -Z from (0, 0, 5), 60 degrees, square, 0.1 .. 100.
    const Mat4 vp = Perspective(60, 1, 0.1f, 100) * LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(BoxInView(vp, {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}));   // in the middle
    CHECK(!BoxInView(vp, {{-0.5f, -0.5f, 6}, {0.5f, 0.5f, 7}}));         // behind the eye
    CHECK(!BoxInView(vp, {{20, -0.5f, -0.5f}, {21, 0.5f, 0.5f}}));       // far to the right
    CHECK(!BoxInView(vp, {{-21, -0.5f, -0.5f}, {-20, 0.5f, 0.5f}}));     // far to the left
    CHECK(!BoxInView(vp, {{-0.5f, 20, -0.5f}, {0.5f, 21, 0.5f}}));       // above
    CHECK(!BoxInView(vp, {{-0.5f, -0.5f, -200}, {0.5f, 0.5f, -150}}));   // beyond the far plane
    // Partly inside (it crosses the edge, or reaches from behind the eye to the front): seen.
    CHECK(BoxInView(vp, {{2, -0.5f, -0.5f}, {30, 0.5f, 0.5f}}));
    CHECK(BoxInView(vp, {{-0.5f, -0.5f, -3}, {0.5f, 0.5f, 10}}));
    // A huge box around the eye: seen (every corner is outside, but not outside the same side).
    CHECK(BoxInView(vp, {{-1000, -1000, -1000}, {1000, 1000, 1000}}));
    // Orthographic too (the camera component's other projection).
    game_x_3d::Camera cam;
    cam.projection = game_x_3d::CameraProjection::Orthographic;
    cam.orthographicSize = 2;
    const Mat4 o = game_x_3d::ProjectionOf(cam, 1.f) * LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    CHECK(BoxInView(o, {{1.5f, 1.5f, -0.5f}, {2.5f, 2.5f, 0.5f}}));
    CHECK(!BoxInView(o, {{2.5f, -0.5f, -0.5f}, {3.5f, 0.5f, 0.5f}}));  // just past the right edge (no perspective widening)
}

// --- Reading models on a thread of their own (space3d/runtime/MeshLoader) ---------------------

namespace {

// Waits (a little at a time) until `ticket` is done; nullopt after about 5 seconds.
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
    MeshLoader loader([&](const std::filesystem::path& file, std::string* error) -> std::optional<MeshData> {
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
    CHECK(!loader.Take(a));  // taken once only
    CHECK(loader.Pending() == 0);
    std::lock_guard lock(mu);
    CHECK(read == std::vector<std::string>({"a.obj", "bad.obj"}));
}

RM_TEST(MeshLoader_CancelledIsNotKept) {
    std::atomic<bool> open{false};
    std::atomic<int> calls{0};
    MeshLoader loader([&](const std::filesystem::path&, std::string* error) -> std::optional<MeshData> {
        ++calls;
        while (!open) std::this_thread::sleep_for(std::chrono::milliseconds(1));  // held until the test opens it
        return ParseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", error);
    });
    const auto first = loader.Request("first.obj");    // being read (held)
    const auto second = loader.Request("second.obj");  // waiting
    while (calls == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    loader.Cancel(first);   // dropped when its reading ends
    loader.Cancel(second);  // never read
    CHECK(loader.Pending() == 0);
    const auto third = loader.Request("third.obj");
    open = true;
    CHECK(WaitFor(loader, third).has_value());
    CHECK(!loader.Take(first) && !loader.Take(second));
    CHECK(calls == 2);  // first and third
}

RM_TEST(MeshLoader_StopsWhileReading) {
    // Destroyed with work left: waits for the file being read, drops the rest (no crash, no hang).
    std::atomic<int> calls{0};
    {
        MeshLoader loader([&](const std::filesystem::path&, std::string*) -> std::optional<MeshData> {
            ++calls;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return std::nullopt;
        });
        for (int i = 0; i < 5; ++i) loader.Request("x.obj");
        while (calls == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(calls >= 1 && calls < 5);
}
