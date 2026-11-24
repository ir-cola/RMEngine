// ゲーム UI プラグイン (plugins/rm_engine/game_ui) のテスト: アンカーとサイズ、キャンバスを
// 画面とそのセーフエリアに合わせる、マウスが乗っているもの (回転したものも)、ボタンの状態と
// クリック、キーのフォーカス、部品の保存、そしてスクリプトの部分。

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"
#include "rm_engine/game_ui/runtime/ScriptUi.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"
#include "rm_engine/game_ui/runtime/UiControls.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "rm_engine/game_ui/runtime/UiMotion.h"
#include "rm_engine/game_ui/runtime/UiService.h"
#include "rm_engine/game_ui/runtime/sdk/RMUIAbi.h"

#include <cmath>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::game_ui;
using game_production::InputFrame;
using game_production::Scene;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }
bool Near(const UiBox& a, const UiBox& b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.w, b.w) && Near(a.h, b.h); }

template <class T>
T* Add(Scene& s, int id) {
    s.AddComponent(id, reflect::TypeOf<T>());
    game_production::Component* c = s.Find(id)->FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

Scene UiScene() {
    Scene s;
    s.SetComponentTypes({&reflect::TypeOf<UiCanvas>(), &reflect::TypeOf<UiRect>(), &reflect::TypeOf<UiText>(), &reflect::TypeOf<UiImage>(),
                         &reflect::TypeOf<UiButton>(), &reflect::TypeOf<UiBar>(), &reflect::TypeOf<UiMotion>(), &reflect::TypeOf<UiPath>()},
                        {});
    return s;
}

// ボタン: UI の配置 は `pos` (中央から) に `size`、画像つき (マウスを受け取る)。
int MakeButton(Scene& s, int parent, Vec2 pos, Vec2 size, const char* name = "ボタン") {
    const int id = s.Create(name, parent);
    UiRect* r = Add<UiRect>(s, id);
    r->position = pos;
    r->size = size;
    Add<UiImage>(s, id);
    Add<UiButton>(s, id);
    return id;
}

}  // namespace

namespace {

// 親の中でのものの箱の角の位置 (その 中心 のまわりに回転した状態)、描かれたとおり。
std::array<Vec2, 4> SeenCorners(const UiRect& r, const UiBox& parent) {
    const UiBox b = PlaceBox(r, parent);
    const Affine m = About(PivotPoint(r, b), r.rotation, 1);
    return {Apply(m, {b.x, b.y}), Apply(m, {b.Right(), b.y}), Apply(m, {b.Right(), b.Bottom()}), Apply(m, {b.x, b.Bottom()})};
}
bool NearV(Vec2 a, Vec2 b) { return Near(a.x, b.x, 1e-2f) && Near(a.y, b.y, 1e-2f); }

}  // namespace

RM_TEST(GameUi_Handles_ResizeRotateAndPivot) {
    const UiBox parent{0, 0, 1000, 800};
    UiRect r;
    r.size = {200, 100};
    r.position = {30, -20};
    // 右のハンドル: 幅が 50 増え、左側は動かない。
    UiRect w = ResizeByHandle(r, parent, UiHandle::Right, {50, 0}, false);
    CHECK(Near(PlaceBox(w, parent).x, PlaceBox(r, parent).x) && Near(PlaceBox(w, parent).w, 250));
    // 左上の角を左上へ引いた: 右下の角は動かない。
    UiRect tl = ResizeByHandle(r, parent, UiHandle::TopLeft, {-20, -10}, false);
    CHECK(Near(PlaceBox(tl, parent).Right(), PlaceBox(r, parent).Right()) && Near(PlaceBox(tl, parent).Bottom(), PlaceBox(r, parent).Bottom()));
    CHECK(Near(PlaceBox(tl, parent).w, 220) && Near(PlaceBox(tl, parent).h, 110));
    // 形を保つ角: 200 x 100 -> 300 x 150 (大きいほうの変化で決まる)。
    UiRect a = ResizeByHandle(r, parent, UiHandle::BottomRight, {100, 10}, true);
    CHECK(Near(PlaceBox(a, parent).w, 300) && Near(PlaceBox(a, parent).h, 150));
    // 最小サイズを下回らない。
    UiRect tiny = ResizeByHandle(r, parent, UiHandle::Left, {500, 0}, false, 4);
    CHECK(Near(PlaceBox(tiny, parent).w, 4));
    // 30 度回転、中心 が真ん中からずれ、上に広げて伸ばした: ハンドルと反対の辺は、見えている
    // 位置のまま。
    UiRect t = r;
    t.rotation = 30;
    t.pivot = {0.2f, 0.7f};
    t.anchor = UiAnchor::StretchTop;
    t.size = {-600, 120};
    const auto before = SeenCorners(t, parent);
    const UiRect right = ResizeByHandle(t, parent, UiHandle::Right, {40, 0}, false);
    const auto afterRight = SeenCorners(right, parent);
    CHECK(NearV(afterRight[0], before[0]) && NearV(afterRight[3], before[3]));
    const UiRect bottom = ResizeByHandle(t, parent, UiHandle::Bottom, {0, 25}, false);
    const auto afterBottom = SeenCorners(bottom, parent);
    CHECK(NearV(afterBottom[0], before[0]) && NearV(afterBottom[1], before[1]));
    // 中心 を動かしても、見えている位置は保たれる (変わるのは 中心 と 位置 だけ)。
    const UiRect moved = MovePivot(t, parent, {0.9f, 0.1f});
    const auto afterPivot = SeenCorners(moved, parent);
    for (size_t i = 0; i < 4; ++i) CHECK(NearV(afterPivot[i], before[i]));
    CHECK(Near(moved.pivot.x, 0.9f) && Near(moved.size.x, t.size.x) && Near(moved.rotation, 30));
    // 中心 のまわりのドラッグで回す: 時計回りに 4 分の 1 (Y が下) は +90。その周りで刻む。
    CHECK(Near(RotateByDrag(10, {0, 0}, {1, 0}, {0, 1}), 100));
    CHECK(Near(RotateByDrag(0, {0, 0}, {1, 0}, {1, 0.4f}, 15), 15));
    CHECK(Near(RotateByDrag(170, {0, 0}, {1, 0}, {-1, -0.01f}), 170 - 179.427f, 0.01f));  // (短いほうの回り方)
}

RM_TEST(GameUi_Snap_ToTheNearestEdgeOrGrid) {
    // 10..110 の箱 (真ん中は 60): 届く範囲で一番近い目標が勝つ - その辺のどれか、または真ん中。
    const float edges[] = {10, 60, 110};
    const float targets[] = {0, 57, 200};
    std::optional<float> line;
    CHECK(Near(SnapShift(edges, targets, 5, &line), -3) && line && Near(*line, 57));  // 真ん中 60 -> 57
    const float far[] = {300, 400};
    CHECK(SnapShift(edges, far, 5, &line) == 0 && !line);  // 近くにない: ずらさない
    const float exact[] = {110};
    CHECK(SnapShift(edges, exact, 5, &line) == 0 && line && Near(*line, 110));  // もう合っている: そのまま、線は出る
    // グリッド: 一番近い倍数へ。
    CHECK(Near(GridShift(23, 10), -3) && Near(GridShift(26, 10), 4) && Near(GridShift(-14, 10), 4) && Near(GridShift(-16, 10), -4) && GridShift(7, 0) == 0);
}

RM_TEST(GameUi_AlignAndDistribute) {
    const UiBox boxes[] = {{10, 0, 20, 10}, {50, 30, 40, 20}, {200, 5, 10, 10}};
    const auto left = AlignShifts(boxes, UiAlign::Left);
    CHECK(Near(left[0].x, 0) && Near(left[1].x, -40) && Near(left[2].x, -190) && Near(left[1].y, 0));
    const auto right = AlignShifts(boxes, UiAlign::Right);  // to 210
    CHECK(Near(right[0].x, 180) && Near(right[1].x, 120) && Near(right[2].x, 0));
    const auto mid = AlignShifts(boxes, UiAlign::CenterY);  // すべて 0 から 50: 真ん中は 25
    CHECK(Near(mid[0].y, 20) && Near(mid[1].y, -15) && Near(mid[2].y, 15) && Near(mid[0].x, 0));
    // 等間隔: 10 から 210 で 70 が埋まっている -> 間隔は 65。最初と最後は動かない。
    const auto spread = DistributeShifts(boxes, true);
    CHECK(Near(spread[0].x, 0) && Near(spread[2].x, 0) && Near(boxes[1].x + spread[1].x, 10 + 20 + 65));
    // 軸に沿った順で、渡された順には関係しない。
    const UiBox mixed[] = {{200, 0, 10, 10}, {10, 0, 10, 10}, {50, 0, 10, 10}};
    const auto m = DistributeShifts(mixed, true);
    CHECK(Near(m[0].x, 0) && Near(m[1].x, 0) && Near(mixed[2].x + m[2].x, 105));
    // 2 つ以下: 広げるものはない。
    CHECK(DistributeShifts(std::span(boxes, 2), true)[1].x == 0);
}

RM_TEST(GameUi_AnchorGrid_RehomeAndOneCanvas) {
    // すべてのアンカーにセルがあり、セルがそれを返す。
    for (int a = 0; a <= static_cast<int>(UiAnchor::Stretch); ++a) {
        int row = -1, col = -1;
        AnchorCell(static_cast<UiAnchor>(a), row, col);
        CHECK(row >= 0 && row < 4 && col >= 0 && col < 4 && AnchorAt(row, col) == static_cast<UiAnchor>(a));
    }
    CHECK(AnchorAt(0, 0) == UiAnchor::TopLeft && AnchorAt(3, 3) == UiAnchor::Stretch && AnchorAt(2, 3) == UiAnchor::StretchBottom);
    // 別の親の下: 元の大きさのまま、中心 は頼んだ所に。
    UiRect r;
    r.anchor = UiAnchor::StretchTop;
    r.size = {-100, 50};
    r.pivot = {0.5f, 0.5f};
    const UiBox oldParent{0, 0, 800, 600}, newParent{100, 100, 300, 200};
    const UiRect moved = Rehomed(r, oldParent, newParent, {200, 150}, 12);
    const UiBox before = PlaceBox(r, oldParent), after = PlaceBox(moved, newParent);
    CHECK(Near(after.w, before.w) && Near(after.h, before.h) && moved.anchor == UiAnchor::StretchTop && Near(moved.rotation, 12));
    const Vec2 pv = PivotPoint(moved, after);
    CHECK(Near(pv.x, 200) && Near(pv.y, 150));
    // キャンバス 1 つだけ: そのもの、親は小さくなった一覧から見つけ直される。
    Scene s = UiScene();
    const int a = s.Create("A");
    Add<UiCanvas>(s, a);
    const int b = s.Create("B");
    Add<UiCanvas>(s, b);
    const int inB = s.Create("in B", b);
    Add<UiRect>(s, inB);
    const int deep = s.Create("deep", inB);
    Add<UiRect>(s, deep);
    const UiTree all = BuildUi(s, UiScreen{});
    const UiTree only = OnlyCanvas(all, b);
    CHECK(only.nodes.size() == 3 && only.nodes[0].entity == b && only.nodes[0].parent == -1);
    CHECK(only.Find(deep) && only.Find(deep)->parent >= 0 && only.nodes[static_cast<size_t>(only.Find(deep)->parent)].entity == inB);
    CHECK(!only.Find(a));
}

RM_TEST(GameUi_Reanchored_KeepsTheBox) {
    // インスペクターの アンカー: ものは画面上の同じ場所に留まる (どのプリセットへも、戻しても)。
    const UiBox parent{0, 0, 1920, 1080};
    UiRect r;  // 真ん中
    r.position = {174, 366};
    r.size = {1200, 80};
    r.pivot = {0.3f, 0.5f};
    r.rotation = 15;
    const UiBox box = PlaceBox(r, parent);
    for (int a = 0; a <= static_cast<int>(UiAnchor::Stretch); ++a) {
        const UiRect moved = Reanchored(r, static_cast<UiAnchor>(a), parent);
        CHECK(moved.anchor == static_cast<UiAnchor>(a));
        CHECK(Near(PlaceBox(moved, parent), box));
        CHECK(Near(moved.pivot.x, 0.3f) && Near(moved.rotation, 15));  // (残りは元のまま)
        const UiRect back = Reanchored(moved, UiAnchor::Center, parent);
        CHECK(Near(back.position.x, 174) && Near(back.position.y, 366) && Near(back.size.x, 1200) && Near(back.size.y, 80));
    }
    // 右下へ: 位置 は今その角から測る (角より左と上は -)。
    const UiRect corner = Reanchored(r, UiAnchor::BottomRight, parent);
    CHECK(corner.position.x < 0 && corner.position.y < 0 && Near(corner.size.x, 1200));
    // ストレッチへ: サイズは、箱が親とどれだけ違うか。
    const UiRect wide = Reanchored(r, UiAnchor::Stretch, parent);
    CHECK(Near(wide.size.x, 1200 - 1920) && Near(wide.size.y, 80 - 1080));
}

RM_TEST(GameUi_Anchors_PlaceTheBox) {
    const UiBox parent{0, 0, 1920, 1080};
    UiRect r;  // 真ん中、240 x 64
    r.position = {10, 20};
    r.size = {200, 60};
    CHECK(Near(PlaceBox(r, parent), {870, 530, 200, 60}));
    // 左上に固定、中心 は自分の左上: 位置 は角のある場所。
    r.anchor = UiAnchor::TopLeft;
    r.pivot = {0, 0};
    r.position = {16, 16};
    r.size = {100, 50};
    CHECK(Near(PlaceBox(r, parent), {16, 16, 100, 50}));
    // 右下で 中心 もそこ: 箱は角の左上に来る。
    r.anchor = UiAnchor::BottomRight;
    r.pivot = {1, 1};
    r.position = {-16, -16};
    CHECK(Near(PlaceBox(r, parent), {1920 - 16 - 100, 1080 - 16 - 50, 100, 50}));
    // ストレッチ: 親の幅 / 高さ全体で、サイズは加算される (ここでは、各辺から 20 内側)。
    r.anchor = UiAnchor::Stretch;
    r.pivot = {0.5f, 0.5f};
    r.position = {0, 0};
    r.size = {-40, -40};
    CHECK(Near(PlaceBox(r, parent), {20, 20, 1880, 1040}));
    // 上だけにストレッチ: 幅は親に従い、高さはサイズ。
    r.anchor = UiAnchor::StretchTop;
    r.pivot = {0.5f, 0};
    r.size = {0, 100};
    CHECK(Near(PlaceBox(r, {100, 50, 800, 600}), {100, 50, 800, 100}));
    // 0 より小さくはならない。
    r.size = {-5000, -500};
    CHECK(PlaceBox(r, parent).w == 0 && PlaceBox(r, parent).h == 0);
    // すべてのプリセットは 0..1 の中の範囲。
    for (int a = 0; a <= static_cast<int>(UiAnchor::Stretch); ++a) {
        Vec2 mn, mx;
        AnchorRange(static_cast<UiAnchor>(a), mn, mx);
        CHECK(mn.x >= 0 && mn.y >= 0 && mx.x <= 1 && mx.y <= 1 && mn.x <= mx.x && mn.y <= mx.y);
    }
}

RM_TEST(GameUi_Canvas_FitsTheScreen) {
    const Vec2 ref{1920, 1080};
    CHECK(Near(CanvasScale(UiScaleMode::Fit, ref, {1280, 720}), 1280.f / 1920.f));
    CHECK(Near(CanvasScale(UiScaleMode::Fit, ref, {1280, 1024}), 1280.f / 1920.f));  // 狭いほうの向き
    CHECK(Near(CanvasScale(UiScaleMode::Fill, ref, {1280, 1024}), 1024.f / 1080.f));
    CHECK(Near(CanvasScale(UiScaleMode::Width, ref, {960, 2000}), 0.5f));
    CHECK(Near(CanvasScale(UiScaleMode::Height, ref, {960, 540}), 0.5f));
    CHECK(Near(CanvasScale(UiScaleMode::None, ref, {960, 540}), 1));
    CHECK(CanvasScale(UiScaleMode::Fit, {0, 0}, {0, 0}) > 0);  // 0 にはならない (割り算に使う)
    // セーフエリア: 余白を引く。0 を下回らない。
    CHECK(Near(SafeBox({1280, 720}, {40, 20}, {40, 30}), {40, 20, 1200, 670}));
    CHECK(Near(SafeBox({100, 100}, {80, 0}, {80, 0}), {80, 0, 0, 100}));
    // その中のキャンバス: 640 x 360 の中の 1920 x 1080 は 3 分の 1。
    Scene s = UiScene();
    const int canvas = s.Create("HUD");
    Add<UiCanvas>(s, canvas);
    const int b = MakeButton(s, canvas, {0, 0}, {300, 150});
    UiScreen screen;
    screen.size = {640, 360};
    const UiTree t = BuildUi(s, screen);
    const UiNode* n = t.Find(b);
    CHECK(n != nullptr);
    if (n) CHECK(Hits(*n, {320, 360 * 0.5f}) && Hits(*n, {320 + 49, 180 + 24}) && !Hits(*n, {320 + 51, 180}));
    // 左に 20 のセーフエリアがある (キャンバスはその内側に収めると言っている): 箱は右へ動く。
    screen.safeTopLeft = {20, 0};
    const UiTree safe = BuildUi(s, screen);
    if (const UiNode* m = safe.Find(b)) CHECK(Hits(*m, {330 + 45, 180}) && !Hits(*m, {330 - 52, 180}));
}

RM_TEST(GameUi_Tree_OrderAndWhatIsShown) {
    Scene s = UiScene();
    const int back = s.Create("Back");
    Add<UiCanvas>(s, back)->order = 5;
    const int front = s.Create("Front");
    Add<UiCanvas>(s, front)->order = 1;
    const int a = MakeButton(s, back, {0, 0}, {100, 100}, "a");
    const int b = MakeButton(s, back, {0, 0}, {100, 100}, "b");
    s.Find(a)->FindComponent(reflect::TypeOf<UiRect>().name);
    static_cast<UiRect*>(s.Find(a)->FindComponent(reflect::TypeOf<UiRect>().name)->value.Get())->order = 3;  // a が b の前
    const UiTree t = BuildUi(s, {});
    // キャンバスは 手前の順 で (Front、1、最初)。そのあと b が a の前 (a の順のほうが大きい)。
    std::vector<int> order;
    for (const UiNode& n : t.nodes) order.push_back(n.entity);
    CHECK((order == std::vector<int>{front, back, b, a}));
    CHECK(TopAt(t, {640, 360}) == a);  // 一番手前
    // オフにしたキャンバスはない - 作っているもの (UI のビュー) を持つ場合を除く。
    s.Find(back)->data.active = false;
    CHECK(BuildUi(s, {}).Find(a) == nullptr);
    const int show[] = {a};
    CHECK(BuildUi(s, {}, show).Find(a) != nullptr);
    // オフにしたものは、その下にあるものと一緒に消える。
    s.Find(back)->data.active = true;
    s.Find(b)->data.active = false;
    CHECK(BuildUi(s, {}).Find(b) == nullptr && BuildUi(s, {}).Find(a) != nullptr);
}

RM_TEST(GameUi_Turned_WhatTheMouseIsOn) {
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    UiCanvas* c = Add<UiCanvas>(s, canvas);
    c->reference = {1280, 720};
    const int b = MakeButton(s, canvas, {0, 0}, {400, 40});
    static_cast<UiRect*>(s.Find(b)->FindComponent(reflect::TypeOf<UiRect>().name)->value.Get())->rotation = 90;  // 立てた
    const UiTree t = BuildUi(s, {});
    const UiNode* n = t.Find(b);
    CHECK(n != nullptr);
    if (!n) return;
    CHECK(Hits(*n, {640, 360 + 150}) && !Hits(*n, {640 + 150, 360}));  // 今は縦に長く、横に細い
    // ボタンの上のラベル: ラベルの上のマウスはボタンの上。
    const int label = s.Create("文字", b);
    UiRect* lr = Add<UiRect>(s, label);
    lr->anchor = UiAnchor::Stretch;
    lr->size = {0, 0};
    Add<UiText>(s, label);
    const UiTree t2 = BuildUi(s, {});
    CHECK(ButtonAt(t2, {640, 360}) == b);
    CHECK(ButtonOf(t2, label) == b);
}

RM_TEST(GameUi_Buttons_StatesAndClicks) {
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int b = MakeButton(s, canvas, {0, 0}, {200, 100});
    const int other = MakeButton(s, canvas, {400, 0}, {200, 100});
    UiTree t = BuildUi(s, {});
    PointerState ps;
    const Vec2 on{640, 360}, off{100, 100}, onOther{1040, 360};
    // 同じボタンで押して離した: クリック。
    PointerResult r = StepPointer(t, on, true, true, false, ps);
    CHECK(r.hovered == b && r.held == b && r.clicked == 0);
    r = StepPointer(t, on, false, false, true, ps);
    CHECK(r.clicked == b && ps.pressed == 0);
    // そこで押して、別の所で離した: クリックなし。
    StepPointer(t, on, true, true, false, ps);
    r = StepPointer(t, onOther, true, false, false, ps);
    CHECK(r.hovered == other && r.held == 0);  // (押されていない: マウスが離れた)
    r = StepPointer(t, onOther, false, false, true, ps);
    CHECK(r.clicked == 0);
    // 押せない: ホバーもクリックもない - それでも下にあるものは隠す。
    auto* button = static_cast<UiButton*>(s.Find(b)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    button->interactable = false;
    t = BuildUi(s, {});
    StepPointer(t, on, true, true, false, ps);
    r = StepPointer(t, on, false, false, true, ps);
    CHECK(r.hovered == 0 && r.clicked == 0);
    CHECK(TopAt(t, on) == b);
    // 見た目: 状態ごとの色とサイズ。フォーカス中は 乗った と同じ見た目。
    button->interactable = true;
    button->state = static_cast<int>(UiButtonState::Pressed);
    CHECK(ButtonTarget(*button).tint == button->pressed && Near(ButtonScale(*button), button->pressedScale));
    button->state = static_cast<int>(UiButtonState::Normal);
    button->focused = true;
    CHECK(ButtonTarget(*button).tint == button->hover && Near(ButtonScale(*button), button->hoverScale));
    button->interactable = false;
    CHECK(ButtonTarget(*button).tint == button->disabled && Near(ButtonScale(*button), 1));
    // バーの満たされた部分。
    UiBar bar;
    bar.value = 0.25f;
    bar.padding = 0;
    CHECK(Near(BarFill(bar, {0, 0, 100, 10}), {0, 0, 25, 10}));
    bar.direction = UiBarDirection::RightToLeft;
    CHECK(Near(BarFill(bar, {0, 0, 100, 10}), {75, 0, 25, 10}));
    bar.direction = UiBarDirection::BottomToTop;
    bar.value = 2;  // (1 に収める)
    bar.padding = 2;
    CHECK(Near(BarFill(bar, {0, 0, 100, 10}), {2, 2, 96, 6}));
}

RM_TEST(GameUi_Keys_MoveTheFocusAndPress) {
    using game_production::InputControl;
    Scene s = UiScene();
    const int canvas = s.Create("Menu");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    // グリッド: tl tr / bl br。
    const int tl = MakeButton(s, canvas, {-200, -100}, {100, 50}, "tl");
    const int tr = MakeButton(s, canvas, {200, -100}, {100, 50}, "tr");
    const int bl = MakeButton(s, canvas, {-200, 100}, {100, 50}, "bl");
    const int br = MakeButton(s, canvas, {200, 120}, {100, 50}, "br");
    const UiTree t = BuildUi(s, {});
    CHECK(NavigationCanvas(t) == canvas);
    CHECK(FirstFocus(t, canvas, 0) == tl);
    CHECK(FirstFocus(t, canvas, br) == br);  // キャンバスの 最初に選ぶボタン
    // 単独では: 画面上でその向きに一番近いボタン (ボタンには何も設定していない)。
    CHECK(NextButton(t, canvas, tl, 1, 0) == tr);
    CHECK(NextButton(t, canvas, tl, 0, 1) == bl);
    CHECK(NextButton(t, canvas, tr, 0, 1) == br);
    CHECK(NextButton(t, canvas, tl, -1, 0) == tl);  // その向きには何もない: 動かない

    // アクション (UIUp ... UICancel) を通してだけ読む: パッドの十字、左スティック、矢印キー。
    const std::vector<game_production::InputAction> actions = game_production::DefaultInputActions();
    auto pad = [](InputControl c, float v) {
        InputFrame f;
        f.pad[static_cast<size_t>(static_cast<int>(c) - game_production::kPadControlBase)] = v;
        return f;
    };
    InputFrame none;
    CHECK(ReadUiKeys(actions, pad(InputControl::PadRight, 1), none).dx == 1);
    CHECK(ReadUiKeys(actions, pad(InputControl::PadLeftStickY, 0.9f), none).dy == -1);  // スティックを上へ: 画面の上へ
    CHECK(ReadUiKeys(actions, pad(InputControl::PadLeftStickX, -0.8f), none).dx == -1);
    CHECK(ReadUiKeys(actions, pad(InputControl::PadLeftStickX, 0.3f), none).dx == 0);  // (足りない)
    CHECK(ReadUiKeys(actions, pad(InputControl::PadA, 1), none).submit && ReadUiKeys(actions, pad(InputControl::PadB, 1), none).cancel);
    InputFrame keys;
    keys.keys[0x0D] = 1;  // Enter
    keys.keys[0x27] = 1;  // 右矢印
    UiKeys k = ReadUiKeys(actions, keys, none);
    CHECK(k.submit && k.dx == 1 && k.dy == 0);
    CHECK(ReadUiKeys(actions, keys, keys).dx == 0 && ReadUiKeys(actions, keys, keys).holdDx == 1);  // 押しっぱなしで、押し直しではない
    // 斜めのスティックは、強いほうの向きだけになる。
    InputFrame aslant = pad(InputControl::PadLeftStickX, 0.9f);
    aslant.pad[static_cast<size_t>(static_cast<int>(InputControl::PadLeftStickY) - game_production::kPadControlBase)] = 0.6f;
    k = ReadUiKeys(actions, aslant, none);
    CHECK(k.dx == 1 && k.dy == 0);
    // UI のアクションができる前のゲーム (入力にない): その標準のもの。
    CHECK(ReadUiKeys({}, keys, none).submit && ReadUiKeys({}, keys, none).dx == 1);
    // 割り当てを変えた: UI はそれに従う (UISubmit は Z だけ - Enter は今は何もしない)。
    std::vector<game_production::InputAction> changed = actions;
    for (auto& a : changed) {
        if (a.name == game_production::kUiSubmit) a.bindings = {{InputControl::Z, 1}};
    }
    CHECK(!ReadUiKeys(changed, keys, none).submit);
    InputFrame z;
    z.keys['Z'] = 1;
    CHECK(ReadUiKeys(changed, z, none).submit);

    // フレームを通しで、パッドだけ: ウィンドウには常に選ばれたボタンがあり、最初から見える - A で押す。
    UiFocus state;
    const InputFrame a = pad(InputControl::PadA, 1), right = pad(InputControl::PadRight, 1);
    UiStep step = StepUi(s, t, std::nullopt, false, false, false, actions, &a, &none, 0.016f, state);
    CHECK((step.clicked == std::vector<int>{tl}));
    const auto* tlButton = static_cast<const UiButton*>(s.Find(tl)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(tlButton->focused);
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &right, &none, 0.016f, state);
    CHECK(state.focus == tr && step.clicked.empty());
    // 押しっぱなし: すぐには進まず、kRepeatDelay のあと 1 歩、そのあとは kRepeatEvery ごと (ここでは 2 つのボタンを行き来)。
    const InputFrame down = pad(InputControl::PadDown, 1);
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &none, 0.016f, state);
    CHECK(state.focus == br);
    const InputFrame up = pad(InputControl::PadUp, 1);
    StepUi(s, t, std::nullopt, false, false, false, actions, &up, &none, 0.016f, state);
    CHECK(state.focus == tr);
    StepUi(s, t, std::nullopt, false, false, false, actions, &up, &up, 0.2f, state);
    CHECK(state.focus == tr);  // (0.2 秒: まだ。それに tr の上には何もない)
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &up, 0.016f, state);
    CHECK(state.focus == br);
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &down, 0.3f, state);
    CHECK(state.focus == br);  // 0.3 秒押した
    // マウスが動いた: 選択は隠れる。次の 1 歩で、また見える。
    StepUi(s, t, Vec2{10, 10}, false, false, false, actions, &none, &none, 0.016f, state);
    StepUi(s, t, Vec2{20, 20}, false, false, false, actions, &none, &none, 0.016f, state);
    const auto* brButton = static_cast<const UiButton*>(s.Find(br)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(!state.keyboard && !brButton->focused);
    StepUi(s, t, std::nullopt, false, false, false, actions, &right, &none, 0.016f, state);
    CHECK(state.keyboard && state.focus == br && brButton->focused);
}

RM_TEST(GameUi_Keys_RepeatWhileHeld) {
    using game_production::InputControl;
    Scene s = UiScene();
    const int canvas = s.Create("List");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    std::vector<int> rows;
    for (int i = 0; i < 6; ++i) rows.push_back(MakeButton(s, canvas, {0, -250.f + 100.f * static_cast<float>(i)}, {200, 60}));
    const UiTree t = BuildUi(s, {});
    const auto actions = game_production::DefaultInputActions();
    InputFrame none, down;
    down.keys[0x28] = 1;  // VK_DOWN
    UiFocus state;
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &none, 0.016f, state);
    CHECK(state.focus == rows[1]);  // 最初の押下: 1 歩
    float held = 0;
    int steps = 0;
    int last = state.focus;
    while (held < kRepeatDelay + 2.5f * kRepeatEvery) {  // 約 0.7 秒の押しっぱなし、1 フレーム 10 ms
        StepUi(s, t, std::nullopt, false, false, false, actions, &down, &down, 0.01f, state);
        held += 0.01f;
        if (state.focus != last) {
            ++steps;
            last = state.focus;
            CHECK(held >= kRepeatDelay - 1e-3f);  // ディレイの前は何もない
        }
    }
    CHECK(steps == 3);  // ディレイで 1 歩、そのあとさらに 2 歩
}

RM_TEST(GameUi_Cancel_ClosesTheWindowOpenedLast) {
    Scene s = UiScene();
    const int hud = s.Create("HUD");
    Add<UiCanvas>(s, hud);
    MakeButton(s, hud, {0, 0}, {100, 50});
    const int pause = s.Create("Pause");
    Add<UiCanvas>(s, pause)->order = 1;
    const int resume = MakeButton(s, pause, {0, 0}, {100, 50});
    s.Find(pause)->data.active = false;
    const int options = s.Create("Options");
    Add<UiCanvas>(s, options)->order = 2;
    MakeButton(s, options, {0, 0}, {100, 50});
    s.Find(options)->data.active = false;
    const auto actions = game_production::DefaultInputActions();
    InputFrame none, esc;
    esc.keys[0x1B] = 1;
    UiFocus state;
    auto frame = [&](const InputFrame& now) { return StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &now, &none, 0.016f, state); };
    // 最初は HUD だけ: 戻る は何も閉じない (別のウィンドウの上に開いたものではないので)。
    CHECK(frame(esc).closed == 0 && s.Find(hud)->data.active);
    // HUD の上に Pause を出す (スクリプトの Show): 戻る で Pause が閉じ、HUD は残る。
    s.Find(pause)->data.active = true;
    frame(none);
    CHECK(state.focus == resume);  // 新しいウィンドウの最初のボタンが選ばれる
    CHECK(frame(esc).closed == pause && !s.Find(pause)->data.active && s.Find(hud)->data.active);
    frame(none);
    CHECK(frame(esc).closed == 0);  // 最初に戻った: もう閉じるものはない
    // HUD の代わりに Options (SwitchTo): 戻る は Options を閉じて、また HUD を見せる。
    s.Find(hud)->data.active = false;
    s.Find(options)->data.active = true;
    frame(none);
    CHECK(frame(esc).closed == options && s.Find(hud)->data.active && !s.Find(options)->data.active);
    frame(none);
    CHECK(state.visits.empty());
    // 戻るで押すボタン のあるウィンドウ: 戻る は代わりにそれを押す。
    s.Find(pause)->data.active = true;
    static_cast<UiCanvas*>(s.Find(pause)->FindComponent(reflect::TypeOf<UiCanvas>().name)->value.Get())->cancelButton.id = resume;
    frame(none);
    const UiStep st = frame(esc);
    CHECK((st.clicked == std::vector<int>{resume}) && st.closed == 0 && s.Find(pause)->data.active);
}

RM_TEST(GameUi_Parts_SaveAndLoad) {
    UiRect r;
    r.anchor = UiAnchor::StretchBottom;
    r.position = {12, -8};
    r.size = {-40, 96};
    r.rotation = 15;
    const json::Value saved = reflect::Save(r, nullptr);
    CHECK(saved.Get("anchor").AsString() == "StretchBottom");  // 列挙型は名前で
    UiRect back;
    reflect::Load(back, saved, nullptr, nullptr);
    CHECK(back.anchor == r.anchor && back.position == r.position && back.size == r.size && back.rotation == 15);
    // ボタンのフレームごとの状態は決して保存しない。
    UiButton b;
    b.state = 2;
    b.focused = true;
    const json::Value bs = reflect::Save(b, nullptr);
    CHECK(bs.Get("state").IsNull() && bs.Get("focused").IsNull());
    // 新しいキーのあるテキスト: 書き戻される。
    UiText t;
    t.text = "スコア 0";
    json::Value ts = reflect::Save(t, nullptr);
    ts.Set("gradient", true);
    UiText t2;
    reflect::Unknowns unknowns;
    reflect::Load(t2, ts, &unknowns, nullptr);
    CHECK(t2.text == "スコア 0" && reflect::Save(t2, &unknowns).Get("gradient").AsBool());
    // 値は範囲に収める: バーの値。
    UiBar bar;
    json::Value bv = reflect::Save(bar, nullptr);
    bv.Set("value", 7.0);
    reflect::Load(bar, bv, nullptr, nullptr);
    CHECK(bar.value <= 1);
}

RM_TEST(GameUi_Scripts_TheirPart) {
    Scene s = UiScene();
    const int title = s.Create("Title");
    Add<UiCanvas>(s, title);
    const int hud = s.Create("HUD");
    Add<UiCanvas>(s, hud);
    const int label = s.Create("score", hud);
    Add<UiRect>(s, label);
    Add<UiText>(s, label);
    const int hp = s.Create("hp", hud);
    Add<UiRect>(s, hp);
    Add<UiBar>(s, hp);
    game_production::ScriptWorld world;
    world.scene = &s;
    const auto* api = static_cast<const RmUiApi*>(ScriptUiPart().table);
    CHECK(api != nullptr);
    if (!api) return;
    CHECK(api->setText(&world, label, "スコア 10") == 1);
    char buf[64];
    CHECK(api->getText(&world, label, buf, sizeof(buf)) == std::string("スコア 10").size() && std::string(buf) == "スコア 10");
    CHECK(api->getText(&world, label, buf, 4) == std::string("スコア 10").size() && std::string(buf).size() == 3);  // 入るように切る
    CHECK(api->setBar(&world, hp, 0.4f) == 1 && Near(api->getBar(&world, hp), 0.4f));
    CHECK(api->setBar(&world, hp, 9) == 1 && Near(api->getBar(&world, hp), 1));
    CHECK(api->setBar(&world, label, 0.5f) == 0 && api->getBar(&world, label) == -1);  // バーではない
    // 名前でウィンドウ: 切り替えると 1 つが出て、ほかは隠れる。
    CHECK(api->switchWindow(&world, "HUD") == 1);
    CHECK(api->windowShown(&world, "HUD") == 1 && api->windowShown(&world, "Title") == 0);
    CHECK(api->showWindow(&world, "Title", 1) == 1 && api->windowShown(&world, "Title") == 1);
    CHECK(api->switchWindow(&world, "Nowhere") == 0);  // (コンソールに 1 回だけ知らせる)
    // ボタンが呼ぶフック名は OnClick (ABI 6)。
    static_assert(RM_ABI_VERSION >= 6u && RM_HOOK_CLICK == 7);
}

RM_TEST(GameUi_QuickClick_InOneFrame) {
    // フレームより速いクリック (コンピューター操作、タッチパッドのタップ): ボタンはどちらの
    // 確認でも上がっているが、ウィンドウには下がって上がったと伝わった - それはクリック。
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int b = MakeButton(s, canvas, {0, 0}, {200, 100});
    const UiTree t = BuildUi(s, {});
    PointerState ps;
    PointerResult r = StepPointer(t, Vec2{640, 360}, false, true, true, ps);
    CHECK(r.clicked == b && ps.pressed == 0 && r.held == 0);
    // ... ボタンの外: 何もしない。そして次のフレームに押されたまま残るものもない。
    r = StepPointer(t, Vec2{50, 50}, false, true, true, ps);
    CHECK(r.clicked == 0 && ps.pressed == 0);
    // UI のフレームを通しで (サービスがウィンドウの押下をどう扱うか)。
    UiFocus state;
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    const UiStep step = StepUi(s, t, Vec2{640, 360}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK((step.clicked == std::vector<int>{b}));
    // そのあと、ボタンは通常の見た目 (押されたまま残らない)。
    const auto* button = static_cast<const UiButton*>(s.Find(b)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(button->state != static_cast<int>(UiButtonState::Pressed));
}

RM_TEST(GameUi_ButtonLook_EasedOutlineAndSounds) {
    UiButton b;
    // イージングはフレームレートに依らない: 0.01 秒を 10 フレーム = 0.1 秒を 1 フレーム。
    UiButtonLook a, c;
    b.state = static_cast<int>(UiButtonState::Hover);
    const UiButtonLook hover = ButtonTarget(b);
    CHECK(Near(hover.scale, b.hoverScale) && Near(hover.outlineAmount, 1) && Near(hover.outline, b.hoverOutline));
    for (int i = 0; i < 10; ++i) a = EaseLook(a, hover, EaseAmount(0.01f, 0.08f));
    c = EaseLook(c, hover, EaseAmount(0.1f, 0.08f));
    CHECK(Near(a.scale, c.scale, 1e-4f) && Near(a.outlineAmount, c.outlineAmount, 1e-4f) && Near(a.tint.r, c.tint.r, 1e-4f));
    CHECK(a.scale > 1 && a.scale < b.hoverScale);
    // 0 秒: すぐ。十分近い: 目標に着く (髪の毛ほどの線は残らない)。
    CHECK(EaseLook({}, hover, EaseAmount(0.016f, 0)).scale == hover.scale);
    UiButtonLook almost = hover;
    almost.outlineAmount = 0.99995f;
    CHECK(EaseLook(almost, hover, 0.5f).outlineAmount == 1.f);
    // 輪郭はすぐに、自分の太さで見える (増えるのは量だけ) ...
    CHECK(Near(a.outline, b.hoverOutline));
    // ... 通常に戻るときはフェードアウトし、細くはならず、白に向かうこともない。
    b.hoverOutlineColor = {1, 0, 0, 1};
    UiButtonLook now = ButtonTarget(b);
    b.state = static_cast<int>(UiButtonState::Normal);
    const UiButtonLook normal = ButtonTarget(b);
    CHECK(normal.outlineAmount == 0);
    float lastAlpha = 1;
    for (int i = 0; i < 60; ++i) {  // (変わる速さの約 12 倍: 目標に着く)
        now = EaseLook(now, normal, EaseAmount(0.016f, 0.08f));
        const UiStroke s = ButtonOutline(now);
        CHECK(Near(s.width, b.hoverOutline));
        CHECK(s.color.r == 1 && s.color.g == 0 && s.color.a <= lastAlpha);
        lastAlpha = s.color.a;
    }
    CHECK(now.outlineAmount == 0 && ButtonOutline(now).color.a == 0);
    // 自分の輪郭を持つもの: それと混ぜる (太さ 3: 3 と状態の 2 の間)。
    now.outlineAmount = 0.5f;
    CHECK(Near(ButtonOutline(now, {3, {0, 0, 0, 1}}).width, 2.5f));

    // 掛ける はより明るくできない。置き換える はその色そのものを返す。
    b.state = static_cast<int>(UiButtonState::Hover);
    b.hover = {0.2f, 0.9f, 1, 1};
    const Color red{1, 0, 0, 1};
    CHECK(Paint(red, PaintOf(ButtonTarget(b))) == Color(0.2f, 0, 0, 1));
    b.colorMode = UiButtonColorMode::Replace;
    const Color replaced = Paint(red, PaintOf(ButtonTarget(b)));
    CHECK(Near(replaced.r, 0.2f) && Near(replaced.g, 0.9f) && Near(replaced.b, 1) && Near(replaced.a, 1));
    b.state = static_cast<int>(UiButtonState::Normal);
    CHECK(IsPlain(PaintOf(ButtonTarget(b))) && Paint(red, PaintOf(ButtonTarget(b))) == red);
    // 差し替えた画像: ホバーのもの。画像なしで押した: これもホバーのもの。通常: 自分のもの。
    const AssetRef own{"0123456789abcdef0123456789abcd01"}, lit{"0123456789abcdef0123456789abcd02"};
    b.hoverImage = lit;
    CHECK(ButtonImage(b, own) == own);
    b.state = static_cast<int>(UiButtonState::Pressed);
    CHECK(ButtonImage(b, own) == lit);
    b.interactable = false;
    CHECK(ButtonImage(b, own) == own);

    // フレームを通しで: 見た目はサービスがイージングし、音のタイミングは 1 回 (最初のフレームではない)。
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int id = MakeButton(s, canvas, {0, 0}, {200, 100});
    const UiTree t = BuildUi(s, {});
    UiFocus state;
    state.keyboard = false;
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    const Vec2 on{640, 360}, off{50, 50};
    UiStep step = StepUi(s, t, off, false, false, false, actions, &none, &none, 0.016f, state);
    const auto* part = static_cast<const UiButton*>(s.Find(id)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(step.hovered.empty() && part->ready && Near(part->now.scale, 1));
    step = StepUi(s, t, on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK((step.hovered == std::vector<int>{id}));
    CHECK(part->now.scale > 1 && part->now.scale < part->hoverScale);  // 途中
    step = StepUi(s, t, on, true, true, false, actions, &none, &none, 0.016f, state);
    CHECK(step.hovered.empty());  // その上で押した: 2 回目の音はない
    step = StepUi(s, t, on, false, false, true, actions, &none, &none, 0.016f, state);
    CHECK(step.hovered.empty() && (step.clicked == std::vector<int>{id}));
    StepUi(s, t, off, false, false, false, actions, &none, &none, 0.016f, state);
    step = StepUi(s, t, on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK((step.hovered == std::vector<int>{id}));  // 離れて戻った: また鳴る
}

namespace {

const reflect::FieldDesc& UiField(const reflect::TypeDesc& t, std::string_view name) {
    for (const reflect::FieldDesc& f : t.fields) {
        if (name == f.name) return f;
    }
    return t.fields[0];
}

}  // namespace

RM_TEST(GameUi_ButtonActions_WhatTheyDo) {
    // 種類ごとに自分のフィールドだけを見せる (ShowIf)。
    const reflect::TypeDesc& at = reflect::TypeOf<UiAction>();
    UiAction a;
    a.kind = UiActionKind::ShowWindow;
    CHECK(reflect::FieldShown(at, UiField(at, "window"), &a) && !reflect::FieldShown(at, UiField(at, "scene"), &a));
    a.kind = UiActionKind::SetVariable;
    a.change = UiVariableChange::Toggle;
    CHECK(reflect::FieldShown(at, UiField(at, "variable"), &a) && !reflect::FieldShown(at, UiField(at, "amount"), &a));
    a.change = UiVariableChange::Add;
    CHECK(reflect::FieldShown(at, UiField(at, "amount"), &a) && !reflect::FieldShown(at, UiField(at, "min"), &a));
    a.useMin = true;
    CHECK(reflect::FieldShown(at, UiField(at, "min"), &a) && reflect::ShowIfProblem(at, UiField(at, "min")).empty());

    // 変数を変える: ON / OFF / 入れ替える、下限 / 上限つきの 足す、整数は丸める。
    using game_production::GlobalType;
    using game_production::GlobalValue;
    GlobalValue flag{GlobalType::Bool};
    a.change = UiVariableChange::Toggle;
    CHECK(ChangeVariable(flag, a, nullptr) && flag.v[0] == 1);
    CHECK(ChangeVariable(flag, a, nullptr) && flag.v[0] == 0);
    a.change = UiVariableChange::Add;
    CHECK(!ChangeVariable(flag, a, nullptr));  // (ON / OFF への 足す: 不可)
    GlobalValue n{GlobalType::Int};
    a.amount = 2.6f;
    a.useMin = false;
    a.useMax = true;
    a.max = 5;
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 3);
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 5);  // (上限に収める)
    a.change = UiVariableChange::Set;
    a.amount = -4;
    a.useMin = true;
    a.min = 7;  // どちらも逆に使った: 入れ替える (5..7)
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 5);
    a.useMax = false;  // 下限 7 だけ: 一度も折り返さない
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 7);
    GlobalValue text{GlobalType::String};
    std::string why;
    CHECK(!ChangeVariable(text, a, &why) && !why.empty());

    // ボタンを通しで: 2 つのウィンドウ (片方は中にキャンバスあり)、すべてを順にやるボタン。
    Scene s = UiScene();
    const int title = s.Create("Title");
    Add<UiCanvas>(s, title);
    const int inner = s.Create("Inner", title);
    Add<UiCanvas>(s, inner);
    const int menu = s.Create("Menu");
    Add<UiCanvas>(s, menu);
    s.Find(menu)->data.active = false;
    const int b = MakeButton(s, title, {0, 0}, {200, 100});
    auto* button = static_cast<UiButton*>(s.Find(b)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(IsWindow(s, title) && IsWindow(s, menu) && !IsWindow(s, inner) && !IsWindow(s, b));

    game_production::GlobalStore globals;
    const game_production::GlobalVariable vars[] = {{"paused", GlobalType::Bool, "false"}};
    globals.Reset(vars);
    const AssetRef stage{"0123456789abcdef0123456789abcd11"}, beep{"0123456789abcdef0123456789abcd12"};
    std::vector<std::string> done;
    UiActionHost host;
    host.globals = &globals;
    host.loadScene = [&](const AssetRef& r) {
        done.push_back("scene");
        return r == stage;
    };
    host.quit = [&] { done.push_back("quit"); };
    host.playSound = [&](const AssetRef& r, float v) { done.push_back(r == beep && v == 0.5f ? "beep" : "?"); };
    host.log = [&](int level, std::string_view) { done.push_back(level == 1 ? "warn" : "info"); };
    auto add = [&](UiActionKind kind) -> UiAction& {
        button->actions.push_back({});
        button->actions.back().kind = kind;
        return button->actions.back();
    };
    add(UiActionKind::ShowWindow).window.id = static_cast<uint64_t>(menu);
    UiAction& v = add(UiActionKind::SetVariable);
    v.variable = "paused";
    v.change = UiVariableChange::On;
    add(UiActionKind::LoadScene).scene = stage;
    UiAction& snd = add(UiActionKind::PlaySound);
    snd.sound = beep;
    snd.volume = 0.5f;
    add(UiActionKind::Quit);
    add(UiActionKind::SetVariable).variable = "nothing";  // (ない: 知らせる)
    UiFocus state;
    RunActions(s, b, title, *button, state, host);
    CHECK(s.Find(menu)->data.active && globals.Find("paused")->v[0] == 1);
    CHECK((done == std::vector<std::string>{"scene", "beep", "quit", "warn"}));
    // 切り替える: そのウィンドウだけ (Title の中のキャンバスはその一部 - そのまま)。
    button->actions.clear();
    add(UiActionKind::SwitchWindow).window.id = static_cast<uint64_t>(menu);
    RunActions(s, b, title, *button, state, host);
    CHECK(!s.Find(title)->data.active && s.Find(menu)->data.active && s.Find(inner)->data.active);

    // 前のウィンドウに戻る: Title の上に Menu を開いた (フレームで確認)、そのボタンが閉じる - また Title。
    s.Find(title)->data.active = true;
    s.Find(menu)->data.active = false;
    const int back = MakeButton(s, menu, {0, 0}, {100, 50}, "戻る");
    auto* backButton = static_cast<UiButton*>(s.Find(back)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    backButton->actions.push_back({});
    backButton->actions.back().kind = UiActionKind::Back;
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    UiFocus play;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, play);
    s.Find(title)->data.active = false;  // (切り替えた: Title は隠れ、Menu が出る)
    s.Find(menu)->data.active = true;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, play);
    RunActions(s, back, menu, *backButton, play, host);
    CHECK(!s.Find(menu)->data.active && s.Find(title)->data.active);
    done.clear();
    RunActions(s, back, title, *backButton, play, host);  // (Title は最初からあった: 何もしない)
    CHECK(s.Find(title)->data.active && (done == std::vector<std::string>{"info"}));
}

RM_TEST(GameUi_HiddenButton_StartsAgainFromNormal) {
    // マウスの下にあるボタンのウィンドウが隠れ、またマウスの下で出た: その 乗った の音が
    // また鳴る (失われない)。見た目は通常から始まる。
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int b = MakeButton(s, canvas, {0, 0}, {200, 100});
    const auto* part = static_cast<const UiButton*>(s.Find(b)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    UiFocus state;
    state.keyboard = false;
    const Vec2 on{640, 360};
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, state);
    UiStep step = StepUi(s, BuildUi(s, {}), on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK((step.hovered == std::vector<int>{b}) && part->hot);
    s.Find(canvas)->data.active = false;  // (隠れた: ボタンのアクションかスクリプトによる)
    StepUi(s, BuildUi(s, {}), on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK(!part->hot && part->now.scale == 1.f);
    s.Find(canvas)->data.active = true;
    step = StepUi(s, BuildUi(s, {}), on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK((step.hovered == std::vector<int>{b}));
}

RM_TEST(GameUi_ScriptFocus_KeptWhenItsWindowComesInFront) {
    // ウィンドウが最初に出るフレームでのスクリプトの rm::UI::Focus (Start、Show のあと Focus):
    // 保たれ、ウィンドウの最初のボタンで置き換えられない。
    Scene s = UiScene();
    const int canvas = s.Create("Menu");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int a = MakeButton(s, canvas, {0, -100}, {100, 50}, "A");
    const int b = MakeButton(s, canvas, {0, 100}, {100, 50}, "B");
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    UiFocus chosen;
    chosen.focus = b;  // (UiService::Focus がすること)
    chosen.scriptChose = true;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, chosen);
    CHECK(chosen.focus == b && !chosen.scriptChose);
    UiFocus plain;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, plain);
    CHECK(plain.focus == a);  // (選ばれたものなし: 左上のもの)
}

RM_TEST(GameUi_NextButton_LinksFirst) {
    // 上 / 下 / 左 / 右で移る先: 同じウィンドウで押せるならそのリンク。そうでなければ、その向きで一番近いもの。
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int a = MakeButton(s, canvas, {-300, 0}, {100, 50}, "A");
    const int b = MakeButton(s, canvas, {0, 0}, {100, 50}, "B");
    const int c = MakeButton(s, canvas, {300, 0}, {100, 50}, "C");
    auto part = [&](int id) { return static_cast<UiButton*>(s.Find(id)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get()); };
    UiTree t = BuildUi(s, {});
    CHECK(NextButton(t, canvas, a, 1, 0) == b);
    part(a)->right.id = static_cast<uint64_t>(c);
    CHECK(NextButton(t, canvas, a, 1, 0) == c);
    CHECK(NextButton(t, canvas, a, 0, 1) == a);  // (下へのリンクも、下のボタンもない)
    part(a)->down.id = static_cast<uint64_t>(b);
    CHECK(NextButton(t, canvas, a, 0, 1) == b);  // (リンクは、言う向きのどこへでも行く)
    part(c)->interactable = false;
    t = BuildUi(s, {});
    CHECK(NextButton(t, canvas, a, 1, 0) == b);  // (リンクは押せない: 一番近いもの)
}

RM_TEST(GameUi_Motion_EaseTransitionAndPath) {
    // どのカーブも 0 から始まって 1 で終わる。行き過ぎて戻る は途中で 1 を越える。
    for (const UiEase e : {UiEase::Linear, UiEase::In, UiEase::Out, UiEase::InOut, UiEase::Bounce, UiEase::Back}) {
        CHECK(Near(Ease(e, 0), 0, 1e-4f) && Near(Ease(e, 1), 1, 1e-4f));
    }
    CHECK(Ease(UiEase::In, 0.5f) < 0.5f && Ease(UiEase::Out, 0.5f) > 0.5f);
    bool past = false;
    for (int i = 1; i < 20; ++i) past |= Ease(UiEase::Back, static_cast<float>(i) / 20) > 1.f;
    CHECK(past);
    // 出るとき: その値 (透明、左へ 80、半分の大きさ) から、今あるとおりのものへ。
    UiTransition t;
    t.duration = 1;
    t.delay = 0.5f;
    t.ease = UiEase::Linear;
    t.slide = UiSlide::Left;
    t.distance = 80;
    t.scale = 0.5f;
    UiLive l = TransitionAt(t, true, 0.25f);  // (まだ待っている: 始まりのとおり)
    CHECK(Near(l.alpha, 0) && Near(l.offset.x, -80) && Near(l.scale, 0.5f));
    l = TransitionAt(t, true, 1.0f);
    CHECK(Near(l.alpha, 0.5f) && Near(l.offset.x, -40) && Near(l.scale, 0.75f) && !TransitionDone(t, 1.0f));
    CHECK(TransitionAt(t, true, 1.5f) == UiLive{} && TransitionDone(t, 1.5f));
    // 消えるとき: 逆向き。
    l = TransitionAt(t, false, 1.5f);
    CHECK(Near(l.alpha, 0) && Near(l.offset.x, -80));
    t.enabled = false;
    CHECK(TransitionDone(t, 0) && TransitionAt(t, true, 0) == UiLive{});
    // パス: 直線、その長さと、その上のどこにいるか。
    UiPath p;
    p.points = {{0, 0}, {100, 0}, {100, 50}};
    p.smooth = false;
    UiPathLine line = PathLine(p);
    CHECK(Near(line.Length(), 150));
    UiPathPose pose = PathPoseAt(line, 125);
    CHECK(Near(pose.offset.x, 100) && Near(pose.offset.y, 25) && Near(pose.angle, 90));
    p.closed = true;
    CHECK(Near(PathLine(p).Length(), 150 + std::hypot(100.f, 50.f)));
    // 曲線: すべての点を通る。
    p.closed = false;
    p.smooth = true;
    line = PathLine(p);
    CHECK(line.Length() > 150 * 0.95f);
    bool throughMiddle = false;
    for (const Vec2 v : line.points) throughMiddle |= Near(v.x, 100, 1e-3f) && Near(v.y, 0, 1e-3f);
    CHECK(throughMiddle);
    // 歩く: 1 回 は終わりで止まり、くり返す は一周し、往復 は逆向きで戻ってくる。
    p.smooth = false;
    line = PathLine(p);
    p.speed = 100;
    p.loop = UiPathLoop::Once;
    pose = StepPath(p, line, 1.0f);
    CHECK(p.playing && Near(pose.offset.x, 100));
    pose = StepPath(p, line, 5.0f);
    CHECK(!p.playing && Near(pose.offset.y, 50));
    UiPath loop = p;
    loop.started = false;
    loop.loop = UiPathLoop::Loop;
    StepPath(loop, line, 1.0f);
    pose = StepPath(loop, line, 1.0f);  // 150 の線の上を 200 進む: 次の周の 50 の所
    CHECK(loop.playing && Near(pose.offset.x, 50));
    UiPath back = p;
    back.started = false;
    back.loop = UiPathLoop::PingPong;
    pose = StepPath(back, line, 1.75f);  // 175: 150 行って、25 戻る (最後の一片を上がる)
    CHECK(back.backward && Near(pose.offset.x, 100) && Near(pose.offset.y, 25) && Near(pose.angle, 180 + 90, 1e-2f));
    // スクリプトが再生するまで止まっている。
    UiPath waits = p;
    waits.started = false;
    waits.playOnStart = false;
    pose = StepPath(waits, line, 1.0f);
    CHECK(!waits.playing && Near(pose.offset.x, 0));
}

RM_TEST(GameUi_Motion_ShowHideAndTweens) {
    // 出る・消える動き のあるウィンドウ: 最初に入ってくる。オフにしても、消えるとき が
    // 終わるまでは配置されたまま (クリックも選択もされない)。
    Scene s = UiScene();
    const int canvas = s.Create("Menu");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    UiMotion* m = Add<UiMotion>(s, canvas);
    m->show.duration = m->hide.duration = 0.2f;
    m->show.ease = m->hide.ease = UiEase::Linear;
    const int b = MakeButton(s, canvas, {0, 0}, {200, 100});
    UiMotionWorld world;
    StepMotion(s, 0.1f, world);
    UiTree t = BuildUi(s, {});
    CHECK(Near(t.Find(canvas)->alpha, 0.5f) && Near(t.Find(b)->alpha, 0.5f));
    StepMotion(s, 0.2f, world);
    t = BuildUi(s, {});
    CHECK(Near(t.Find(b)->alpha, 1) && m->phase == static_cast<int>(UiMotionPhase::Still));
    CHECK(ButtonAt(t, {640, 360}) == b);
    s.Find(canvas)->data.active = false;
    StepMotion(s, 0.05f, world);
    t = BuildUi(s, {});
    CHECK(t.Find(b) && t.Find(b)->closing && Near(t.Find(b)->alpha, 0.75f));
    CHECK(ButtonAt(t, {640, 360}) == 0 && NavigationCanvas(t) == 0);
    StepMotion(s, 0.2f, world);
    t = BuildUi(s, {});
    CHECK(!t.Find(b) && m->phase == static_cast<int>(UiMotionPhase::Still));
    // また出した: また入ってくる。
    s.Find(canvas)->data.active = true;
    StepMotion(s, 0.1f, world);
    CHECK(m->phase == static_cast<int>(UiMotionPhase::Showing));
    // ゲームの始めにも がオフ: 始めからすぐそこにある。
    UiMotionWorld fresh;
    m->seen = false;
    m->atStart = false;
    StepMotion(s, 0.01f, fresh);
    CHECK(m->phase == static_cast<int>(UiMotionPhase::Still) && Near(BuildUi(s, {}).Find(b)->alpha, 1));
    // スクリプトのトゥイーン: 位置 と 回転 そのもの、サイズと透明度は終わったあとも保たれる。
    CHECK(StartTween(s, fresh, b, UiTweenKind::Move, {100, 0}, 1, UiEase::Linear));
    CHECK(StartTween(s, fresh, b, UiTweenKind::Fade, {0.2f, 0}, 0, UiEase::Linear));  // (すぐ)
    StepMotion(s, 0.5f, fresh);
    auto* rect = static_cast<UiRect*>(s.Find(b)->FindComponent(reflect::TypeOf<UiRect>().name)->value.Get());
    CHECK(Near(rect->position.x, 50) && Near(rect->live.alpha, 0.2f));
    StepMotion(s, 1.0f, fresh);
    CHECK(Near(rect->position.x, 100) && Near(rect->live.alpha, 0.2f) && !fresh.tweens[b].move.active);
    CHECK(!StartTween(s, fresh, canvas, UiTweenKind::Move, {0, 0}, 1, UiEase::Linear));  // (UI の配置 のないウィンドウ: 位置 なし)
    // 歩かせたパスはそれ (その箱) を動かし、スクリプトが止める。
    UiPath* path = Add<UiPath>(s, b);
    path->points = {{0, 0}, {100, 0}};
    path->smooth = false;
    path->speed = 100;
    path->loop = UiPathLoop::Once;
    StepMotion(s, 0.5f, fresh);
    CHECK(Near(rect->live.offset.x, 50));
    CHECK(PlayPath(s, b, 0) && !path->playing);
    StepMotion(s, 0.5f, fresh);
    CHECK(Near(rect->live.offset.x, 50));
    CHECK(PlayPath(s, b, 2));
    StepMotion(s, 0.25f, fresh);
    CHECK(Near(rect->live.offset.x, 25));
}

RM_TEST(GameUi_Style_BevelRoundingAndTheMouse) {
    // 見た目 (UI-5): 1 つの場所が各ものを決める - 立体 は使うときだけ、太さつきで描く。
    // へこませる のとき、またはそのボタンを押している間 (押すとへこむ) は押し込む。角は辺の半分を越えない。
    UiStyle st;
    CHECK(!BevelShown(st));
    st.bevel.enabled = true;
    CHECK(BevelShown(st) && !BevelInset(st, nullptr));
    st.bevel.width = 0;
    CHECK(!BevelShown(st));
    st.bevel.width = 3;
    UiButton b;
    b.state = static_cast<int>(UiButtonState::Pressed);
    CHECK(BevelInset(st, &b));
    st.bevel.inset = true;
    CHECK(!BevelInset(st, &b) && BevelInset(st, nullptr));  // (押した: また出る)
    st.rounding = 50;
    CHECK(Near(Rounding(st, {0, 0, 200, 40}), 20) && Near(Rounding(st, {0, 0, 200, 400}), 50));
    st.rounding = -3;
    CHECK(Near(Rounding(st, {0, 0, 10, 10}), 0));
    // 背景 だけを持つもの (画像なし) はマウスを受け取る - そして下にあるものを隠す。
    Scene s = UiScene();
    s.SetComponentTypes({&reflect::TypeOf<UiCanvas>(), &reflect::TypeOf<UiRect>(), &reflect::TypeOf<UiImage>(), &reflect::TypeOf<UiButton>(),
                         &reflect::TypeOf<UiStyle>()},
                        {});
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int b1 = MakeButton(s, canvas, {0, 0}, {200, 100});
    const int panel = s.Create("下敷き", canvas);
    Add<UiRect>(s, panel)->size = {400, 300};
    Add<UiStyle>(s, panel);
    const UiTree t = BuildUi(s, {});
    CHECK(t.Find(panel)->style && TopAt(t, {640, 360}) == panel && ButtonAt(t, {640, 360}) == 0);
    (void)b1;
}

RM_TEST(GameUi_Text_RunRanges) {
    // 部分のスタイル は見えるとおりに文字を数える: BMP の外の 1 文字 (UTF-16 の 2 単位) は 1 つ。
    const std::wstring plain = L"スコア 100";
    UiTextRange r = TextRange(plain, 4, 3);
    CHECK(r.start == 4 && r.length == 3);
    r = TextRange(plain, 5, 100);  // (テキストの中に収める)
    CHECK(r.start == 5 && r.length == 2);
    r = TextRange(plain, 50, 2);
    CHECK(r.start == static_cast<uint32_t>(plain.size()) && r.length == 0);
    const std::wstring wide = L"a\U0001F600b";  // a、絵文字 (サロゲートペア)、b
    r = TextRange(wide, 1, 1);
    CHECK(r.start == 1 && r.length == 2);
    r = TextRange(wide, 2, 1);
    CHECK(r.start == 3 && r.length == 1);
    r = TextRange(wide, -3, 1);
    CHECK(r.start == 0 && r.length == 1);
}

RM_TEST(GameUi_CropAndRainbow) {
    // 切り出し: 画像の中に収める。サイズ 0 はその端まで。
    CHECK(Near(CropRect({100, 50}, {0, 0}, {0, 0}), {0, 0, 100, 50}));
    CHECK(Near(CropRect({100, 50}, {20, 10}, {0, 0}), {20, 10, 80, 40}));
    CHECK(Near(CropRect({100, 50}, {20, 10}, {30, 300}), {20, 10, 30, 40}));
    CHECK(Near(CropRect({100, 50}, {500, -5}, {10, 10}), {100, 0, 0, 10}));
    // 虹色: 始まりが赤、1 / speed 秒で一周する。ずらし は始まりを動かす。
    UiRainbow r;
    r.saturation = 1;
    r.brightness = 1;
    r.speed = 0.5f;
    Color c = RainbowColor(r, 0);
    CHECK(Near(c.r, 1) && Near(c.g, 0) && Near(c.b, 0) && Near(c.a, 1));
    c = RainbowColor(r, 2.0 / 3.0);  // 一周の 3 分の 1: 緑
    CHECK(Near(c.r, 0) && Near(c.g, 1) && Near(c.b, 0));
    c = RainbowColor(r, 2.0);  // 一周した: また赤
    CHECK(Near(c.r, 1) && Near(c.g, 0));
    r.phase = 2.f / 3.f;  // 最初は青
    c = RainbowColor(r, 0);
    CHECK(Near(c.b, 1) && Near(c.r, 0) && Near(c.g, 0));
    r.saturation = 0;  // 灰色: 言うとおりの明るさ
    r.brightness = 0.5f;
    c = RainbowColor(r, 1.234);
    CHECK(Near(c.r, 0.5f) && Near(c.g, 0.5f) && Near(c.b, 0.5f));
}

// --- 入力の部品 (入力欄・スライダー・トグル・スクロール) ----------------------------------------------

namespace {

Scene ControlsScene() {
    Scene s;
    s.SetComponentTypes({&reflect::TypeOf<UiCanvas>(), &reflect::TypeOf<UiRect>(), &reflect::TypeOf<UiText>(), &reflect::TypeOf<UiImage>(),
                         &reflect::TypeOf<UiButton>(), &reflect::TypeOf<UiBar>(), &reflect::TypeOf<UiStyle>(), &reflect::TypeOf<UiInputField>(),
                         &reflect::TypeOf<UiToggle>(), &reflect::TypeOf<UiSlider>(), &reflect::TypeOf<UiScroll>()},
                        {});
    return s;
}

int Canvas1280(Scene& s, const char* name = "UI") {
    const int canvas = s.Create(name);
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    return canvas;
}

// `T` の部品を持つ、ボタンで選ぶ物 (中央から `pos` に `size`)。
template <class T>
int MakeControl(Scene& s, int parent, Vec2 pos, Vec2 size, const char* name) {
    const int id = s.Create(name, parent);
    UiRect* r = Add<UiRect>(s, id);
    r->position = pos;
    r->size = size;
    Add<UiButton>(s, id);
    Add<T>(s, id);
    return id;
}

template <class T>
T& PartIn(Scene& s, int id) {
    return *static_cast<T*>(s.Find(id)->FindComponent(reflect::TypeOf<T>().name)->value.Get());
}

ui::TypedText Chars(std::wstring_view chars) {
    ui::TypedText t;
    t.chars = std::wstring(chars);
    return t;
}

ui::TypedText Keys(std::initializer_list<ui::KeyEvent> keys) {
    ui::TypedText t;
    t.keys = keys;
    return t;
}

}  // namespace

RM_TEST(GameUi_Input_TypeIntoTheText) {
    UiInputField f;
    std::wstring text;
    int caret = 0, anchor = 0;
    UiTypeResult r = TypeInto(text, caret, anchor, f, Chars(L"abc"));
    CHECK(r.changed && text == L"abc" && caret == 3 && anchor == 3);
    r = TypeInto(text, caret, anchor, f, Keys({{ui::Key::Backspace}}));
    CHECK(r.changed && text == L"ab" && caret == 2);
    // Shift + ← で選び、打つと置き換わる。Home / End、Ctrl で単語。
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::Left, false, true}}));
    CHECK(caret == 1 && anchor == 2);
    TypeInto(text, caret, anchor, f, Chars(L"X"));
    CHECK(text == L"aX" && caret == 2 && anchor == 2);
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::Home}}));
    CHECK(caret == 0);
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::Delete}}));
    CHECK(text == L"X");
    // IME の確定した文字も入る。範囲の外のカーソルは収める。
    caret = anchor = 99;
    TypeInto(text, caret, anchor, f, [] {
        ui::TypedText t;
        t.imeResult = L"名前";
        return t;
    }());
    CHECK(text == L"X名前" && caret == 3);
    // Enter: 決まり (その後の文字は入らない)。Esc: やめる。
    ui::TypedText enter = Keys({{ui::Key::Enter}});
    enter.chars = L"zz";
    r = TypeInto(text, caret, anchor, f, enter);
    CHECK(r.submitted && !r.changed && text == L"X名前");
    CHECK(TypeInto(text, caret, anchor, f, Keys({{ui::Key::Escape}})).cancelled);

    // 最大の文字数は見える文字で (絵文字の 2 単位も 1 文字)。
    f.maxLength = 3;
    text = L"a";
    caret = anchor = 1;
    TypeInto(text, caret, anchor, f, Chars(L"\U0001F600bc"));
    CHECK(text == L"a\U0001F600b" && CountChars(text) == 3 && caret == 4);
    // 選んだ所を置き換えるときは、消える分も数える。
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::A, true}}));
    CHECK(caret == 4 && anchor == 0);
    TypeInto(text, caret, anchor, f, Chars(L"wxyz"));
    CHECK(text == L"wxy");
    f.maxLength = 0;

    // 入れてよい文字 (全角の英数字は半角に)。
    CHECK(FilterTyped(UiInputContent::Integer, L"", 0, L"-1\xFF12" L"a3") == L"-123");
    CHECK(FilterTyped(UiInputContent::Integer, L"12", 2, L"-") == L"");
    CHECK(FilterTyped(UiInputContent::Integer, L"-5", 0, L"7") == L"");  // (- の前には入らない)
    CHECK(FilterTyped(UiInputContent::Decimal, L"", 0, L"1.5.2") == L"1.52");
    CHECK(FilterTyped(UiInputContent::Alphanumeric, L"", 0, L"ab_c\xFF11 ") == L"abc1");
    CHECK(FilterTyped(UiInputContent::Any, L"", 0, L"あ_1") == L"あ_1");
    f.content = UiInputContent::Integer;
    text.clear();
    caret = anchor = 0;
    r = TypeInto(text, caret, anchor, f, Chars(L"x"));
    CHECK(!r.changed && text.empty());

    // コピーと貼り付け (パスワードはコピーしない)。
    f.content = UiInputContent::Any;
    std::wstring board;
    UiClipboard clip;
    clip.get = [&] { return std::wstring(L"貼る"); };
    clip.set = [&](const std::wstring& s) { board = s; };
    text = L"秘密";
    caret = 2, anchor = 0;
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::C, true}}), clip);
    CHECK(board == L"秘密");
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::X, true}}), clip);
    CHECK(text.empty() && board == L"秘密");
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::V, true}}), clip);
    CHECK(text == L"貼る");
    f.password = true;
    board.clear();
    anchor = 0;
    TypeInto(text, caret, anchor, f, Keys({{ui::Key::C, true}}), clip);
    CHECK(board.empty());
}

RM_TEST(GameUi_Input_WhatIsDrawn) {
    UiInputField f;
    f.placeholder = "名前";
    UiInputView v = InputView(f);
    CHECK(v.placeholder && v.shown == L"名前");
    f.text = "a\xF0\x9F\x98\x80" "b";  // a😀b
    f.typing = true;
    f.caret = 3;  // (😀 の後: UTF-16 で 3)
    f.anchor = 0;
    v = InputView(f);
    CHECK(!v.placeholder && v.shown == L"a\U0001F600b" && v.caret == 3 && v.selStart == 0 && v.selEnd == 3);
    // パスワード: 見える 1 文字に ● 1 つ。位置も文字の数で。
    f.password = true;
    v = InputView(f);
    CHECK(v.shown == L"\x25CF\x25CF\x25CF" && v.caret == 2 && v.selEnd == 2);
    // IME で変換中: カーソルの所に差し込み、その所に下線。
    f.password = false;
    f.anchor = f.caret = 1;
    f.composing = "かな";
    f.composingCaret = 1;
    v = InputView(f);
    CHECK(v.shown == L"aかな\U0001F600b" && v.compStart == 1 && v.compLength == 2 && v.caret == 2 && v.selStart == v.selEnd);
    // 打っていない欄は変換中の文字を見せない。
    f.typing = false;
    CHECK(InputView(f).shown == L"a\U0001F600b");
}

RM_TEST(GameUi_Input_ClickTypeSubmitAndCancel) {
    Scene s = ControlsScene();
    const int canvas = Canvas1280(s);
    const int field = MakeControl<UiInputField>(s, canvas, {0, 0}, {400, 80}, "名前");
    const int other = MakeControl<UiImage>(s, canvas, {0, 200}, {200, 80}, "ほか");
    const UiTree t = BuildUi(s, {});
    const auto actions = game_production::DefaultInputActions();
    InputFrame none;
    UiFocus state;
    // 押す: 打ち始める (IME をつないでと頼む: その欄の箱)。
    UiStep step = StepUi(s, t, Vec2{640, 360}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK(state.typing == field && step.typing == field && PartIn<UiInputField>(s, field).typing);
    CHECK(Near(step.typingBox.x, 440) && Near(step.typingBox.w, 400));
    // 打つ: 変わった (文字を付けて)。
    ui::TypedText typed = Chars(L"りむ");
    typed.imeComposition = L"えん";
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &none, &none, 0.016f, state, &typed);
    CHECK(PartIn<UiInputField>(s, field).text == "りむ" && PartIn<UiInputField>(s, field).composing == "えん");
    CHECK(step.changes.size() == 1 && step.changes[0].entity == field && !step.changes[0].submitted && step.changes[0].text == "りむ");
    // 打っている間、Enter (UISubmit) と矢印はボタンを押さない / 選ばない。Enter は欄が決める。
    InputFrame enterKey;
    enterKey.keys[0x0D] = 1;
    enterKey.keys[0x28] = 1;  // ↓
    ui::TypedText enter = Keys({{ui::Key::Enter}});
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &enterKey, &none, 0.016f, state, &enter);
    CHECK(step.clicked.empty() && state.typing == 0 && !PartIn<UiInputField>(s, field).typing);
    CHECK(step.changes.size() == 1 && step.changes[0].submitted && step.changes[0].text == "りむ");
    CHECK(PartIn<UiInputField>(s, field).composing.empty());
    // もう一度: 決定 (キー) でも打ち始める。Esc は打つ前に戻す。
    state.focus = field;
    state.keyboard = true;
    InputFrame enterOnly;
    enterOnly.keys[0x0D] = 1;
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &enterOnly, &none, 0.016f, state);
    CHECK(state.typing == field);
    ui::TypedText more = Chars(L"!");
    StepUi(s, t, std::nullopt, false, false, false, actions, &enterOnly, &enterOnly, 0.016f, state, &more);
    CHECK(PartIn<UiInputField>(s, field).text == "りむ!");
    ui::TypedText esc = Keys({{ui::Key::Escape}});
    InputFrame escKey;
    escKey.keys[0x1B] = 1;
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &escKey, &none, 0.016f, state, &esc);
    CHECK(state.typing == 0 && PartIn<UiInputField>(s, field).text == "りむ" && step.closed == 0);
    CHECK(step.changes.size() == 1 && step.changes[0].text == "りむ");
    // ほかの所を押すと打つのをやめる (文字はそのまま)。
    StartTyping(s, state, field);
    StepUi(s, t, Vec2{640, 560}, true, true, false, actions, &none, &none, 0.016f, state);
    CHECK(state.typing == 0 && PartIn<UiInputField>(s, field).text == "りむ");
    (void)other;
    // 押せない欄には打てない。
    PartIn<UiButton>(s, field).interactable = false;
    CHECK(!StartTyping(s, state, field));
}

RM_TEST(GameUi_Slider_SnapDragAndKeys) {
    UiSlider sl;
    sl.min = 0, sl.max = 10, sl.step = 2;
    CHECK(Near(SliderSnap(sl, 3.1f), 4) && Near(SliderSnap(sl, -5), 0) && Near(SliderSnap(sl, 99), 10));
    sl.min = 10, sl.max = 0;  // (逆でもよい)
    CHECK(Near(SliderSnap(sl, 3.1f), 4));
    sl = UiSlider{};
    sl.value = 0.25f;
    CHECK(Near(SliderFraction(sl), 0.25f));
    const UiBox box{0, 0, 236, 40};  // つまみ 36: 真ん中は 18 .. 218 を動く
    CHECK(Near(SliderAt(sl, box, {118, 20}), 0.5f) && Near(SliderAt(sl, box, {-50, 0}), 0) && Near(SliderAt(sl, box, {500, 0}), 1));
    CHECK(Near(SliderHandle(sl, box).x + 18, 18 + 200 * 0.25f));
    CHECK(Near(SliderFill(sl, box).w, 50) && Near(SliderTrack(sl, box).w, 200) && Near(SliderTrack(sl, box).h, 12));
    sl.direction = UiBarDirection::RightToLeft;
    CHECK(Near(SliderAt(sl, box, {18, 20}), 1));
    CHECK(SliderKey(sl, -1, 0) == 1 && SliderKey(sl, 0, 1) == 0);
    sl.direction = UiBarDirection::BottomToTop;
    CHECK(SliderVertical(sl) && SliderKey(sl, 0, -1) == 1 && SliderKey(sl, 1, 0) == 0);
    sl.direction = UiBarDirection::LeftToRight;
    CHECK(Near(SliderNudge(sl, 1), 0.3f) && Near(SliderNudge(sl, -1), 0.2f));  // (幅の 20 分の 1)
    sl.keyStep = 0.5f;
    CHECK(Near(SliderNudge(sl, 1), 0.75f));

    // フレームを通しで: マウスで押した所へ、押したまま外へ出ても動き、キーは向きが合えば値を、違えば選択を動かす。
    using game_production::InputControl;
    Scene s = ControlsScene();
    const int canvas = Canvas1280(s);
    const int slider = MakeControl<UiSlider>(s, canvas, {0, 0}, {436, 40}, "音量");  // 箱 422 .. 858、真ん中は 440 .. 840
    const int below = MakeControl<UiImage>(s, canvas, {0, 200}, {200, 80}, "下");
    const UiTree t = BuildUi(s, {});
    const auto actions = game_production::DefaultInputActions();
    InputFrame none;
    UiFocus state;
    UiStep step = StepUi(s, t, Vec2{540, 360}, true, true, false, actions, &none, &none, 0.016f, state);
    CHECK(Near(PartIn<UiSlider>(s, slider).value, 0.25f) && step.changes.size() == 1 && Near(static_cast<float>(step.changes[0].number), 0.25f));
    step = StepUi(s, t, Vec2{2000, 900}, true, false, false, actions, &none, &none, 0.016f, state);
    CHECK(Near(PartIn<UiSlider>(s, slider).value, 1));
    StepUi(s, t, Vec2{2000, 900}, false, false, true, actions, &none, &none, 0.016f, state);
    step = StepUi(s, t, Vec2{640, 360}, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK(step.changes.empty());  // (離した後は動かない)
    auto pad = [](InputControl c) {
        InputFrame f;
        f.pad[static_cast<size_t>(static_cast<int>(c) - game_production::kPadControlBase)] = 1;
        return f;
    };
    state.focus = slider;
    state.keyboard = true;
    const InputFrame left = pad(InputControl::PadLeft), down = pad(InputControl::PadDown);
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &left, &none, 0.016f, state);
    CHECK(Near(PartIn<UiSlider>(s, slider).value, 0.95f) && state.focus == slider && step.changes.size() == 1);
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &none, 0.016f, state);
    CHECK(state.focus == below);
}

RM_TEST(GameUi_Toggle_FlipsAndRadioGroups) {
    UiToggle tg;
    CHECK(Near(ToggleMark(tg, {10, 0, 300, 50}), {10, 0, 50, 50}));
    tg.look = UiToggleLook::Switch;
    tg.markSize = 30;
    CHECK(Near(ToggleMark(tg, {10, 0, 300, 50}), {10, 10, 54, 30}));
    CHECK(Near(ToggleMark(tg, {0, 0, 27, 50}), {0, 17.5f, 27, 15}));  // (入らなければ縮める)
    CHECK(ToggleShown(tg) == 0);

    Scene s = ControlsScene();
    const int canvas = Canvas1280(s);
    const int sound = MakeControl<UiToggle>(s, canvas, {0, -200}, {300, 60}, "効果音");
    const int easy = MakeControl<UiToggle>(s, canvas, {0, 0}, {300, 60}, "やさしい");
    const int hard = MakeControl<UiToggle>(s, canvas, {0, 100}, {300, 60}, "むずかしい");
    PartIn<UiToggle>(s, easy).group = PartIn<UiToggle>(s, hard).group = "level";
    PartIn<UiToggle>(s, easy).on = true;
    const UiTree t = BuildUi(s, {});
    const auto actions = game_production::DefaultInputActions();
    InputFrame none;
    UiFocus state;
    UiStep step = StepUi(s, t, Vec2{640, 160}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK(PartIn<UiToggle>(s, sound).on && step.changes.size() == 1 && step.changes[0].number == 1);
    CHECK(Near(PartIn<UiToggle>(s, sound).shown, 1));  // (最初のフレームは、なめらかにせずそのまま)
    step = StepUi(s, t, Vec2{640, 160}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK(!PartIn<UiToggle>(s, sound).on && PartIn<UiToggle>(s, sound).shown > 0.5f);  // (つまみはなめらかに)
    // グループ: 押したものがオン、ほかはオフ。オンのものを押してもオンのまま。
    step = StepUi(s, t, Vec2{640, 460}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK(PartIn<UiToggle>(s, hard).on && !PartIn<UiToggle>(s, easy).on && step.changes.size() == 2);
    step = StepUi(s, t, Vec2{640, 460}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK(PartIn<UiToggle>(s, hard).on && step.changes.empty());

    // UI サービス: 変わったものはそのエンティティへのイベント "UIChanged" (スクリプトの OnEvent) になる。
    game_production::GameSettings settings;
    game_production::PlayHost host;
    host.settings = &settings;
    UiService service(host);
    game_production::ScriptWorld world;
    world.scene = &s;
    game_production::PlayFrame f{s, 0, 0.016f};
    f.screen = {1280, 720};
    f.pointer = Vec2{640, 160};
    f.pointerWentDown = f.pointerWentUp = true;
    f.world = &world;
    game_production::TextInputAsk ask;
    f.textInput = &ask;
    service.Frame(f);
    CHECK(world.events.size() == 1 && world.events[0].name == kChangedEvent && world.events[0].target == sound && world.events[0].scope == 1 &&
          world.events[0].number == 1);
    CHECK(!ask.want);
}

RM_TEST(GameUi_Scroll_ClipsWheelDragAndFocus) {
    // 純粋: 範囲・見せる・バー。
    UiScroll sc;
    const UiBox view{0, 0, 400, 300};
    const UiBox content{0, 0, 380, 600};
    UiScrollRange r = ScrollRange(sc, view, content);
    CHECK(Near(r.low.y, 0) && Near(r.high.y, 300) && Near(r.high.x, 0));
    CHECK(Near(ClampScroll({50, 400}, r).y, 300) && Near(ClampScroll({50, 400}, r).x, 0));
    CHECK(Near(ScrollToShow({0, 0}, view, {0, 450, 100, 100}, r).y, 250));  // 下にはみ出す: 下の端を合わせる
    CHECK(Near(ScrollToShow({0, 250}, view, {0, 100, 100, 100}, r).y, 100));  // 上にはみ出す
    CHECK(Near(ScrollToShow({0, 100}, view, {0, 150, 100, 100}, r).y, 100));  // 見えている: そのまま
    UiScrollBar bar = ScrollBar(sc, view, content, {0, 150}, true);
    CHECK(bar.shown && Near(bar.track.x, 390) && Near(bar.track.h, 300) && Near(bar.thumb.h, 150) && Near(bar.thumb.y, 75));
    CHECK(Near(ThumbDrag(bar, 15, r.low.y, r.high.y, true), 30));
    CHECK(!ScrollBar(sc, view, content, {0, 0}, false).shown);

    // シーン: 枠 400 x 300 (中央)、その中に 100 ずつの行 6 つ。
    Scene s = ControlsScene();
    const int canvas = Canvas1280(s);
    const int scroll = s.Create("リスト", canvas);
    {
        UiRect* rr = Add<UiRect>(s, scroll);
        rr->size = {400, 300};
        Add<UiScroll>(s, scroll)->wheelStep = 50;
    }
    std::vector<int> rows;
    for (int i = 0; i < 6; ++i) {
        const int row = s.Create("行", scroll);
        UiRect* rr = Add<UiRect>(s, row);
        rr->anchor = UiAnchor::TopLeft;
        rr->pivot = {0, 0};
        rr->position = {0, 100.f * static_cast<float>(i)};
        rr->size = {300, 90};
        Add<UiImage>(s, row);
        Add<UiButton>(s, row);
        rows.push_back(row);
    }
    UiTree t = BuildUi(s, {});
    const UiNode* sn = t.Find(scroll);
    CHECK(sn && sn->scroll && Near(sn->content, {440, 210, 300, 590}) && sn->end == static_cast<int>(t.nodes.size()));
    CHECK(t.Find(rows[5])->clip == static_cast<int>(sn - t.nodes.data()));
    // 枠の外の行は見えないので触れない (枠は 440..840 × 210..510)。行 4 は 610..700。
    CHECK(TopAt(t, {500, 650}) == 0 && TopAt(t, {500, 250}) == rows[0]);
    CHECK(ScrollAt(t, {500, 250}) == static_cast<int>(sn - t.nodes.data()) && ScrollAt(t, {100, 100}) == -1);
    // ホイール (手前へ 2 つ = 下を見る)。
    const auto actions = game_production::DefaultInputActions();
    UiFocus state;
    InputFrame wheel;
    wheel.wheel = -2;
    InputFrame none;
    StepUi(s, t, Vec2{500, 250}, false, false, false, actions, &wheel, &none, 0.016f, state);
    CHECK(Near(PartIn<UiScroll>(s, scroll).offset.y, 100) && Near(PartIn<UiScroll>(s, scroll).high.y, 290));
    t = BuildUi(s, {});
    CHECK(TopAt(t, {500, 250}) == rows[1]);  // (中身が 100 上へ)
    // 空いた所 (行の右) をつかんで上へ 40 動かす: 中身もついてくる。
    StepUi(s, t, Vec2{800, 400}, true, true, false, actions, &none, &none, 0.016f, state);
    StepUi(s, t, Vec2{800, 360}, true, false, false, actions, &none, &none, 0.016f, state);
    CHECK(Near(PartIn<UiScroll>(s, scroll).offset.y, 140));
    StepUi(s, t, Vec2{800, 360}, false, false, true, actions, &none, &none, 0.016f, state);
    CHECK(state.scrolling == 0);
    // キーで一番下の行を選ぶ: 見えるまで動く。
    t = BuildUi(s, {});
    state.focus = rows[5];
    state.keyboard = true;
    StepUi(s, t, std::nullopt, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK(Near(PartIn<UiScroll>(s, scroll).offset.y, 290));
    state.focus = rows[0];
    StepUi(s, t, std::nullopt, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK(Near(PartIn<UiScroll>(s, scroll).offset.y, 0));
}

RM_TEST(GameUi_Scripts_InputParts) {
    Scene s = ControlsScene();
    const int canvas = Canvas1280(s, "Settings");
    const int field = MakeControl<UiInputField>(s, canvas, {0, 0}, {400, 80}, "name");
    Add<UiText>(s, field)->text = "見た目だけ";
    const int slider = MakeControl<UiSlider>(s, canvas, {0, 100}, {400, 40}, "volume");
    const int a = MakeControl<UiToggle>(s, canvas, {0, 200}, {300, 60}, "a");
    const int b = MakeControl<UiToggle>(s, canvas, {0, 300}, {300, 60}, "b");
    PartIn<UiToggle>(s, a).group = PartIn<UiToggle>(s, b).group = "g";
    const int list = s.Create("list", canvas);
    Add<UiRect>(s, list);
    UiScroll& sc = *Add<UiScroll>(s, list);
    sc.low = {0, 0};
    sc.high = {0, 200};
    game_production::ScriptWorld world;
    world.scene = &s;
    const auto* api = static_cast<const RmUiApi*>(ScriptUiPart().table);
    CHECK(RM_PART_UI_VERSION == 3u && ScriptUiPartVersion2().version == 2u && ScriptUiPartVersion2().table == api);
    // 入力欄の文字は入力欄のもの (同じエンティティの文字 (UI) ではない)。
    CHECK(api->setText(&world, field, "りむ") == 1 && PartIn<UiInputField>(s, field).text == "りむ" && PartIn<UiText>(s, field).text == "見た目だけ");
    char buf[32];
    CHECK(api->getText(&world, field, buf, sizeof(buf)) == std::string("りむ").size() && std::string(buf) == "りむ");
    float v = -1;
    CHECK(api->setValue(&world, slider, 2) == 1 && api->getValue(&world, slider, &v) == 1 && Near(v, 1));
    CHECK(api->setValue(&world, a, 1) == 1 && api->getValue(&world, a, &v) == 1 && v == 1);
    CHECK(api->setValue(&world, b, 1) == 1 && !PartIn<UiToggle>(s, a).on && PartIn<UiToggle>(s, b).on);  // (同じグループ)
    CHECK(api->getValue(&world, field, &v) == 0 && api->setValue(&world, field, 1) == 0);
    float at[2] = {-1, -1};
    CHECK(api->setScroll(&world, list, 0, 0.5f) == 1 && Near(sc.offset.y, 100));
    CHECK(api->getScroll(&world, list, at) == 1 && Near(at[0], 0) && Near(at[1], 0.5f));
    CHECK(api->typing(&world) == 0 && api->startTyping(&world, field) == 0);  // (UI サービスが無い: テスト)
}
