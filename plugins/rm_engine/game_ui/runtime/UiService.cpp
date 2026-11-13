#include "rm_engine/game_ui/runtime/UiService.h"

#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include <algorithm>
#include <cmath>

namespace rm::game_ui {

namespace {

using game_production::EvaluateAction;
using game_production::FindAction;
using game_production::InputAction;
using game_production::InputBinding;
using game_production::InputControl;
using game_production::InputFrame;

// The action of that name, or the usual keys when the game settings have none of it.
InputAction ActionOr(std::span<const InputAction> actions, const std::string& name, InputAction fallback) {
    if (const InputAction* a = FindAction(actions, name)) return *a;
    return fallback;
}

bool WentDown(const InputAction& a, const InputFrame& now, const InputFrame& before) {
    return EvaluateAction(a, now).down && !EvaluateAction(a, before).down;
}

// A step of an axis: it crossed 0.5 (either way) this frame.
int Step(const InputAction& a, const InputFrame& now, const InputFrame& before) {
    const float v = EvaluateAction(a, now).axis, b = EvaluateAction(a, before).axis;
    if (v >= 0.5f && b < 0.5f) return 1;
    if (v <= -0.5f && b > -0.5f) return -1;
    return 0;
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

}  // namespace

UiKeys ReadUiKeys(const UiCanvas& canvas, std::span<const InputAction> actions, const InputFrame& now, const InputFrame& before) {
    using C = InputControl;
    const InputAction submit = ActionOr(actions, canvas.submitAction, {"Submit", {{C::Enter, 1}, {C::Space, 1}, {C::PadA, 1}}});
    const InputAction cancel = ActionOr(actions, canvas.cancelAction, {"Cancel", {{C::Escape, 1}, {C::PadB, 1}}});
    const InputAction x = ActionOr(actions, canvas.moveXAction,
                                   {"MoveX", {{C::Left, -1}, {C::Right, 1}, {C::PadLeftStickX, 1}, {C::PadLeft, -1}, {C::PadRight, 1}}});
    const InputAction y = ActionOr(actions, canvas.moveYAction,
                                   {"MoveY", {{C::Down, -1}, {C::Up, 1}, {C::PadLeftStickY, 1}, {C::PadDown, -1}, {C::PadUp, 1}}});
    UiKeys k;
    k.submit = WentDown(submit, now, before);
    k.cancel = WentDown(cancel, now, before);
    k.dx = Step(x, now, before);
    k.dy = -Step(y, now, before);  // (MoveY: up is +; the screen's Y goes down)
    return k;
}

std::vector<int> StepUi(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown, bool wentUp,
                        std::span<const InputAction> actions, const InputFrame* now, const InputFrame* before, UiFocus& state) {
    std::vector<int> clicked;
    // The mouse.
    const PointerResult pr = StepPointer(tree, pointer, down, wentDown, wentUp, state.pointer);
    const bool moved = pointer && (!state.lastPointer || std::abs(pointer->x - state.lastPointer->x) + std::abs(pointer->y - state.lastPointer->y) > 0.5f);
    if (moved || wentDown) state.keyboard = false;  // the mouse is used: the focus is not shown
    state.lastPointer = pointer;
    if (pr.clicked) {
        clicked.push_back(pr.clicked);
        state.focus = pr.clicked;  // (the keys go on from there)
    }
    // The keys, in the frontmost canvas that takes them.
    if (const int canvas = NavigationCanvas(tree); canvas && now && before) {
        const UiNode* cn = tree.Find(canvas);
        if (!PressableIn(tree, canvas, state.focus)) state.focus = 0;
        const UiKeys keys = ReadUiKeys(*cn->canvasPart, actions, *now, *before);
        if (keys.dx || keys.dy) {
            const bool first = !state.focus || !state.keyboard;
            if (!state.focus) state.focus = FirstFocus(tree, canvas, static_cast<int>(cn->canvasPart->firstButton.id));
            if (!first) state.focus = MoveFocus(tree, canvas, state.focus, keys.dx, keys.dy);
            state.keyboard = true;
        }
        if (keys.submit) {
            if (!state.focus) state.focus = FirstFocus(tree, canvas, static_cast<int>(cn->canvasPart->firstButton.id));
            if (state.focus && state.keyboard) clicked.push_back(state.focus);
            state.keyboard = true;  // (the first 決定 shows the focus; the next presses it)
        }
        if (keys.cancel && PressableIn(tree, canvas, static_cast<int>(cn->canvasPart->cancelButton.id))) {
            clicked.push_back(static_cast<int>(cn->canvasPart->cancelButton.id));
        }
    } else {
        state.focus = 0;
    }
    // How each button looks this frame.
    for (const UiNode& n : tree.nodes) {
        if (!n.button) continue;
        UiButton* b = MutableButton(scene, n.entity);
        if (!b) continue;
        // Pressed while held on it; 乗った only when no other button is held (dragging off one onto another).
        const UiButtonState s = !b->interactable                             ? UiButtonState::Disabled
                                : pr.held == n.entity                        ? UiButtonState::Pressed
                                : pr.hovered == n.entity && !state.pointer.pressed ? UiButtonState::Hover
                                                                             : UiButtonState::Normal;
        b->state = static_cast<int>(s);
        b->focused = state.keyboard && state.focus == n.entity;
    }
    return clicked;
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
    const bool down = f.input && game_production::ControlValue(*f.input, InputControl::MouseLeft) >= 0.5f;
    const bool before = f.inputBefore && game_production::ControlValue(*f.inputBefore, InputControl::MouseLeft) >= 0.5f;
    const std::span<const InputAction> actions = f.actions ? std::span<const InputAction>(*f.actions) : std::span<const InputAction>();
    const std::vector<int> clicked = StepUi(f.scene, tree, f.pointer, down, down && !before, !down && before, actions, f.input, f.inputBefore, state_);
    for (const int id : clicked) {
        if (f.hooks) f.hooks->push_back({id, RM_HOOK_CLICK, 0});
        const game_production::SceneEntity* e = f.scene.Find(id);
        if (host_.log && e) host_.log(0, "ゲーム UI: 「" + e->data.name + "」を押しました");
    }
}

std::unique_ptr<game_production::PlayService> CreateUiService(const game_production::PlayHost& host) { return std::make_unique<UiService>(host); }

}  // namespace rm::game_ui
