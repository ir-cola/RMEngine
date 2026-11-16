// Tests for the game UI plugin (plugins/rm_engine/game_ui): anchors and sizes, fitting a canvas to the
// screen and its safe area, what the mouse is on (turned things too), the buttons' states and clicks, the
// keys' focus, saving the parts, and the scripts' part.

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"
#include "rm_engine/game_ui/runtime/ScriptUi.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"
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

// A button: UI の配置 at `pos` (from the middle) of `size`, with an image (it takes the mouse).
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

// Where a thing's box corners are in its parent (turned about its 中心), as drawn.
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
    // The right handle: wider by 50, the left side stays.
    UiRect w = ResizeByHandle(r, parent, UiHandle::Right, {50, 0}, false);
    CHECK(Near(PlaceBox(w, parent).x, PlaceBox(r, parent).x) && Near(PlaceBox(w, parent).w, 250));
    // The top left corner pulled up and left: the bottom right corner stays.
    UiRect tl = ResizeByHandle(r, parent, UiHandle::TopLeft, {-20, -10}, false);
    CHECK(Near(PlaceBox(tl, parent).Right(), PlaceBox(r, parent).Right()) && Near(PlaceBox(tl, parent).Bottom(), PlaceBox(r, parent).Bottom()));
    CHECK(Near(PlaceBox(tl, parent).w, 220) && Near(PlaceBox(tl, parent).h, 110));
    // A corner keeping its shape: 200 x 100 -> 300 x 150 (the bigger change decides).
    UiRect a = ResizeByHandle(r, parent, UiHandle::BottomRight, {100, 10}, true);
    CHECK(Near(PlaceBox(a, parent).w, 300) && Near(PlaceBox(a, parent).h, 150));
    // Never below the least size.
    UiRect tiny = ResizeByHandle(r, parent, UiHandle::Left, {500, 0}, false, 4);
    CHECK(Near(PlaceBox(tiny, parent).w, 4));
    // Turned 30 degrees, a 中心 off the middle, stretched across the top: the side opposite the handle
    // stays where it is seen.
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
    // Moving its 中心 keeps it where it is seen (only the 中心 and 位置 change).
    const UiRect moved = MovePivot(t, parent, {0.9f, 0.1f});
    const auto afterPivot = SeenCorners(moved, parent);
    for (size_t i = 0; i < 4; ++i) CHECK(NearV(afterPivot[i], before[i]));
    CHECK(Near(moved.pivot.x, 0.9f) && Near(moved.size.x, t.size.x) && Near(moved.rotation, 30));
    // Turning by a drag around the 中心: a quarter clockwise (Y down) is +90; steps round it.
    CHECK(Near(RotateByDrag(10, {0, 0}, {1, 0}, {0, 1}), 100));
    CHECK(Near(RotateByDrag(0, {0, 0}, {1, 0}, {1, 0.4f}, 15), 15));
    CHECK(Near(RotateByDrag(170, {0, 0}, {1, 0}, {-1, -0.01f}), 170 - 179.427f, 0.01f));  // (the short way round)
}

RM_TEST(GameUi_Snap_ToTheNearestEdgeOrGrid) {
    // A box at 10..110 (middle 60): the nearest target within reach wins - any of its sides or its middle.
    const float edges[] = {10, 60, 110};
    const float targets[] = {0, 57, 200};
    std::optional<float> line;
    CHECK(Near(SnapShift(edges, targets, 5, &line), -3) && line && Near(*line, 57));  // middle 60 -> 57
    const float far[] = {300, 400};
    CHECK(SnapShift(edges, far, 5, &line) == 0 && !line);  // nothing near: no shift
    const float exact[] = {110};
    CHECK(SnapShift(edges, exact, 5, &line) == 0 && line && Near(*line, 110));  // already on it: stays, the line shows
    // The grid: to the nearest multiple.
    CHECK(Near(GridShift(23, 10), -3) && Near(GridShift(26, 10), 4) && Near(GridShift(-14, 10), 4) && Near(GridShift(-16, 10), -4) && GridShift(7, 0) == 0);
}

RM_TEST(GameUi_AlignAndDistribute) {
    const UiBox boxes[] = {{10, 0, 20, 10}, {50, 30, 40, 20}, {200, 5, 10, 10}};
    const auto left = AlignShifts(boxes, UiAlign::Left);
    CHECK(Near(left[0].x, 0) && Near(left[1].x, -40) && Near(left[2].x, -190) && Near(left[1].y, 0));
    const auto right = AlignShifts(boxes, UiAlign::Right);  // to 210
    CHECK(Near(right[0].x, 180) && Near(right[1].x, 120) && Near(right[2].x, 0));
    const auto mid = AlignShifts(boxes, UiAlign::CenterY);  // all from 0 to 50: middle 25
    CHECK(Near(mid[0].y, 20) && Near(mid[1].y, -15) && Near(mid[2].y, 15) && Near(mid[0].x, 0));
    // Equal gaps: from 10 to 210 with 70 filled -> gaps of 65; the first and last stay.
    const auto spread = DistributeShifts(boxes, true);
    CHECK(Near(spread[0].x, 0) && Near(spread[2].x, 0) && Near(boxes[1].x + spread[1].x, 10 + 20 + 65));
    // In their order along the axis, whatever order they were given in.
    const UiBox mixed[] = {{200, 0, 10, 10}, {10, 0, 10, 10}, {50, 0, 10, 10}};
    const auto m = DistributeShifts(mixed, true);
    CHECK(Near(m[0].x, 0) && Near(m[1].x, 0) && Near(mixed[2].x + m[2].x, 105));
    // Two or fewer: nothing to spread.
    CHECK(DistributeShifts(std::span(boxes, 2), true)[1].x == 0);
}

RM_TEST(GameUi_AnchorGrid_RehomeAndOneCanvas) {
    // Every anchor has its cell, and the cell gives it back.
    for (int a = 0; a <= static_cast<int>(UiAnchor::Stretch); ++a) {
        int row = -1, col = -1;
        AnchorCell(static_cast<UiAnchor>(a), row, col);
        CHECK(row >= 0 && row < 4 && col >= 0 && col < 4 && AnchorAt(row, col) == static_cast<UiAnchor>(a));
    }
    CHECK(AnchorAt(0, 0) == UiAnchor::TopLeft && AnchorAt(3, 3) == UiAnchor::Stretch && AnchorAt(2, 3) == UiAnchor::StretchBottom);
    // Under another parent: as big as it was, its 中心 where asked.
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
    // One canvas alone: its things, their parents found again in the smaller list.
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
    // The inspector's アンカー: the thing stays where it is on the screen (to every preset and back).
    const UiBox parent{0, 0, 1920, 1080};
    UiRect r;  // the middle
    r.position = {174, 366};
    r.size = {1200, 80};
    r.pivot = {0.3f, 0.5f};
    r.rotation = 15;
    const UiBox box = PlaceBox(r, parent);
    for (int a = 0; a <= static_cast<int>(UiAnchor::Stretch); ++a) {
        const UiRect moved = Reanchored(r, static_cast<UiAnchor>(a), parent);
        CHECK(moved.anchor == static_cast<UiAnchor>(a));
        CHECK(Near(PlaceBox(moved, parent), box));
        CHECK(Near(moved.pivot.x, 0.3f) && Near(moved.rotation, 15));  // (the rest as it was)
        const UiRect back = Reanchored(moved, UiAnchor::Center, parent);
        CHECK(Near(back.position.x, 174) && Near(back.position.y, 366) && Near(back.size.x, 1200) && Near(back.size.y, 80));
    }
    // To the bottom right: the 位置 is measured from that corner now (left of it and above it: -).
    const UiRect corner = Reanchored(r, UiAnchor::BottomRight, parent);
    CHECK(corner.position.x < 0 && corner.position.y < 0 && Near(corner.size.x, 1200));
    // To stretched: the size is what the box differs from the parent by.
    const UiRect wide = Reanchored(r, UiAnchor::Stretch, parent);
    CHECK(Near(wide.size.x, 1200 - 1920) && Near(wide.size.y, 80 - 1080));
}

RM_TEST(GameUi_Anchors_PlaceTheBox) {
    const UiBox parent{0, 0, 1920, 1080};
    UiRect r;  // the middle, 240 x 64
    r.position = {10, 20};
    r.size = {200, 60};
    CHECK(Near(PlaceBox(r, parent), {870, 530, 200, 60}));
    // Fixed to the top left, its 中心 at its own top left: 位置 is where its corner is.
    r.anchor = UiAnchor::TopLeft;
    r.pivot = {0, 0};
    r.position = {16, 16};
    r.size = {100, 50};
    CHECK(Near(PlaceBox(r, parent), {16, 16, 100, 50}));
    // Bottom right with its 中心 there too: the box goes up and left of the corner.
    r.anchor = UiAnchor::BottomRight;
    r.pivot = {1, 1};
    r.position = {-16, -16};
    CHECK(Near(PlaceBox(r, parent), {1920 - 16 - 100, 1080 - 16 - 50, 100, 50}));
    // Stretched: the parent's whole width / height, the size adds (here: 20 in from each side).
    r.anchor = UiAnchor::Stretch;
    r.pivot = {0.5f, 0.5f};
    r.position = {0, 0};
    r.size = {-40, -40};
    CHECK(Near(PlaceBox(r, parent), {20, 20, 1880, 1040}));
    // Stretched across the top only: the width follows the parent, the height is the size.
    r.anchor = UiAnchor::StretchTop;
    r.pivot = {0.5f, 0};
    r.size = {0, 100};
    CHECK(Near(PlaceBox(r, {100, 50, 800, 600}), {100, 50, 800, 100}));
    // Never less than nothing.
    r.size = {-5000, -500};
    CHECK(PlaceBox(r, parent).w == 0 && PlaceBox(r, parent).h == 0);
    // Every preset is a range inside 0..1.
    for (int a = 0; a <= static_cast<int>(UiAnchor::Stretch); ++a) {
        Vec2 mn, mx;
        AnchorRange(static_cast<UiAnchor>(a), mn, mx);
        CHECK(mn.x >= 0 && mn.y >= 0 && mx.x <= 1 && mx.y <= 1 && mn.x <= mx.x && mn.y <= mx.y);
    }
}

RM_TEST(GameUi_Canvas_FitsTheScreen) {
    const Vec2 ref{1920, 1080};
    CHECK(Near(CanvasScale(UiScaleMode::Fit, ref, {1280, 720}), 1280.f / 1920.f));
    CHECK(Near(CanvasScale(UiScaleMode::Fit, ref, {1280, 1024}), 1280.f / 1920.f));  // the narrower way
    CHECK(Near(CanvasScale(UiScaleMode::Fill, ref, {1280, 1024}), 1024.f / 1080.f));
    CHECK(Near(CanvasScale(UiScaleMode::Width, ref, {960, 2000}), 0.5f));
    CHECK(Near(CanvasScale(UiScaleMode::Height, ref, {960, 540}), 0.5f));
    CHECK(Near(CanvasScale(UiScaleMode::None, ref, {960, 540}), 1));
    CHECK(CanvasScale(UiScaleMode::Fit, {0, 0}, {0, 0}) > 0);  // never 0 (it divides)
    // The safe area: the margins taken off; never below nothing.
    CHECK(Near(SafeBox({1280, 720}, {40, 20}, {40, 30}), {40, 20, 1200, 670}));
    CHECK(Near(SafeBox({100, 100}, {80, 0}, {80, 0}), {80, 0, 0, 100}));
    // A canvas in it: 1920 x 1080 in 640 x 360 is a third.
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
    // With a safe area of 20 on the left (the canvas says to keep inside it): the box moves right.
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
    static_cast<UiRect*>(s.Find(a)->FindComponent(reflect::TypeOf<UiRect>().name)->value.Get())->order = 3;  // a in front of b
    const UiTree t = BuildUi(s, {});
    // Canvases by 手前の順 (Front, 1, first); then b before a (a's order is bigger).
    std::vector<int> order;
    for (const UiNode& n : t.nodes) order.push_back(n.entity);
    CHECK((order == std::vector<int>{front, back, b, a}));
    CHECK(TopAt(t, {640, 360}) == a);  // the frontmost
    // A canvas switched off is not there - unless it holds what is being made (the UI view).
    s.Find(back)->data.active = false;
    CHECK(BuildUi(s, {}).Find(a) == nullptr);
    const int show[] = {a};
    CHECK(BuildUi(s, {}, show).Find(a) != nullptr);
    // A thing switched off goes with what is under it.
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
    static_cast<UiRect*>(s.Find(b)->FindComponent(reflect::TypeOf<UiRect>().name)->value.Get())->rotation = 90;  // standing up
    const UiTree t = BuildUi(s, {});
    const UiNode* n = t.Find(b);
    CHECK(n != nullptr);
    if (!n) return;
    CHECK(Hits(*n, {640, 360 + 150}) && !Hits(*n, {640 + 150, 360}));  // long downwards now, narrow sideways
    // A label over the button: the mouse on the label is on the button.
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
    // Down and up on the same button: a click.
    PointerResult r = StepPointer(t, on, true, true, false, ps);
    CHECK(r.hovered == b && r.held == b && r.clicked == 0);
    r = StepPointer(t, on, false, false, true, ps);
    CHECK(r.clicked == b && ps.pressed == 0);
    // Down on it, up somewhere else: none.
    StepPointer(t, on, true, true, false, ps);
    r = StepPointer(t, onOther, true, false, false, ps);
    CHECK(r.hovered == other && r.held == 0);  // (not held: the mouse left it)
    r = StepPointer(t, onOther, false, false, true, ps);
    CHECK(r.clicked == 0);
    // Not pressable: no hover, no click - and it still hides what is under it.
    auto* button = static_cast<UiButton*>(s.Find(b)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    button->interactable = false;
    t = BuildUi(s, {});
    StepPointer(t, on, true, true, false, ps);
    r = StepPointer(t, on, false, false, true, ps);
    CHECK(r.hovered == 0 && r.clicked == 0);
    CHECK(TopAt(t, on) == b);
    // Its look: the colour and the size by state; focused looks as 乗った.
    button->interactable = true;
    button->state = static_cast<int>(UiButtonState::Pressed);
    CHECK(ButtonTarget(*button).tint == button->pressed && Near(ButtonScale(*button), button->pressedScale));
    button->state = static_cast<int>(UiButtonState::Normal);
    button->focused = true;
    CHECK(ButtonTarget(*button).tint == button->hover && Near(ButtonScale(*button), button->hoverScale));
    button->interactable = false;
    CHECK(ButtonTarget(*button).tint == button->disabled && Near(ButtonScale(*button), 1));
    // A bar's filled part.
    UiBar bar;
    bar.value = 0.25f;
    bar.padding = 0;
    CHECK(Near(BarFill(bar, {0, 0, 100, 10}), {0, 0, 25, 10}));
    bar.direction = UiBarDirection::RightToLeft;
    CHECK(Near(BarFill(bar, {0, 0, 100, 10}), {75, 0, 25, 10}));
    bar.direction = UiBarDirection::BottomToTop;
    bar.value = 2;  // (kept to 1)
    bar.padding = 2;
    CHECK(Near(BarFill(bar, {0, 0, 100, 10}), {2, 2, 96, 6}));
}

RM_TEST(GameUi_Keys_MoveTheFocusAndPress) {
    using game_production::InputControl;
    Scene s = UiScene();
    const int canvas = s.Create("Menu");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    // A grid: tl tr / bl br.
    const int tl = MakeButton(s, canvas, {-200, -100}, {100, 50}, "tl");
    const int tr = MakeButton(s, canvas, {200, -100}, {100, 50}, "tr");
    const int bl = MakeButton(s, canvas, {-200, 100}, {100, 50}, "bl");
    const int br = MakeButton(s, canvas, {200, 120}, {100, 50}, "br");
    const UiTree t = BuildUi(s, {});
    CHECK(NavigationCanvas(t) == canvas);
    CHECK(FirstFocus(t, canvas, 0) == tl);
    CHECK(FirstFocus(t, canvas, br) == br);  // the canvas's 最初に選ぶボタン
    // By itself: the nearest button that way on the screen (nothing set on the buttons).
    CHECK(NextButton(t, canvas, tl, 1, 0) == tr);
    CHECK(NextButton(t, canvas, tl, 0, 1) == bl);
    CHECK(NextButton(t, canvas, tr, 0, 1) == br);
    CHECK(NextButton(t, canvas, tl, -1, 0) == tl);  // nothing that way: stays

    // Read only through the actions (UIUp ... UICancel): the pad's cross, the left stick, the arrows.
    const std::vector<game_production::InputAction> actions = game_production::DefaultInputActions();
    auto pad = [](InputControl c, float v) {
        InputFrame f;
        f.pad[static_cast<size_t>(static_cast<int>(c) - game_production::kPadControlBase)] = v;
        return f;
    };
    InputFrame none;
    CHECK(ReadUiKeys(actions, pad(InputControl::PadRight, 1), none).dx == 1);
    CHECK(ReadUiKeys(actions, pad(InputControl::PadLeftStickY, 0.9f), none).dy == -1);  // the stick up: up the screen
    CHECK(ReadUiKeys(actions, pad(InputControl::PadLeftStickX, -0.8f), none).dx == -1);
    CHECK(ReadUiKeys(actions, pad(InputControl::PadLeftStickX, 0.3f), none).dx == 0);  // (not far enough)
    CHECK(ReadUiKeys(actions, pad(InputControl::PadA, 1), none).submit && ReadUiKeys(actions, pad(InputControl::PadB, 1), none).cancel);
    InputFrame keys;
    keys.keys[0x0D] = 1;  // Enter
    keys.keys[0x27] = 1;  // the right arrow
    UiKeys k = ReadUiKeys(actions, keys, none);
    CHECK(k.submit && k.dx == 1 && k.dy == 0);
    CHECK(ReadUiKeys(actions, keys, keys).dx == 0 && ReadUiKeys(actions, keys, keys).holdDx == 1);  // held, not pressed again
    // A stick aslant goes the stronger way only.
    InputFrame aslant = pad(InputControl::PadLeftStickX, 0.9f);
    aslant.pad[static_cast<size_t>(static_cast<int>(InputControl::PadLeftStickY) - game_production::kPadControlBase)] = 0.6f;
    k = ReadUiKeys(actions, aslant, none);
    CHECK(k.dx == 1 && k.dy == 0);
    // A game made before the UI's actions (none in its 入力): their standard ones.
    CHECK(ReadUiKeys({}, keys, none).submit && ReadUiKeys({}, keys, none).dx == 1);
    // Bindings changed: the UI follows them (UISubmit on Z only - Enter does nothing now).
    std::vector<game_production::InputAction> changed = actions;
    for (auto& a : changed) {
        if (a.name == game_production::kUiSubmit) a.bindings = {{InputControl::Z, 1}};
    }
    CHECK(!ReadUiKeys(changed, keys, none).submit);
    InputFrame z;
    z.keys['Z'] = 1;
    CHECK(ReadUiKeys(changed, z, none).submit);

    // Whole frames, the pad only: the window always has a chosen button, shown from the start - A presses it.
    UiFocus state;
    const InputFrame a = pad(InputControl::PadA, 1), right = pad(InputControl::PadRight, 1);
    UiStep step = StepUi(s, t, std::nullopt, false, false, false, actions, &a, &none, 0.016f, state);
    CHECK((step.clicked == std::vector<int>{tl}));
    const auto* tlButton = static_cast<const UiButton*>(s.Find(tl)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(tlButton->focused);
    step = StepUi(s, t, std::nullopt, false, false, false, actions, &right, &none, 0.016f, state);
    CHECK(state.focus == tr && step.clicked.empty());
    // Held: no step at once, one after kRepeatDelay, then every kRepeatEvery (here back and forth on 2 buttons).
    const InputFrame down = pad(InputControl::PadDown, 1);
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &none, 0.016f, state);
    CHECK(state.focus == br);
    const InputFrame up = pad(InputControl::PadUp, 1);
    StepUi(s, t, std::nullopt, false, false, false, actions, &up, &none, 0.016f, state);
    CHECK(state.focus == tr);
    StepUi(s, t, std::nullopt, false, false, false, actions, &up, &up, 0.2f, state);
    CHECK(state.focus == tr);  // (0.2 s: not yet; and nothing above tr anyway)
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &up, 0.016f, state);
    CHECK(state.focus == br);
    StepUi(s, t, std::nullopt, false, false, false, actions, &down, &down, 0.3f, state);
    CHECK(state.focus == br);  // held 0.3 s
    // The mouse moved: the choice is hidden; the next step only shows it again.
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
    CHECK(state.focus == rows[1]);  // the first press: a step
    float held = 0;
    int steps = 0;
    int last = state.focus;
    while (held < kRepeatDelay + 2.5f * kRepeatEvery) {  // ~0.7 s of holding, 10 ms a frame
        StepUi(s, t, std::nullopt, false, false, false, actions, &down, &down, 0.01f, state);
        held += 0.01f;
        if (state.focus != last) {
            ++steps;
            last = state.focus;
            CHECK(held >= kRepeatDelay - 1e-3f);  // nothing before the delay
        }
    }
    CHECK(steps == 3);  // at the delay, then 2 more
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
    // At the start only HUD: 戻る closes nothing (it is not a window opened over another).
    CHECK(frame(esc).closed == 0 && s.Find(hud)->data.active);
    // Pause shown over HUD (a script's Show): 戻る closes Pause, HUD stays.
    s.Find(pause)->data.active = true;
    frame(none);
    CHECK(state.focus == resume);  // the new window's first button is chosen
    CHECK(frame(esc).closed == pause && !s.Find(pause)->data.active && s.Find(hud)->data.active);
    frame(none);
    CHECK(frame(esc).closed == 0);  // back at the start: nothing more to close
    // Options instead of HUD (SwitchTo): 戻る closes Options and shows HUD again.
    s.Find(hud)->data.active = false;
    s.Find(options)->data.active = true;
    frame(none);
    CHECK(frame(esc).closed == options && s.Find(hud)->data.active && !s.Find(options)->data.active);
    frame(none);
    CHECK(state.visits.empty());
    // A window with 戻るで押すボタン: 戻る presses it instead.
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
    CHECK(saved.Get("anchor").AsString() == "StretchBottom");  // enums by name
    UiRect back;
    reflect::Load(back, saved, nullptr, nullptr);
    CHECK(back.anchor == r.anchor && back.position == r.position && back.size == r.size && back.rotation == 15);
    // A button's state of the frame is never saved.
    UiButton b;
    b.state = 2;
    b.focused = true;
    const json::Value bs = reflect::Save(b, nullptr);
    CHECK(bs.Get("state").IsNull() && bs.Get("focused").IsNull());
    // Text with a newer key: written back.
    UiText t;
    t.text = "スコア 0";
    json::Value ts = reflect::Save(t, nullptr);
    ts.Set("gradient", true);
    UiText t2;
    reflect::Unknowns unknowns;
    reflect::Load(t2, ts, &unknowns, nullptr);
    CHECK(t2.text == "スコア 0" && reflect::Save(t2, &unknowns).Get("gradient").AsBool());
    // Values kept in range: a bar's value.
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
    CHECK(api->getText(&world, label, buf, 4) == std::string("スコア 10").size() && std::string(buf).size() == 3);  // cut to fit
    CHECK(api->setBar(&world, hp, 0.4f) == 1 && Near(api->getBar(&world, hp), 0.4f));
    CHECK(api->setBar(&world, hp, 9) == 1 && Near(api->getBar(&world, hp), 1));
    CHECK(api->setBar(&world, label, 0.5f) == 0 && api->getBar(&world, label) == -1);  // not a bar
    // Windows by name: switching shows one and hides the others.
    CHECK(api->switchWindow(&world, "HUD") == 1);
    CHECK(api->windowShown(&world, "HUD") == 1 && api->windowShown(&world, "Title") == 0);
    CHECK(api->showWindow(&world, "Title", 1) == 1 && api->windowShown(&world, "Title") == 1);
    CHECK(api->switchWindow(&world, "Nowhere") == 0);  // (told once on the console)
    // The hook a button calls is OnClick (ABI 6).
    static_assert(RM_ABI_VERSION >= 6u && RM_HOOK_CLICK == 7);
}

RM_TEST(GameUi_QuickClick_InOneFrame) {
    // A click quicker than a frame (computer use, a tap of a touch pad): the button is up at both looks,
    // but the window was told it went down and up - that is a click.
    Scene s = UiScene();
    const int canvas = s.Create("UI");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int b = MakeButton(s, canvas, {0, 0}, {200, 100});
    const UiTree t = BuildUi(s, {});
    PointerState ps;
    PointerResult r = StepPointer(t, Vec2{640, 360}, false, true, true, ps);
    CHECK(r.clicked == b && ps.pressed == 0 && r.held == 0);
    // ... off the button: nothing; and nothing left pressed for the next frame.
    r = StepPointer(t, Vec2{50, 50}, false, true, true, ps);
    CHECK(r.clicked == 0 && ps.pressed == 0);
    // Through a whole frame of the UI (what the service does with the window's presses).
    UiFocus state;
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    const UiStep step = StepUi(s, t, Vec2{640, 360}, false, true, true, actions, &none, &none, 0.016f, state);
    CHECK((step.clicked == std::vector<int>{b}));
    // The button looks normal afterwards (not left pressed).
    const auto* button = static_cast<const UiButton*>(s.Find(b)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(button->state != static_cast<int>(UiButtonState::Pressed));
}

RM_TEST(GameUi_ButtonLook_EasedOutlineAndSounds) {
    UiButton b;
    // The ease does not depend on the frame rate: ten frames of 0.01 s = one of 0.1 s.
    UiButtonLook a, c;
    b.state = static_cast<int>(UiButtonState::Hover);
    const UiButtonLook hover = ButtonTarget(b);
    CHECK(Near(hover.scale, b.hoverScale) && Near(hover.outlineAmount, 1) && Near(hover.outline, b.hoverOutline));
    for (int i = 0; i < 10; ++i) a = EaseLook(a, hover, EaseAmount(0.01f, 0.08f));
    c = EaseLook(c, hover, EaseAmount(0.1f, 0.08f));
    CHECK(Near(a.scale, c.scale, 1e-4f) && Near(a.outlineAmount, c.outlineAmount, 1e-4f) && Near(a.tint.r, c.tint.r, 1e-4f));
    CHECK(a.scale > 1 && a.scale < b.hoverScale);
    // 0 s: at once; near enough: on the target (no hair-thin line left).
    CHECK(EaseLook({}, hover, EaseAmount(0.016f, 0)).scale == hover.scale);
    UiButtonLook almost = hover;
    almost.outlineAmount = 0.99995f;
    CHECK(EaseLook(almost, hover, 0.5f).outlineAmount == 1.f);
    // The outline seen at once at its own width (only its amount grows) ...
    CHECK(Near(a.outline, b.hoverOutline));
    // ... and back to normal it fades out, never thinner, never toward white.
    b.hoverOutlineColor = {1, 0, 0, 1};
    UiButtonLook now = ButtonTarget(b);
    b.state = static_cast<int>(UiButtonState::Normal);
    const UiButtonLook normal = ButtonTarget(b);
    CHECK(normal.outlineAmount == 0);
    float lastAlpha = 1;
    for (int i = 0; i < 60; ++i) {  // (about 12 times 変わる速さ: on the target)
        now = EaseLook(now, normal, EaseAmount(0.016f, 0.08f));
        const UiStroke s = ButtonOutline(now);
        CHECK(Near(s.width, b.hoverOutline));
        CHECK(s.color.r == 1 && s.color.g == 0 && s.color.a <= lastAlpha);
        lastAlpha = s.color.a;
    }
    CHECK(now.outlineAmount == 0 && ButtonOutline(now).color.a == 0);
    // A thing with its own outline: mixed with it (3 wide: between 3 and the state's 2).
    now.outlineAmount = 0.5f;
    CHECK(Near(ButtonOutline(now, {3, {0, 0, 0, 1}}).width, 2.5f));

    // 掛ける cannot make it lighter; 置き換える gives the colour itself.
    b.state = static_cast<int>(UiButtonState::Hover);
    b.hover = {0.2f, 0.9f, 1, 1};
    const Color red{1, 0, 0, 1};
    CHECK(Paint(red, PaintOf(ButtonTarget(b))) == Color(0.2f, 0, 0, 1));
    b.colorMode = UiButtonColorMode::Replace;
    const Color replaced = Paint(red, PaintOf(ButtonTarget(b)));
    CHECK(Near(replaced.r, 0.2f) && Near(replaced.g, 0.9f) && Near(replaced.b, 1) && Near(replaced.a, 1));
    b.state = static_cast<int>(UiButtonState::Normal);
    CHECK(IsPlain(PaintOf(ButtonTarget(b))) && Paint(red, PaintOf(ButtonTarget(b))) == red);
    // A swapped picture: hover's; pressed without one: the hover's too; normal: its own.
    const AssetRef own{"0123456789abcdef0123456789abcd01"}, lit{"0123456789abcdef0123456789abcd02"};
    b.hoverImage = lit;
    CHECK(ButtonImage(b, own) == own);
    b.state = static_cast<int>(UiButtonState::Pressed);
    CHECK(ButtonImage(b, own) == lit);
    b.interactable = false;
    CHECK(ButtonImage(b, own) == own);

    // Whole frames: the look eased by the service, the sound's moment once (not on the first frame).
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
    CHECK(part->now.scale > 1 && part->now.scale < part->hoverScale);  // on its way
    step = StepUi(s, t, on, true, true, false, actions, &none, &none, 0.016f, state);
    CHECK(step.hovered.empty());  // pressed on it: no second sound
    step = StepUi(s, t, on, false, false, true, actions, &none, &none, 0.016f, state);
    CHECK(step.hovered.empty() && (step.clicked == std::vector<int>{id}));
    StepUi(s, t, off, false, false, false, actions, &none, &none, 0.016f, state);
    step = StepUi(s, t, on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK((step.hovered == std::vector<int>{id}));  // left and came back: again
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
    // Each kind shows its own fields only (ShowIf).
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

    // A variable changed: ON / OFF / 入れ替える, 足す with 下限 / 上限, an int rounded.
    using game_production::GlobalType;
    using game_production::GlobalValue;
    GlobalValue flag{GlobalType::Bool};
    a.change = UiVariableChange::Toggle;
    CHECK(ChangeVariable(flag, a, nullptr) && flag.v[0] == 1);
    CHECK(ChangeVariable(flag, a, nullptr) && flag.v[0] == 0);
    a.change = UiVariableChange::Add;
    CHECK(!ChangeVariable(flag, a, nullptr));  // (足す on ON / OFF: no)
    GlobalValue n{GlobalType::Int};
    a.amount = 2.6f;
    a.useMin = false;
    a.useMax = true;
    a.max = 5;
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 3);
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 5);  // (kept to the 上限)
    a.change = UiVariableChange::Set;
    a.amount = -4;
    a.useMin = true;
    a.min = 7;  // both used the wrong way round: turned round (5..7)
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 5);
    a.useMax = false;  // only 下限 7: never turned round
    CHECK(ChangeVariable(n, a, nullptr) && n.v[0] == 7);
    GlobalValue text{GlobalType::String};
    std::string why;
    CHECK(!ChangeVariable(text, a, &why) && !why.empty());

    // Whole buttons: two windows (one with a canvas inside it), a button doing everything in order.
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
    add(UiActionKind::SetVariable).variable = "nothing";  // (not there: told)
    UiFocus state;
    RunActions(s, b, title, *button, state, host);
    CHECK(s.Find(menu)->data.active && globals.Find("paused")->v[0] == 1);
    CHECK((done == std::vector<std::string>{"scene", "beep", "quit", "warn"}));
    // 切り替える: only that window (the canvas inside Title is a part of it - left as it is).
    button->actions.clear();
    add(UiActionKind::SwitchWindow).window.id = static_cast<uint64_t>(menu);
    RunActions(s, b, title, *button, state, host);
    CHECK(!s.Find(title)->data.active && s.Find(menu)->data.active && s.Find(inner)->data.active);

    // 前のウィンドウに戻る: Menu opened over Title (seen by a frame), its button closes it - Title again.
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
    s.Find(title)->data.active = false;  // (switched: Title hidden, Menu shown)
    s.Find(menu)->data.active = true;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, play);
    RunActions(s, back, menu, *backButton, play, host);
    CHECK(!s.Find(menu)->data.active && s.Find(title)->data.active);
    done.clear();
    RunActions(s, back, title, *backButton, play, host);  // (Title was there from the start: nothing)
    CHECK(s.Find(title)->data.active && (done == std::vector<std::string>{"info"}));
}

RM_TEST(GameUi_HiddenButton_StartsAgainFromNormal) {
    // A button under the mouse whose window is hidden, then shown again under the mouse: its 乗った sound
    // comes again (not lost), its look starts from normal.
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
    s.Find(canvas)->data.active = false;  // (hidden: by a button's action, a script)
    StepUi(s, BuildUi(s, {}), on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK(!part->hot && part->now.scale == 1.f);
    s.Find(canvas)->data.active = true;
    step = StepUi(s, BuildUi(s, {}), on, false, false, false, actions, &none, &none, 0.016f, state);
    CHECK((step.hovered == std::vector<int>{b}));
}

RM_TEST(GameUi_ScriptFocus_KeptWhenItsWindowComesInFront) {
    // A script's rm::UI::Focus in the frame its window is first shown (a Start, a Show then Focus): kept,
    // not replaced by the window's first button.
    Scene s = UiScene();
    const int canvas = s.Create("Menu");
    Add<UiCanvas>(s, canvas)->reference = {1280, 720};
    const int a = MakeButton(s, canvas, {0, -100}, {100, 50}, "A");
    const int b = MakeButton(s, canvas, {0, 100}, {100, 50}, "B");
    InputFrame none;
    const auto actions = game_production::DefaultInputActions();
    UiFocus chosen;
    chosen.focus = b;  // (what UiService::Focus does)
    chosen.scriptChose = true;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, chosen);
    CHECK(chosen.focus == b && !chosen.scriptChose);
    UiFocus plain;
    StepUi(s, BuildUi(s, {}), std::nullopt, false, false, false, actions, &none, &none, 0.016f, plain);
    CHECK(plain.focus == a);  // (none chosen: the top left one)
}

RM_TEST(GameUi_NextButton_LinksFirst) {
    // 上 / 下 / 左 / 右で移る先: the link when it can be pressed in the same window; else the nearest that way.
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
    CHECK(NextButton(t, canvas, a, 0, 1) == a);  // (no link down, no button below)
    part(a)->down.id = static_cast<uint64_t>(b);
    CHECK(NextButton(t, canvas, a, 0, 1) == b);  // (a link goes any way it says)
    part(c)->interactable = false;
    t = BuildUi(s, {});
    CHECK(NextButton(t, canvas, a, 1, 0) == b);  // (the link cannot be pressed: the nearest)
}

RM_TEST(GameUi_Motion_EaseTransitionAndPath) {
    // Every curve starts at 0 and ends at 1; 行き過ぎて戻る goes past 1 on the way.
    for (const UiEase e : {UiEase::Linear, UiEase::In, UiEase::Out, UiEase::InOut, UiEase::Bounce, UiEase::Back}) {
        CHECK(Near(Ease(e, 0), 0, 1e-4f) && Near(Ease(e, 1), 1, 1e-4f));
    }
    CHECK(Ease(UiEase::In, 0.5f) < 0.5f && Ease(UiEase::Out, 0.5f) > 0.5f);
    bool past = false;
    for (int i = 1; i < 20; ++i) past |= Ease(UiEase::Back, static_cast<float>(i) / 20) > 1.f;
    CHECK(past);
    // 出るとき: from its values (transparent, 80 to the left, half the size) to the thing as it is.
    UiTransition t;
    t.duration = 1;
    t.delay = 0.5f;
    t.ease = UiEase::Linear;
    t.slide = UiSlide::Left;
    t.distance = 80;
    t.scale = 0.5f;
    UiLive l = TransitionAt(t, true, 0.25f);  // (still waiting: as at its beginning)
    CHECK(Near(l.alpha, 0) && Near(l.offset.x, -80) && Near(l.scale, 0.5f));
    l = TransitionAt(t, true, 1.0f);
    CHECK(Near(l.alpha, 0.5f) && Near(l.offset.x, -40) && Near(l.scale, 0.75f) && !TransitionDone(t, 1.0f));
    CHECK(TransitionAt(t, true, 1.5f) == UiLive{} && TransitionDone(t, 1.5f));
    // 消えるとき: the other way.
    l = TransitionAt(t, false, 1.5f);
    CHECK(Near(l.alpha, 0) && Near(l.offset.x, -80));
    t.enabled = false;
    CHECK(TransitionDone(t, 0) && TransitionAt(t, true, 0) == UiLive{});
    // A path: straight, its length and where it is along it.
    UiPath p;
    p.points = {{0, 0}, {100, 0}, {100, 50}};
    p.smooth = false;
    UiPathLine line = PathLine(p);
    CHECK(Near(line.Length(), 150));
    UiPathPose pose = PathPoseAt(line, 125);
    CHECK(Near(pose.offset.x, 100) && Near(pose.offset.y, 25) && Near(pose.angle, 90));
    p.closed = true;
    CHECK(Near(PathLine(p).Length(), 150 + std::hypot(100.f, 50.f)));
    // Curved: through every point.
    p.closed = false;
    p.smooth = true;
    line = PathLine(p);
    CHECK(line.Length() > 150 * 0.95f);
    bool throughMiddle = false;
    for (const Vec2 v : line.points) throughMiddle |= Near(v.x, 100, 1e-3f) && Near(v.y, 0, 1e-3f);
    CHECK(throughMiddle);
    // Walked: 1 回 stops at the end, くり返す goes round, 往復 comes back facing the other way.
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
    pose = StepPath(loop, line, 1.0f);  // 200 along a 150 line: 50 into the next round
    CHECK(loop.playing && Near(pose.offset.x, 50));
    UiPath back = p;
    back.started = false;
    back.loop = UiPathLoop::PingPong;
    pose = StepPath(back, line, 1.75f);  // 175: 150 there, 25 back (up the last piece)
    CHECK(back.backward && Near(pose.offset.x, 100) && Near(pose.offset.y, 25) && Near(pose.angle, 180 + 90, 1e-2f));
    // Stopped until a script plays it.
    UiPath waits = p;
    waits.started = false;
    waits.playOnStart = false;
    pose = StepPath(waits, line, 1.0f);
    CHECK(!waits.playing && Near(pose.offset.x, 0));
}

RM_TEST(GameUi_Motion_ShowHideAndTweens) {
    // A window with 出る・消える動き: it comes in at the start; switched off, it stays laid out (not
    // clicked, not chosen) until its 消えるとき ends.
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
    // Shown again: it comes in again.
    s.Find(canvas)->data.active = true;
    StepMotion(s, 0.1f, world);
    CHECK(m->phase == static_cast<int>(UiMotionPhase::Showing));
    // ゲームの始めにも off: there at once at the start.
    UiMotionWorld fresh;
    m->seen = false;
    m->atStart = false;
    StepMotion(s, 0.01f, fresh);
    CHECK(m->phase == static_cast<int>(UiMotionPhase::Still) && Near(BuildUi(s, {}).Find(b)->alpha, 1));
    // The scripts' tweens: 位置 and 回転 themselves, the size and the transparency kept after they end.
    CHECK(StartTween(s, fresh, b, UiTweenKind::Move, {100, 0}, 1, UiEase::Linear));
    CHECK(StartTween(s, fresh, b, UiTweenKind::Fade, {0.2f, 0}, 0, UiEase::Linear));  // (at once)
    StepMotion(s, 0.5f, fresh);
    auto* rect = static_cast<UiRect*>(s.Find(b)->FindComponent(reflect::TypeOf<UiRect>().name)->value.Get());
    CHECK(Near(rect->position.x, 50) && Near(rect->live.alpha, 0.2f));
    StepMotion(s, 1.0f, fresh);
    CHECK(Near(rect->position.x, 100) && Near(rect->live.alpha, 0.2f) && !fresh.tweens[b].move.active);
    CHECK(!StartTween(s, fresh, canvas, UiTweenKind::Move, {0, 0}, 1, UiEase::Linear));  // (a window without UI の配置: no 位置)
    // A path walked moves it (its box), a script stops it.
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
    // 見た目 (UI-5): one place decides each thing - the 立体 drawn only when used and with a width; pressed
    // in when へこませる, or while its button is held (押すとへこむ); the corners never past half a side.
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
    CHECK(!BevelInset(st, &b) && BevelInset(st, nullptr));  // (pressed: out again)
    st.rounding = 50;
    CHECK(Near(Rounding(st, {0, 0, 200, 40}), 20) && Near(Rounding(st, {0, 0, 200, 400}), 50));
    st.rounding = -3;
    CHECK(Near(Rounding(st, {0, 0, 10, 10}), 0));
    // A thing with only a 背景 (no image) takes the mouse - and hides what is under it.
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
