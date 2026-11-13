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
                         &reflect::TypeOf<UiButton>(), &reflect::TypeOf<UiBar>()},
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
    CHECK(ButtonColor(*button) == button->pressed && Near(ButtonScale(*button), button->pressedScale));
    button->state = static_cast<int>(UiButtonState::Normal);
    button->focused = true;
    CHECK(ButtonColor(*button) == button->hover && Near(ButtonScale(*button), button->hoverScale));
    button->interactable = false;
    CHECK(ButtonColor(*button) == button->disabled && Near(ButtonScale(*button), 1));
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
    CHECK(MoveFocus(t, canvas, tl, 1, 0) == tr);
    CHECK(MoveFocus(t, canvas, tl, 0, 1) == bl);
    CHECK(MoveFocus(t, canvas, tr, 0, 1) == br);
    CHECK(MoveFocus(t, canvas, tl, -1, 0) == tl);  // nothing that way: stays
    // The keys of two frames: Enter went down = 決定; the right arrow = a step to the right.
    InputFrame before, now;
    now.keys[0x0D] = 1;
    now.keys[0x27] = 1;  // VK_RIGHT
    const std::vector<game_production::InputAction> actions = game_production::DefaultInputActions();
    const UiKeys k = ReadUiKeys(UiCanvas{}, actions, now, before);
    CHECK(k.submit && k.dx == 1 && k.dy == 0 && !k.cancel);
    // A game made before the UI's actions (no Submit in its 入力): Enter still works.
    const UiKeys old = ReadUiKeys(UiCanvas{}, {}, now, before);
    CHECK(old.submit && old.dx == 1);
    // Up (MoveY +) is a step up the screen.
    InputFrame up;
    up.keys[0x26] = 1;  // VK_UP
    CHECK(ReadUiKeys(UiCanvas{}, actions, up, before).dy == -1);

    // A whole frame: the first step shows the focus on the first button, the next moves it, 決定 presses it.
    UiFocus state;
    InputFrame none;
    InputFrame right;
    right.keys[0x27] = 1;
    InputFrame enter;
    enter.keys[0x0D] = 1;
    std::vector<int> clicked = StepUi(s, t, std::nullopt, false, false, false, actions, &right, &none, state);
    CHECK(clicked.empty() && state.focus == tl && state.keyboard);
    clicked = StepUi(s, t, std::nullopt, false, false, false, actions, &right, &none, state);
    CHECK(state.focus == tr);
    clicked = StepUi(s, t, std::nullopt, false, false, false, actions, &enter, &none, state);
    CHECK((clicked == std::vector<int>{tr}));
    // The buttons' states were written for the drawing: tr focused.
    const auto* trButton = static_cast<const UiButton*>(s.Find(tr)->FindComponent(reflect::TypeOf<UiButton>().name)->value.Get());
    CHECK(trButton->focused);
    // The mouse moved: the focus is no longer shown.
    StepUi(s, t, Vec2{10, 10}, false, false, false, actions, &none, &none, state);
    StepUi(s, t, Vec2{20, 20}, false, false, false, actions, &none, &none, state);
    CHECK(!state.keyboard && !trButton->focused);
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
