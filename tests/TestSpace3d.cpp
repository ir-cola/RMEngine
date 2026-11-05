// Tests for the 3D space plugin's math (plugins/rm_engine/space3d/runtime/Transform).

#include "Test.h"

#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>

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
