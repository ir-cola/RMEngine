// 2D 空間プラグイン (plugins/rm_engine/space2d: Transform2D、View2D、ギズモ) と、
// コネクター ゲーム制作 × 2D の純粋な部分 (スプライトのデータ、回り続ける (2D)、
// ビューの保存状態) のテスト。

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_2d/editor/SceneViewState2D.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/Spin2D.h"
#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/editor/Gizmo2D.h"
#include "rm_engine/space2d/runtime/Atlas.h"
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
    CHECK(Near(TransformPoint(LocalMatrix({}), {1, 2}), {1, 2}));  // 初期値は何も変えない
    // (1,0) -> 拡大 (2,0) -> 反時計回りに 90 度 (0,2) -> 移動 (3,4)
    const Mat3 m = LocalMatrix(Make({3, 2}, 90, {2, 2}));
    CHECK(Near(TransformPoint(m, {1, 0}), {3, 4}));
    CHECK(Near(TransformDirection(m, {1, 0}), {0, 2}));  // 向きは動かない
    // 回す前に拡大: (0,1) を (1,3) で拡大 -> (0,3)、90 回す -> (-3,0)。
    CHECK(Near(TransformPoint(LocalMatrix(Make({0, 0}, 90, {1, 3})), {0, 1}), {-3, 0}));
}

RM_TEST(Space2d_WorldMatrix_FollowsTheParent) {
    const Mat3 parent = LocalMatrix(Make({10, 0}, 90, {2, 2}));
    const Mat3 world = WorldMatrix(parent, Make({1, 0}, 0, {1, 1}));
    CHECK(Near(TransformPoint(world, {0, 0}), {10, 2}));  // 親の X に沿って 1 (上に回し、2 倍に拡大した)
    const auto inv = Inverse(world);
    CHECK(inv.has_value());
    CHECK(Near(TransformPoint(*inv, TransformPoint(world, {3, -4})), {3, -4}));
    CHECK(!Inverse(Scaling({0, 1})).has_value());  // 拡大が 0 だと元に戻せない
    CHECK(Determinant(Scaling({-1, 1})) < 0);       // 反転
}

RM_TEST(Space2d_Decompose_AndBackFromTheWorld) {
    const Mat3 parent = LocalMatrix(Make({5, 5}, 30, {2, 2}, 3));
    const Transform2D child = Make({1, 2}, 45, {1.5f, 0.5f}, 2);
    const WorldPlacement2D w = Decompose(WorldMatrix(parent, child), child.rotation);
    CHECK(Near(w.rotation, 75));
    CHECK(Near(w.scale, {3, 1}));
    CHECK(Near(w.position, TransformPoint(parent, {1, 2})));
    // ヒントに近い: 75 も 435 も -285 も同じ回転。
    CHECK(Near(Decompose(WorldMatrix(parent, child), 400).rotation, 435));
    // 反転: Y の拡大が負。
    CHECK(Near(Decompose(Scaling({1, -2})).scale, {1, -2}));

    // ワールドの X だけ打つと、それに沿って動き、ほかは変わらない。
    WorldPlacement2D want = w;
    want.order = 3 + child.order;
    want.position.x += 4;
    Transform2D t = FromWorld(child, parent, 3, want);
    CHECK(Near(t.rotation, child.rotation) && Near(t.scale, child.scale) && t.order == child.order);
    CHECK(Near(TransformPoint(WorldMatrix(parent, t), {0, 0}), want.position));
    // ワールドの回転、拡大、順番。
    want = w;
    want.order = 3 + child.order;
    want.rotation = 100;
    want.scale = {6, 1};
    want.order = 10;
    t = FromWorld(child, parent, 3, want);
    const WorldPlacement2D back = Decompose(WorldMatrix(parent, t), t.rotation);
    CHECK(Near(back.rotation, 100) && Near(back.scale, {6, 1}) && Near(back.position, w.position));
    CHECK(t.order == 7);
    // 反転した親の下では、回転が逆向きになる。
    const Mat3 mirror = Scaling({-1, 1});
    WorldPlacement2D turned = Decompose(WorldMatrix(mirror, Make({0, 0}, 10, {1, 1})), 0);
    turned.rotation += 20;
    const Transform2D m = FromWorld(Make({0, 0}, 10, {1, 1}), mirror, 0, turned);
    CHECK(Near(m.rotation, -10));
}

RM_TEST(Space2d_DrawOrder_BackToFront) {
    // (order, subOrder, 木): まず order、次にスプライトのもの、そのあとは木で後ろのものが手前。
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
    CHECK(Near(ToScreen(cam, r, {2, 1}), {300, 200}));  // 真ん中
    CHECK(Near(ToScreen(cam, r, {3, 2}), {350, 150}));  // 右と上: 画面では Y は下向き
    CHECK(Near(ToWorld(cam, r, ToScreen(cam, r, {-7, 4.5f})), {-7, 4.5f}));
    CHECK(Near(TransformPoint(ScreenMatrix(cam, r), {3, 2}), {350, 150}));  // 行列も一致する
    // 画像はマウスに付いてくる。
    const Vec2 before = ToScreen(cam, r, {0, 0});
    Pan(cam, 10, -20);
    CHECK(Near(ToScreen(cam, r, {0, 0}), {before.x + 10, before.y - 20}));
    // ズームしても、マウスの下の点は同じ場所に残る。
    const Vec2 mouse{130, 260};
    const Vec2 under = ToWorld(cam, r, mouse);
    ZoomAt(cam, r, mouse, 3);
    CHECK(cam.zoom > 50);
    CHECK(Near(ToWorld(cam, r, mouse), under));
    ZoomAt(cam, r, mouse, -1000);
    CHECK(Near(cam.zoom, kMinZoom));  // それ以下にはならない
    // Frame: 箱は真ん中に来て、収まる。
    Frame(cam, r, {10, 10}, {2, 1});
    CHECK(Near(cam.center, {10, 10}));
    CHECK(ToScreen(cam, r, {12, 11}).x <= r.x + r.w && ToScreen(cam, r, {12, 11}).y >= r.y);
}

RM_TEST(Space2d_Grid_StepsAreFarEnoughApart) {
    for (const float zoom : {0.5f, 3.f, 12.f, 100.f, 777.f, 20000.f}) {
        const GridStep g = GridFor(zoom, 12);
        CHECK(g.step * zoom >= 12 * 0.999f);
        CHECK(g.step * zoom < 12 * 5.01f);  // 必要以上には遠くならない
        CHECK(g.major == 5 || g.major == 10);
    }
    CHECK(Near(GridFor(100, 12).step, 0.2f));  // 0.1 だと 10 DIP 間隔になる
    CHECK(Near(GridFor(12, 12).step, 1));
}

RM_TEST(Space2d_InsidePlacedBox_TurnedAndMirrored) {
    const Mat3 w = LocalMatrix(Make({5, 0}, 45, {2, 1}));
    CHECK(InsidePlacedBox({5, 0}, w, {-0.5f, -0.5f}, {0.5f, 0.5f}));
    CHECK(InsidePlacedBox(TransformPoint(w, {0.45f, 0.45f}), w, {-0.5f, -0.5f}, {0.5f, 0.5f}));
    CHECK(!InsidePlacedBox(TransformPoint(w, {0.55f, 0}), w, {-0.5f, -0.5f}, {0.5f, 0.5f}));
    CHECK(!InsidePlacedBox({6, 0}, w, {-0.5f, -0.5f}, {0.5f, 0.5f}));  // 向こうを向いた角
    const Mat3 m = LocalMatrix(Make({0, 0}, 0, {-1, 1}));
    CHECK(InsidePlacedBox({-0.3f, 0}, m, {0, -0.5f}, {1, 0.5f}));  // 反転: 箱は左にある
    CHECK(!InsidePlacedBox({0.3f, 0}, m, {0, -0.5f}, {1, 0.5f}));
    CHECK(!InsidePlacedBox({0, 0}, Scaling({0, 1}), {-1, -1}, {1, 1}));  // 平ら: 中には何もない
    Vec2 k[4];
    PlacedBoxCorners(Translation({1, 1}), {-1, -1}, {1, 1}, k);
    CHECK(Near(k[0], {0, 0}) && Near(k[2], {2, 2}));
}

RM_TEST(Space2d_Gizmo_HitTest) {
    using namespace gizmo2d;
    Camera2D cam;  // ズーム 100、中心 (0, 0)
    const ScreenRect r{0, 0, 400, 400};
    const gizmo2d::Frame f = MakeFrame(LocalMatrix(Make({0, 0}, 0, {1, 1})), Tool::Move, Space::Local);
    // 真ん中は (200, 200): X は右、Y は上 (画面の上)。
    CHECK(HitTest(Tool::Move, f, cam, r, {200, 200}) == Handle::Free);
    CHECK(HitTest(Tool::Move, f, cam, r, {200 + 60, 202}) == Handle::X);
    CHECK(HitTest(Tool::Move, f, cam, r, {198, 200 - 60}) == Handle::Y);
    CHECK(HitTest(Tool::Move, f, cam, r, {200 + 60, 200 - 60}) == Handle::None);
    CHECK(HitTest(Tool::Move, f, cam, r, {200 + kArrowLength + 20, 200}) == Handle::None);  // 先端の先
    CHECK(HitTest(Tool::Rotate, f, cam, r, {200 + kRingRadius, 200}) == Handle::Ring);
    CHECK(HitTest(Tool::Rotate, f, cam, r, {200 + 20, 200}) == Handle::None);
    // ローカル: 矢印は物と一緒に回る (90 度: その X が画面の上を向く)。
    const gizmo2d::Frame turned = MakeFrame(LocalMatrix(Make({0, 0}, 90, {1, 1})), Tool::Move, Space::Local);
    CHECK(HitTest(Tool::Move, turned, cam, r, {200, 200 - 60}) == Handle::X);
    const gizmo2d::Frame world = MakeFrame(LocalMatrix(Make({0, 0}, 90, {1, 1})), Tool::Move, Space::World);
    CHECK(HitTest(Tool::Move, world, cam, r, {200 + 60, 200}) == Handle::X);
    // 拡大はいつも物自身の軸に沿う。
    const gizmo2d::Frame scale = MakeFrame(LocalMatrix(Make({0, 0}, 90, {1, 1})), Tool::Scale, Space::World);
    CHECK(Near(scale.axes[0], {0, 1}));
}

RM_TEST(Space2d_Gizmo_MoveTurnScale) {
    using namespace gizmo2d;
    Camera2D cam;
    const ScreenRect r{0, 0, 400, 400};
    const Transform2D start = Make({0, 0}, 0, {1, 1});
    const Mat3 world = LocalMatrix(start);
    // X に沿って移動: マウスが右へ 50 DIP、上へ 30 -> X に沿って 0.5 だけ。
    gizmo2d::Frame f = MakeFrame(world, Tool::Move, Space::Local);
    Drag d = BeginDrag(Tool::Move, Handle::X, f, cam, r, {260, 200});
    Change ch = Update(d, cam, r, {310, 170}, {});
    CHECK(Near(ch.move, {0.5f, 0}));
    CHECK(Near(Apply(start, {}, world, ch).position, {0.5f, 0}));
    // 自由に、0.25 刻みで: (0.37, 0.6) -> (0.25, 0.5)。
    d = BeginDrag(Tool::Move, Handle::Free, f, cam, r, {200, 200});
    Snap snap;
    snap.on = true;
    snap.move = 0.25f;
    ch = Update(d, cam, r, {237, 140}, snap);
    CHECK(Near(ch.move, {0.25f, 0.5f}));
    // 90 回して 2 倍に拡大した親の下: ワールドで (0, 2) 動かすと、親の X に沿って 1。
    const Mat3 parent = LocalMatrix(Make({0, 0}, 90, {2, 2}));
    Change up;
    up.move = {0, 2};
    CHECK(Near(Apply(start, parent, WorldMatrix(parent, start), up).position, {1, 0}));

    // 回転: 輪の 4 分の 1 を反時計回り (画面では右 -> 上) = +90。180 を
    // 越えてさらに進んでも数え続ける。
    f = MakeFrame(world, Tool::Rotate, Space::Local);
    d = BeginDrag(Tool::Rotate, Handle::Ring, f, cam, r, {270, 200});
    ch = Update(d, cam, r, {200, 130}, {});
    CHECK(Near(ch.degrees, 90));
    ch = Update(d, cam, r, {130, 200}, {});
    ch = Update(d, cam, r, {200, 270}, {});
    CHECK(Near(ch.degrees, 270));
    CHECK(Near(Apply(start, {}, world, ch).rotation, 270));
    CHECK(Near(Apply(start, Scaling({-1, 1}), world, ch).rotation, -270));  // 反転した親
    snap.degrees = 15;
    d = BeginDrag(Tool::Rotate, Handle::Ring, f, cam, r, {270, 200});
    ch = Update(d, cam, r, {270, 180}, snap);  // 約 16 度 -> 15
    CHECK(Near(ch.degrees, 15));

    // X に沿って拡大: 0.6 から 1.2 の所まで = 2 倍。
    f = MakeFrame(world, Tool::Scale, Space::Local);
    d = BeginDrag(Tool::Scale, Handle::X, f, cam, r, {260, 200});
    ch = Update(d, cam, r, {320, 230}, {});
    CHECK(Near(ch.scale, {2, 1}));
    CHECK(Near(Apply(Make({0, 0}, 0, {3, 1}), {}, world, ch).scale, {6, 1}));
    // 真ん中: 右へ 50、上へ 50 = 1 + 100 * 0.01 = 2 倍、均等に。
    d = BeginDrag(Tool::Scale, Handle::Free, f, cam, r, {200, 200});
    ch = Update(d, cam, r, {250, 150}, {});
    CHECK(Near(ch.scale, {2, 2}));
    ch = Update(d, cam, r, {0, 400}, {});
    CHECK(ch.scale.x >= 0.01f - 1e-6f);  // 1/100 より下にはならない
}

RM_TEST(Space2d_Spin2D_TurnsTheTransform2D) {
    using namespace game_production;
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_2d::Spin2D>();
    const reflect::TypeDesc& transform = reflect::TypeOf<Transform2D>();
    Scene s;
    s.SetComponentTypes({&transform, &spin}, {&spin});
    const int e = s.Create("turning");
    s.AddComponent(e, transform);
    s.AddComponent(e, spin);  // 1 秒に 90 度
    s.AddComponent(e, spin);
    static_cast<game_x_2d::Spin2D*>(s.Find(e)->FindComponent(spin.name, 1)->value.Get())->degreesPerSecond = -30;
    static const SystemInfo kSpin{"回り続ける (2D)", SystemPhase::Frame, 0, &game_x_2d::RunSpin2D};
    const SystemInfo* list[] = {&kSpin};
    SystemRunner runner;
    runner.SetSystems(list);
    runner.Advance(s, 0.5, 0.02f);
    const float& rot = static_cast<const Transform2D*>(s.Find(e)->FindComponent(transform.name)->value.Get())->rotation;
    CHECK(Near(rot, 30));  // (90 - 30) * 0.5: 2 つが足される
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
    // 1 単位のピクセル数 は 0 にならない (大きさをこれで割る)。
    json::Value zero = saved;
    zero.Set("pixelsPerUnit", 0.0);
    reflect::Load(back, zero, nullptr, nullptr);
    CHECK(back.pixelsPerUnit > 0);

    // ビューの状態: 知らないキーは書き戻す。ズームは範囲内に収まる。
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

RM_TEST(Space2d_GameCamera_WhatItSees) {
    game_x_2d::Camera2D cam;  // 真ん中から上下に 5 単位
    const Mat3 world = LocalMatrix(Make({3, -2}, 0, {1, 1}));
    const Camera2D view = game_x_2d::ViewOf(cam, world, 400);
    CHECK(Near(view.center, {3, -2}));
    CHECK(Near(view.zoom, 40));  // 400 DIP で 10 単位が見える
    const ScreenRect r{0, 0, 600, 400};
    CHECK(Near(ToScreen(view, r, {3, 3}), {300, 0}));     // 見えているものの上の端
    CHECK(Near(game_x_2d::HalfExtent(cam, 1.5f), {7.5f, 5}));
    cam.size = 0;  // (読み込んだあとは 0 にならない。それでも 0 で割らない)
    CHECK(std::isfinite(game_x_2d::ViewOf(cam, world, 400).zoom));
    const Color game{0.1f, 0.2f, 0.3f, 1};
    CHECK(game_x_2d::BackgroundOf(cam, game) == game);
    cam.ownBackground = true;
    cam.background = {1, 0, 0, 1};
    CHECK(game_x_2d::BackgroundOf(cam, game) == cam.background);
}

RM_TEST(Space2d_Camera_Turned) {
    Camera2D cam;
    cam.center = {1, 1};
    cam.zoom = 10;
    cam.rotation = 90;  // 反時計回りに回す: その左 (-X) にあるもの ... その上は平面の -X
    const ScreenRect r{0, 0, 200, 100};
    CHECK(Near(ToScreen(cam, r, {1, 1}), {100, 50}));
    CHECK(Near(ToScreen(cam, r, {0, 1}), {100, 40}));  // 平面の -X が画面の上
    CHECK(Near(ToScreen(cam, r, {1, 2}), {110, 50}));  // 平面の +Y が右
    CHECK(Near(ToWorld(cam, r, ToScreen(cam, r, {-3, 7})), {-3, 7}));
    CHECK(Near(TransformPoint(ScreenMatrix(cam, r), {-3, 7}), ToScreen(cam, r, {-3, 7})));
    // パンはまだマウスに付いてくる。ズームはまだマウスの下の点を保つ。
    const Vec2 before = ToScreen(cam, r, {0, 0});
    Pan(cam, 5, 7);
    CHECK(Near(ToScreen(cam, r, {0, 0}), {before.x + 5, before.y + 7}));
    const Vec2 under = ToWorld(cam, r, {20, 80});
    ZoomAt(cam, r, {20, 80}, 2);
    CHECK(Near(ToWorld(cam, r, {20, 80}), under));
    // ゲームカメラはエンティティと一緒に回る。
    game_x_2d::Camera2D gc;
    const Camera2D v = game_x_2d::ViewOf(gc, LocalMatrix(Make({0, 0}, 30, {2, 2})), 100);
    CHECK(Near(v.rotation, 30) && Near(v.zoom, 10));  // その拡大は見えるものを変えない
}

RM_TEST(Space2d_Gizmo_AboutTheMiddle) {
    using namespace gizmo2d;
    const std::vector<Vec2> places{{-2, 0}, {2, 0}, {0, 3}};
    gizmo2d::Frame own;
    own.axes = {Vec2{0, 1}, Vec2{-1, 0}};
    const gizmo2d::Frame mid = CenterFrame(places, own);
    CHECK(Near(mid.origin, {0, 1}) && Near(mid.axes[0], {0, 1}));  // 真ん中、最後のものの軸
    // 真ん中の周りに 90 回すと、物はその周りを回る (そして向きも回る)。
    const Transform2D a = Make({2, 1}, 10, {1, 1});
    Change turn;
    turn.degrees = 90;
    gizmo2d::Frame center;
    center.origin = {0, 1};
    const Transform2D ta = Apply(a, {}, LocalMatrix(a), turn, &center);
    CHECK(Near(ta.position, {0, 3}) && Near(ta.rotation, 100));
    CHECK(Near(Apply(a, {}, LocalMatrix(a), turn).position, {2, 1}));  // それぞれ: その場に留まる
    // 真ん中の周りで X に沿って 2 回拡大すると、X に沿った距離は 2 倍になる。
    Change grow;
    grow.scale = {2, 1};
    const Transform2D ga = Apply(a, {}, LocalMatrix(a), grow, &center);
    CHECK(Near(ga.position, {4, 1}) && Near(ga.scale, {2, 1}));
    // (10, 0) 動かした親の下: ワールドでの位置は回り、ローカルの位置は付いてくる。
    const Mat3 parent = Translation({10, 0});
    center.origin = {10, 0};
    const Transform2D child = Make({1, 0}, 0, {1, 1});
    CHECK(Near(Apply(child, parent, WorldMatrix(parent, child), turn, &center).position, {0, 1}));
}

RM_TEST(Space2d_Atlas_CellsAndAnimation) {
    // 128 x 32 の画像に、32 x 16 のセルが 4 x 2。
    CHECK((CellOf(128, 32, 4, 2, 0) == PixelRect{0, 0, 32, 16}));
    CHECK((CellOf(128, 32, 4, 2, 5) == PixelRect{32, 16, 32, 16}));
    CHECK((CellOf(128, 32, 4, 2, 8) == PixelRect{0, 0, 32, 16}));    // 最後を過ぎる: 先頭に戻る
    CHECK((CellOf(128, 32, 4, 2, -1) == PixelRect{96, 16, 32, 16}));  // 0 より下も同じ
    CHECK((CellOf(100, 50, 3, 1, 2) == PixelRect{66, 0, 33, 50}));   // 割り切れない: 端は越えない
    CHECK((CellOf(2, 2, 10, 10, 3) == PixelRect{0, 0, 2, 2}));       // ピクセルより多いセル: 全部
    CHECK((CellOf(64, 64, 0, -3, 0) == PixelRect{0, 0, 64, 64}));    // 最低でも 1 x 1
    // 1 秒に 4 つ、全部のセルを順に。決めた順序。最後で止まる。
    CHECK(AnimationCell({}, 3, 4, 0.0, true) == 0);
    CHECK(AnimationCell({}, 3, 4, 0.3, true) == 1);
    CHECK(AnimationCell({}, 3, 4, 0.8, true) == 0);  // 3 コマ: またひと回り
    const std::vector<int> frames{5, 2, 7};
    CHECK(AnimationCell(frames, 8, 2, 0.6, true) == 2);
    CHECK(AnimationCell(frames, 8, 2, 1.1, true) == 7);
    CHECK(AnimationCell(frames, 8, 2, 100, false) == 7);
    CHECK(AnimationCell(frames, 8, 0, 100, true) == 5);  // 速さ 0: そのまま
}

RM_TEST(Space2d_SpriteAnimation_StepsTheSprite) {
    using namespace game_production;
    const reflect::TypeDesc& sprite = reflect::TypeOf<game_x_2d::Sprite>();
    const reflect::TypeDesc& anim = reflect::TypeOf<game_x_2d::SpriteAnimation>();
    Scene s;
    s.SetComponentTypes({&sprite, &anim}, {});
    const int e = s.Create("walker");
    s.AddComponent(e, sprite);
    s.AddComponent(e, anim);
    auto& sp = *static_cast<game_x_2d::Sprite*>(s.Find(e)->FindComponent(sprite.name)->value.Get());
    sp.columns = 4;
    sp.rows = 1;
    static_cast<game_x_2d::SpriteAnimation*>(s.Find(e)->FindComponent(anim.name)->value.Get())->framesPerSecond = 10;
    static const SystemInfo kAnim{"スプライトのアニメーション", SystemPhase::Frame, 0, &game_x_2d::RunSpriteAnimation};
    const SystemInfo* list[] = {&kAnim};
    SystemRunner runner;
    runner.SetSystems(list);
    runner.Advance(s, 0.25, 0.02f);
    CHECK(sp.cell == 2);
    runner.Advance(s, 0.2, 0.02f);
    CHECK(sp.cell == 0);  // 4.5 セル: またひと回り
    // 時間は保存しない (Play のあとに保存したシーンは最初から始まる)。
    CHECK(reflect::Save(*static_cast<game_x_2d::SpriteAnimation*>(s.Find(e)->FindComponent(anim.name)->value.Get()), nullptr)
              .Get("time")
              .IsNull());
}
