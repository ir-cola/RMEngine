// Tests for the 2D space plugin (plugins/rm_engine/space2d: Transform2D, View2D, the gizmo) and the
// connector ゲーム制作 × 2D's pure parts (the sprite's data, 回り続ける (2D), the view's saved state).

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_2d/editor/SceneViewState2D.h"
#include "rm_engine/game_x_2d/runtime/Spin2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/editor/Gizmo2D.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space2d/runtime/View2D.h"

#include <cmath>
#include <vector>

using namespace rm;
using namespace rm::space2d;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
bool Near(Vec2 a, Vec2 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

Transform2D Make(Vec2 pos, float deg, Vec2 scale, int order = 0) {
    Transform2D t;
    t.position = pos;
    t.rotation = deg;
    t.scale = scale;
    t.order = order;
    return t;
}

}  // namespace

RM_TEST(Space2d_LocalMatrix_ScaleTurnMove) {
    CHECK(Near(TransformPoint(LocalMatrix({}), {1, 2}), {1, 2}));  // the defaults change nothing
    // (1,0) -> scaled (2,0) -> 90 degrees counter-clockwise (0,2) -> moved (3,4)
    const Mat3 m = LocalMatrix(Make({3, 2}, 90, {2, 2}));
    CHECK(Near(TransformPoint(m, {1, 0}), {3, 4}));
    CHECK(Near(TransformDirection(m, {1, 0}), {0, 2}));  // a direction does not move
    // Scale before turning: (0,1) scaled by (1,3) -> (0,3), turned 90 -> (-3,0).
    CHECK(Near(TransformPoint(LocalMatrix(Make({0, 0}, 90, {1, 3})), {0, 1}), {-3, 0}));
}

RM_TEST(Space2d_WorldMatrix_FollowsTheParent) {
    const Mat3 parent = LocalMatrix(Make({10, 0}, 90, {2, 2}));
    const Mat3 world = WorldMatrix(parent, Make({1, 0}, 0, {1, 1}));
    CHECK(Near(TransformPoint(world, {0, 0}), {10, 2}));  // 1 along the parent's X (turned up, scaled 2)
    const auto inv = Inverse(world);
    CHECK(inv.has_value());
    CHECK(Near(TransformPoint(*inv, TransformPoint(world, {3, -4})), {3, -4}));
    CHECK(!Inverse(Scaling({0, 1})).has_value());  // a scale of 0 cannot be undone
    CHECK(Determinant(Scaling({-1, 1})) < 0);       // mirrored
}

RM_TEST(Space2d_Decompose_AndBackFromTheWorld) {
    const Mat3 parent = LocalMatrix(Make({5, 5}, 30, {2, 2}, 3));
    const Transform2D child = Make({1, 2}, 45, {1.5f, 0.5f}, 2);
    const WorldPlacement2D w = Decompose(WorldMatrix(parent, child), child.rotation);
    CHECK(Near(w.rotation, 75));
    CHECK(Near(w.scale, {3, 1}));
    CHECK(Near(w.position, TransformPoint(parent, {1, 2})));
    // Near the hint: 75 or 435 or -285 are the same turn.
    CHECK(Near(Decompose(WorldMatrix(parent, child), 400).rotation, 435));
    // Mirrored: a negative Y scale.
    CHECK(Near(Decompose(Scaling({1, -2})).scale, {1, -2}));

    // Typing only the world X moves along it; nothing else changes.
    WorldPlacement2D want = w;
    want.order = 3 + child.order;
    want.position.x += 4;
    Transform2D t = FromWorld(child, parent, 3, want);
    CHECK(Near(t.rotation, child.rotation) && Near(t.scale, child.scale) && t.order == child.order);
    CHECK(Near(TransformPoint(WorldMatrix(parent, t), {0, 0}), want.position));
    // The world rotation, scale and order.
    want = w;
    want.order = 3 + child.order;
    want.rotation = 100;
    want.scale = {6, 1};
    want.order = 10;
    t = FromWorld(child, parent, 3, want);
    const WorldPlacement2D back = Decompose(WorldMatrix(parent, t), t.rotation);
    CHECK(Near(back.rotation, 100) && Near(back.scale, {6, 1}) && Near(back.position, w.position));
    CHECK(t.order == 7);
    // Under a mirrored parent a turn goes the other way.
    const Mat3 mirror = Scaling({-1, 1});
    WorldPlacement2D turned = Decompose(WorldMatrix(mirror, Make({0, 0}, 10, {1, 1})), 0);
    turned.rotation += 20;
    const Transform2D m = FromWorld(Make({0, 0}, 10, {1, 1}), mirror, 0, turned);
    CHECK(Near(m.rotation, -10));
}

RM_TEST(Space2d_DrawOrder_BackToFront) {
    // (order, subOrder, tree): the order first, then the sprite's, then later in the tree in front.
    const std::vector<DrawKey> keys{{1, 0, 0}, {0, 5, 1}, {0, 0, 2}, {1, 0, 3}, {-2, 9, 4}, {0, 0, 0}};
    const auto out = DrawOrder(keys);
    const std::vector<size_t> want{4, 5, 2, 1, 0, 3};
    CHECK(out == want);
}

RM_TEST(Space2d_Camera_ScreenAndBack) {
    Camera2D cam;
    cam.center = {2, 1};
    cam.zoom = 50;
    const ScreenRect r{100, 50, 400, 300};
    CHECK(Near(ToScreen(cam, r, {2, 1}), {300, 200}));  // the middle
    CHECK(Near(ToScreen(cam, r, {3, 2}), {350, 150}));  // right and up: Y goes down on the screen
    CHECK(Near(ToWorld(cam, r, ToScreen(cam, r, {-7, 4.5f})), {-7, 4.5f}));
    CHECK(Near(TransformPoint(ScreenMatrix(cam, r), {3, 2}), {350, 150}));  // the matrix agrees
    // The picture follows the mouse.
    const Vec2 before = ToScreen(cam, r, {0, 0});
    Pan(cam, 10, -20);
    CHECK(Near(ToScreen(cam, r, {0, 0}), {before.x + 10, before.y - 20}));
    // Zooming keeps the point under the mouse where it is.
    const Vec2 mouse{130, 260};
    const Vec2 under = ToWorld(cam, r, mouse);
    ZoomAt(cam, r, mouse, 3);
    CHECK(cam.zoom > 50);
    CHECK(Near(ToWorld(cam, r, mouse), under));
    ZoomAt(cam, r, mouse, -1000);
    CHECK(Near(cam.zoom, kMinZoom));  // never less
    // Frame: the box is in the middle and fits.
    Frame(cam, r, {10, 10}, {2, 1});
    CHECK(Near(cam.center, {10, 10}));
    CHECK(ToScreen(cam, r, {12, 11}).x <= r.x + r.w && ToScreen(cam, r, {12, 11}).y >= r.y);
}

RM_TEST(Space2d_Grid_StepsAreFarEnoughApart) {
    for (const float zoom : {0.5f, 3.f, 12.f, 100.f, 777.f, 20000.f}) {
        const GridStep g = GridFor(zoom, 12);
        CHECK(g.step * zoom >= 12 * 0.999f);
        CHECK(g.step * zoom < 12 * 5.01f);  // and not much farther than needed
        CHECK(g.major == 5 || g.major == 10);
    }
    CHECK(Near(GridFor(100, 12).step, 0.2f));  // 0.1 would be 10 DIPs apart
    CHECK(Near(GridFor(12, 12).step, 1));
}

RM_TEST(Space2d_InsidePlacedBox_TurnedAndMirrored) {
    const Mat3 w = LocalMatrix(Make({5, 0}, 45, {2, 1}));
    CHECK(InsidePlacedBox({5, 0}, w, {-0.5f, -0.5f}, {0.5f, 0.5f}));
    CHECK(InsidePlacedBox(TransformPoint(w, {0.45f, 0.45f}), w, {-0.5f, -0.5f}, {0.5f, 0.5f}));
    CHECK(!InsidePlacedBox(TransformPoint(w, {0.55f, 0}), w, {-0.5f, -0.5f}, {0.5f, 0.5f}));
    CHECK(!InsidePlacedBox({6, 0}, w, {-0.5f, -0.5f}, {0.5f, 0.5f}));  // the corner turned away
    const Mat3 m = LocalMatrix(Make({0, 0}, 0, {-1, 1}));
    CHECK(InsidePlacedBox({-0.3f, 0}, m, {0, -0.5f}, {1, 0.5f}));  // mirrored: the box is on the left
    CHECK(!InsidePlacedBox({0.3f, 0}, m, {0, -0.5f}, {1, 0.5f}));
    CHECK(!InsidePlacedBox({0, 0}, Scaling({0, 1}), {-1, -1}, {1, 1}));  // flat: nothing inside
    Vec2 k[4];
    PlacedBoxCorners(Translation({1, 1}), {-1, -1}, {1, 1}, k);
    CHECK(Near(k[0], {0, 0}) && Near(k[2], {2, 2}));
}

RM_TEST(Space2d_Gizmo_HitTest) {
    using namespace gizmo2d;
    Camera2D cam;  // zoom 100, centre (0, 0)
    const ScreenRect r{0, 0, 400, 400};
    const gizmo2d::Frame f = MakeFrame(LocalMatrix(Make({0, 0}, 0, {1, 1})), Tool::Move, Space::Local);
    // The middle is (200, 200): X goes right, Y up (the screen's up).
    CHECK(HitTest(Tool::Move, f, cam, r, {200, 200}) == Handle::Free);
    CHECK(HitTest(Tool::Move, f, cam, r, {200 + 60, 202}) == Handle::X);
    CHECK(HitTest(Tool::Move, f, cam, r, {198, 200 - 60}) == Handle::Y);
    CHECK(HitTest(Tool::Move, f, cam, r, {200 + 60, 200 - 60}) == Handle::None);
    CHECK(HitTest(Tool::Move, f, cam, r, {200 + kArrowLength + 20, 200}) == Handle::None);  // past the tip
    CHECK(HitTest(Tool::Rotate, f, cam, r, {200 + kRingRadius, 200}) == Handle::Ring);
    CHECK(HitTest(Tool::Rotate, f, cam, r, {200 + 20, 200}) == Handle::None);
    // Local: the arrows turn with the thing (90 degrees: its X points up on the screen).
    const gizmo2d::Frame turned = MakeFrame(LocalMatrix(Make({0, 0}, 90, {1, 1})), Tool::Move, Space::Local);
    CHECK(HitTest(Tool::Move, turned, cam, r, {200, 200 - 60}) == Handle::X);
    const gizmo2d::Frame world = MakeFrame(LocalMatrix(Make({0, 0}, 90, {1, 1})), Tool::Move, Space::World);
    CHECK(HitTest(Tool::Move, world, cam, r, {200 + 60, 200}) == Handle::X);
    // Scale is always along the thing's own axes.
    const gizmo2d::Frame scale = MakeFrame(LocalMatrix(Make({0, 0}, 90, {1, 1})), Tool::Scale, Space::World);
    CHECK(Near(scale.axes[0], {0, 1}));
}

RM_TEST(Space2d_Gizmo_MoveTurnScale) {
    using namespace gizmo2d;
    Camera2D cam;
    const ScreenRect r{0, 0, 400, 400};
    const Transform2D start = Make({0, 0}, 0, {1, 1});
    const Mat3 world = LocalMatrix(start);
    // Move along X: the mouse goes right 50 DIPs and up 30 -> only 0.5 along X.
    gizmo2d::Frame f = MakeFrame(world, Tool::Move, Space::Local);
    Drag d = BeginDrag(Tool::Move, Handle::X, f, cam, r, {260, 200});
    Change ch = Update(d, cam, r, {310, 170}, {});
    CHECK(Near(ch.move, {0.5f, 0}));
    CHECK(Near(Apply(start, {}, world, ch).position, {0.5f, 0}));
    // Free, in steps of 0.25: (0.37, 0.6) -> (0.25, 0.5).
    d = BeginDrag(Tool::Move, Handle::Free, f, cam, r, {200, 200});
    Snap snap;
    snap.on = true;
    snap.move = 0.25f;
    ch = Update(d, cam, r, {237, 140}, snap);
    CHECK(Near(ch.move, {0.25f, 0.5f}));
    // Under a parent turned 90 and scaled 2: a world move of (0, 2) is 1 along the parent's X.
    const Mat3 parent = LocalMatrix(Make({0, 0}, 90, {2, 2}));
    Change up;
    up.move = {0, 2};
    CHECK(Near(Apply(start, parent, WorldMatrix(parent, start), up).position, {1, 0}));

    // Turn: a quarter of the ring counter-clockwise (right -> up on the screen) = +90; going on past
    // 180 keeps counting.
    f = MakeFrame(world, Tool::Rotate, Space::Local);
    d = BeginDrag(Tool::Rotate, Handle::Ring, f, cam, r, {270, 200});
    ch = Update(d, cam, r, {200, 130}, {});
    CHECK(Near(ch.degrees, 90));
    ch = Update(d, cam, r, {130, 200}, {});
    ch = Update(d, cam, r, {200, 270}, {});
    CHECK(Near(ch.degrees, 270));
    CHECK(Near(Apply(start, {}, world, ch).rotation, 270));
    CHECK(Near(Apply(start, Scaling({-1, 1}), world, ch).rotation, -270));  // a mirrored parent
    snap.degrees = 15;
    d = BeginDrag(Tool::Rotate, Handle::Ring, f, cam, r, {270, 200});
    ch = Update(d, cam, r, {270, 180}, snap);  // about 16 degrees -> 15
    CHECK(Near(ch.degrees, 15));

    // Scale along X: from 0.6 to 1.2 of the way = twice.
    f = MakeFrame(world, Tool::Scale, Space::Local);
    d = BeginDrag(Tool::Scale, Handle::X, f, cam, r, {260, 200});
    ch = Update(d, cam, r, {320, 230}, {});
    CHECK(Near(ch.scale, {2, 1}));
    CHECK(Near(Apply(Make({0, 0}, 0, {3, 1}), {}, world, ch).scale, {6, 1}));
    // The middle: 50 right and 50 up = 1 + 100 * 0.01 = twice, evenly.
    d = BeginDrag(Tool::Scale, Handle::Free, f, cam, r, {200, 200});
    ch = Update(d, cam, r, {250, 150}, {});
    CHECK(Near(ch.scale, {2, 2}));
    ch = Update(d, cam, r, {0, 400}, {});
    CHECK(ch.scale.x >= 0.01f - 1e-6f);  // never below 1/100
}

RM_TEST(Space2d_Spin2D_TurnsTheTransform2D) {
    using namespace game_production;
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_2d::Spin2D>();
    const reflect::TypeDesc& transform = reflect::TypeOf<Transform2D>();
    Scene s;
    s.SetComponentTypes({&transform, &spin}, {&spin});
    const int e = s.Create("turning");
    s.AddComponent(e, transform);
    s.AddComponent(e, spin);  // 90 degrees a second
    s.AddComponent(e, spin);
    static_cast<game_x_2d::Spin2D*>(s.Find(e)->FindComponent(spin.name, 1)->value.Get())->degreesPerSecond = -30;
    static const SystemInfo kSpin{"回り続ける (2D)", SystemPhase::Frame, 0, &game_x_2d::RunSpin2D};
    const SystemInfo* list[] = {&kSpin};
    SystemRunner runner;
    runner.SetSystems(list);
    runner.Advance(s, 0.5, 0.02f);
    const float& rot = static_cast<const Transform2D*>(s.Find(e)->FindComponent(transform.name)->value.Get())->rotation;
    CHECK(Near(rot, 30));  // (90 - 30) * 0.5: the two add up
    for (int i = 0; i < 10; ++i) runner.Advance(s, 1.0, 0.02f);
    CHECK(rot > -360 && rot < 360);
}

RM_TEST(Space2d_SpriteAndViewState_SaveAndLoad) {
    game_x_2d::Sprite sp;
    sp.image.guid = "0123456789abcdef0123456789abcdef";
    sp.color = {1, 0.5f, 0.25f, 0.5f};
    sp.flipX = true;
    sp.order = -3;
    sp.pixelsPerUnit = 32;
    const json::Value saved = reflect::Save(sp, nullptr);
    game_x_2d::Sprite back;
    reflect::Load(back, saved, nullptr, nullptr);
    CHECK(back.image.guid == sp.image.guid && back.color == sp.color && back.flipX && !back.flipY && back.order == -3 &&
          Near(back.pixelsPerUnit, 32));
    // 1 単位のピクセル数 is never 0 (the size divides by it).
    json::Value zero = saved;
    zero.Set("pixelsPerUnit", 0.0);
    reflect::Load(back, zero, nullptr, nullptr);
    CHECK(back.pixelsPerUnit > 0);

    // The view's state: unknown keys are written back; the zoom stays in range.
    game_x_2d::SceneViewState2D st;
    st.camera.center = {3, -4};
    st.tool = gizmo2d::Tool::Rotate;
    st.grid = false;
    json::Value v = reflect::Save(st, nullptr);
    v.Set("newerKey", 7);
    json::Value cam = v.Get("camera");
    cam.Set("zoom", 1e9);
    v.Set("camera", cam);
    game_x_2d::SceneViewState2D st2;
    reflect::Unknowns unknowns;
    reflect::Load(st2, v, &unknowns, nullptr);
    CHECK(Near(st2.camera.center, {3, -4}) && st2.tool == gizmo2d::Tool::Rotate && !st2.grid);
    CHECK(st2.camera.zoom <= kMaxZoom);
    CHECK(reflect::Save(st2, &unknowns).Get("newerKey").AsInt() == 7);
}
