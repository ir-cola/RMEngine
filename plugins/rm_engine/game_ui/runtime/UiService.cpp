#include "rm_engine/game_ui/runtime/UiService.h"

#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include <algorithm>
#include <cmath>

namespace rm::game_ui {

namespace {

using game_production::ActionOrStandard;
using game_production::EvaluateAction;
using game_production::InputAction;
using game_production::InputControl;
using game_production::InputFrame;

// An action's push this frame (0..1: its axis; a stick pushed the other way is 0).
float Push(std::span<const InputAction> actions, const char* name, const InputFrame& f) {
    const InputAction* a = ActionOrStandard(actions, name);
    return a ? std::max(0.f, EvaluateAction(*a, f).axis) : 0.f;
}

// The direction held on a frame: the stronger way only (a stick pushed aslant goes one way), screen Y down.
void Held(std::span<const InputAction> actions, const InputFrame& f, int& dx, int& dy) {
    using namespace game_production;
    const float h = Push(actions, kUiRight, f) - Push(actions, kUiLeft, f);
    const float v = Push(actions, kUiDown, f) - Push(actions, kUiUp, f);
    dx = dy = 0;
    if (std::abs(h) >= 0.5f && std::abs(h) > std::abs(v)) dx = h > 0 ? 1 : -1;
    if (std::abs(v) >= 0.5f && std::abs(v) >= std::abs(h)) dy = v > 0 ? 1 : -1;
}

UiButton* MutableButton(game_production::Scene& scene, int entity) {
    game_production::SceneEntity* e = scene.Find(entity);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<UiButton>().name) : nullptr;
    return c && c->Known() ? static_cast<UiButton*>(c->value.Get()) : nullptr;
}

bool PressableIn(const UiTree& tree, int canvas, int entity) {
    const UiNode* n = entity ? tree.Find(entity) : nullptr;
    return n && n->canvas == canvas && n->button && n->button->interactable;
}

bool Contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// The windows shown: what opened since last frame is remembered with what it hid (戻る's way back).
void FollowWindows(const UiTree& tree, UiFocus& state) {
    std::vector<int> now;
    for (const UiNode& n : tree.nodes) {
        if (n.parent < 0) now.push_back(n.entity);
    }
    if (!state.started) {
        state.started = true;  // (the windows of the start: nothing to go back to)
    } else {
        std::vector<int> gone;
        for (const int w : state.shown) {
            if (!Contains(now, w)) gone.push_back(w);
        }
        for (const int w : now) {
            if (!Contains(state.shown, w)) state.visits.push_back({w, gone});
        }
        std::erase_if(state.visits, [&](const UiVisit& v) { return !Contains(now, v.window); });  // closed some other way
    }
    state.shown = std::move(now);
}

}  // namespace

UiKeys ReadUiKeys(std::span<const InputAction> actions, const InputFrame& now, const InputFrame& before) {
    using namespace game_production;
    UiKeys k;
    Held(actions, now, k.holdDx, k.holdDy);
    int bx = 0, by = 0;
    Held(actions, before, bx, by);
    if ((k.holdDx || k.holdDy) && (k.holdDx != bx || k.holdDy != by)) {
        k.dx = k.holdDx;
        k.dy = k.holdDy;
    }
    k.submit = Push(actions, kUiSubmit, now) >= 0.5f && Push(actions, kUiSubmit, before) < 0.5f;
    k.cancel = Push(actions, kUiCancel, now) >= 0.5f && Push(actions, kUiCancel, before) < 0.5f;
    return k;
}

UiStep StepUi(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown, bool wentUp,
              std::span<const InputAction> actions, const InputFrame* now, const InputFrame* before, float dt, UiFocus& state) {
    UiStep step;
    FollowWindows(tree, state);
    // The mouse.
    const PointerResult pr = StepPointer(tree, pointer, down, wentDown, wentUp, state.pointer);
    const bool moved = pointer && state.lastPointer && std::abs(pointer->x - state.lastPointer->x) + std::abs(pointer->y - state.lastPointer->y) > 0.5f;
    if (moved || (pointer && wentDown)) state.keyboard = false;  // the mouse is used: the choice is not shown
    state.lastPointer = pointer;
    if (pr.clicked) {
        step.clicked.push_back(pr.clicked);
        state.focus = pr.clicked;  // (the keys go on from there)
    }
    // The keys and the pad, in the frontmost window that takes them: it always has a chosen button.
    const int window = NavigationCanvas(tree);
    const UiNode* wn = window ? tree.Find(window) : nullptr;
    if (window != state.window) {
        state.window = window;
        state.focus = wn ? FirstFocus(tree, window, static_cast<int>(wn->canvasPart->firstButton.id)) : 0;
    } else if (wn && !PressableIn(tree, window, state.focus)) {
        state.focus = FirstFocus(tree, window, static_cast<int>(wn->canvasPart->firstButton.id));
    }
    if (wn && now && before) {
        UiKeys keys = ReadUiKeys(actions, *now, *before);
        // Held: a step after kRepeatDelay, then every kRepeatEvery.
        if (keys.holdDx || keys.holdDy) {
            if (keys.dx || keys.dy || keys.holdDx != state.heldDx || keys.holdDy != state.heldDy) {
                state.heldDx = keys.holdDx;
                state.heldDy = keys.holdDy;
                state.heldFor = 0;
                state.repeatAt = kRepeatDelay;
            } else {
                state.heldFor += std::max(0.f, dt);
                if (state.heldFor >= state.repeatAt) {
                    keys.dx = keys.holdDx;
                    keys.dy = keys.holdDy;
                    state.repeatAt += kRepeatEvery;
                }
            }
        } else {
            state.heldDx = state.heldDy = 0;
        }
        if (keys.dx || keys.dy) {
            // The first step after the mouse only shows the choice again; then each moves it.
            if (state.keyboard && state.focus) state.focus = NextButton(tree, window, state.focus, keys.dx, keys.dy);
            state.keyboard = true;
        }
        if (keys.submit && state.focus) {
            step.clicked.push_back(state.focus);
            state.keyboard = true;
        }
        if (keys.cancel) {
            const int cancelButton = static_cast<int>(wn->canvasPart->cancelButton.id);
            if (PressableIn(tree, window, cancelButton)) {
                step.clicked.push_back(cancelButton);
            } else if (const auto it = std::find_if(state.visits.rbegin(), state.visits.rend(), [&](const UiVisit& v) { return v.window == window; });
                       it != state.visits.rend()) {
                // Back: this window off, what it hid on again (and remembered so: no new visit next frame).
                if (game_production::SceneEntity* e = scene.Find(window)) e->data.active = false;
                std::erase(state.shown, window);
                for (const int h : it->hid) {
                    if (game_production::SceneEntity* e = scene.Find(h)) e->data.active = true;
                    if (!Contains(state.shown, h)) state.shown.push_back(h);
                }
                state.visits.erase(std::next(it).base());
                step.closed = window;
            }
        }
    }
    // How each button looks this frame.
    for (const UiNode& n : tree.nodes) {
        if (!n.button) continue;
        UiButton* b = MutableButton(scene, n.entity);
        if (!b) continue;
        // Pressed while held on it; 乗った only when no other button is held (dragging off one onto another).
        const UiButtonState s = !b->interactable                                    ? UiButtonState::Disabled
                                : pr.held == n.entity                               ? UiButtonState::Pressed
                                : pr.hovered == n.entity && !state.pointer.pressed ? UiButtonState::Hover
                                                                                    : UiButtonState::Normal;
        b->state = static_cast<int>(s);
        b->focused = state.keyboard && state.focus == n.entity && n.canvas == window;
    }
    return step;
}

UiScreen ScreenOf(const game_production::GameSettings& settings, Vec2 size) {
    UiScreen s;
    s.size = size;
    // The safe area is set in DIPs of the game settings' 画面: scaled to the screen it is drawn on.
    const float kx = settings.display.width > 0 ? size.x / static_cast<float>(settings.display.width) : 1.f;
    const float ky = settings.display.height > 0 ? size.y / static_cast<float>(settings.display.height) : 1.f;
    s.safeTopLeft = {settings.display.safeAreaTopLeft.x * kx, settings.display.safeAreaTopLeft.y * ky};
    s.safeBottomRight = {settings.display.safeAreaBottomRight.x * kx, settings.display.safeAreaBottomRight.y * ky};
    return s;
}

void UiService::Frame(const game_production::PlayFrame& f) {
    if (!host_.settings) return;
    const UiTree tree = BuildUi(f.scene, ScreenOf(*host_.settings, f.screen));
    // (the mouse is the pointer - not a binding of an action)
    const bool down = f.input && game_production::ControlValue(*f.input, InputControl::MouseLeft) >= 0.5f;
    const bool before = f.inputBefore && game_production::ControlValue(*f.inputBefore, InputControl::MouseLeft) >= 0.5f;
    const std::span<const InputAction> actions = f.actions ? std::span<const InputAction>(*f.actions) : std::span<const InputAction>();
    const UiStep step = StepUi(f.scene, tree, f.pointer, down, down && !before, !down && before, actions, f.input, f.inputBefore, f.dt, state_);
    for (const int id : step.clicked) {
        if (f.hooks) f.hooks->push_back({id, RM_HOOK_CLICK, 0});
        const game_production::SceneEntity* e = f.scene.Find(id);
        if (host_.log && e) host_.log(0, "ゲーム UI: 「" + e->data.name + "」を押しました");
    }
    if (step.closed && host_.log) {
        if (const game_production::SceneEntity* e = f.scene.Find(step.closed)) host_.log(0, "ゲーム UI: 戻る で「" + e->data.name + "」を閉じました");
    }
}

std::unique_ptr<game_production::PlayService> CreateUiService(const game_production::PlayHost& host) { return std::make_unique<UiService>(host); }

}  // namespace rm::game_ui
