#include "rm_engine/game_ui/runtime/UiService.h"

#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

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
        if (n.parent < 0 && !n.closing) now.push_back(n.entity);  // (one going away is closed already)
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
        // A button a script chose this frame (rm::UI::Focus - after Show of its window, in a Start) stays
        // chosen; else the window's 最初に選ぶボタン.
        if (!(state.scriptChose && PressableIn(tree, window, state.focus))) {
            state.focus = wn ? FirstFocus(tree, window, static_cast<int>(wn->canvasPart->firstButton.id)) : 0;
        }
    } else if (wn && !PressableIn(tree, window, state.focus)) {
        state.focus = FirstFocus(tree, window, static_cast<int>(wn->canvasPart->firstButton.id));
    }
    state.scriptChose = false;
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
            } else if (CloseWindow(scene, state, window)) {
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
        // Its look eased toward the state's (the first frame: as it is).
        const UiButtonLook target = ButtonTarget(*b);
        b->now = b->ready ? EaseLook(b->now, target, EaseAmount(dt, b->transition)) : target;
        // 乗った (the mouse, the keys' choice) or pressed: its sound once when it becomes so - pressed on it
        // and let go is one sound, not two. Not on the first frame (the choice shown from the start).
        const bool hot = b->interactable && (s == UiButtonState::Hover || s == UiButtonState::Pressed || b->focused);
        if (hot && !b->hot && b->ready) step.hovered.push_back(n.entity);
        b->hot = hot;
        b->ready = true;
    }
    // The buttons not shown now (their window hidden): back to normal, so shown again they start from
    // there - their look not from where it was, their sound when the mouse comes on them (not lost).
    std::unordered_set<int> shown;
    for (const UiNode& n : tree.nodes) {
        if (n.button) shown.insert(n.entity);
    }
    // (every entity: the switched off ones too - those are the hidden ones)
    for (const auto& t : scene.TreeOrder()) {
        if (shown.contains(t.entity->data.id)) continue;
        UiButton* b = MutableButton(scene, t.entity->data.id);
        if (!b || (!b->hot && b->state == static_cast<int>(UiButtonState::Normal) && !b->focused)) continue;
        b->state = static_cast<int>(UiButtonState::Normal);
        b->focused = false;
        b->hot = false;
        if (b->ready) b->now = ButtonTarget(*b);
    }
    return step;
}

bool IsWindow(const game_production::Scene& scene, int entity) {
    const game_production::SceneEntity* e = scene.Find(entity);
    if (!e || !e->FindComponent(reflect::TypeOf<UiCanvas>().name)) return false;
    for (const game_production::SceneEntity* p = scene.Find(e->data.parent); p; p = scene.Find(p->data.parent)) {
        if (p->FindComponent(reflect::TypeOf<UiCanvas>().name)) return false;
    }
    return true;
}

void SwitchToWindow(game_production::Scene& scene, int window) {
    for (const auto& t : scene.TreeOrder()) {
        const int id = t.entity->data.id;
        if (!IsWindow(scene, id)) continue;
        if (game_production::SceneEntity* e = scene.Find(id)) e->data.active = id == window;
    }
}

bool CloseWindow(game_production::Scene& scene, UiFocus& state, int window) {
    const auto it = std::find_if(state.visits.rbegin(), state.visits.rend(), [&](const UiVisit& v) { return v.window == window; });
    if (it == state.visits.rend()) return false;
    // This window off, what it hid on again (and remembered so: no new visit next frame).
    if (game_production::SceneEntity* e = scene.Find(window)) e->data.active = false;
    std::erase(state.shown, window);
    for (const int h : it->hid) {
        if (game_production::SceneEntity* e = scene.Find(h)) e->data.active = true;
        if (!Contains(state.shown, h)) state.shown.push_back(h);
    }
    state.visits.erase(std::next(it).base());
    return true;
}

bool ChangeVariable(game_production::GlobalValue& v, const UiAction& a, std::string* why) {
    using game_production::GlobalType;
    auto fail = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    const bool number = v.type == GlobalType::Int || v.type == GlobalType::Float;
    if (v.type != GlobalType::Bool && !number) return fail("文字や Vec2 / Vec3 の変数は変えられません (ON / OFF か数の変数)");
    double x = v.v[0];
    switch (a.change) {
        case UiVariableChange::On: x = 1; break;
        case UiVariableChange::Off: x = 0; break;
        case UiVariableChange::Toggle: x = x != 0 ? 0 : 1; break;
        case UiVariableChange::Add:
        case UiVariableChange::Set:
            if (!number) return fail("ON / OFF の変数には足せません (ON にする・OFF にする・入れ替える を使います)");
            x = a.change == UiVariableChange::Add ? x + a.amount : a.amount;
            {
                // 下限 / 上限, each on its own (turned round only when both are used and the wrong way).
                double lo = a.min, hi = a.max;
                if (a.useMin && a.useMax && lo > hi) std::swap(lo, hi);
                if (a.useMin) x = std::max(x, lo);
                if (a.useMax) x = std::min(x, hi);
            }
            break;
    }
    if (!std::isfinite(x)) return fail("数になりません");
    v.v[0] = v.type == GlobalType::Int ? std::round(x) : v.type == GlobalType::Bool ? (x != 0 ? 1.0 : 0.0) : x;
    return true;
}

void RunActions(game_production::Scene& scene, int button, int window, const UiButton& b, UiFocus& state, const UiActionHost& host) {
    const game_production::SceneEntity* self = scene.Find(button);
    const std::string who = self ? self->data.name : std::string();
    auto tell = [&](int level, const std::string& text) {
        if (host.log) host.log(level, "ゲーム UI: 「" + who + "」の押したときの動作: " + text);
    };
    for (const UiAction& a : b.actions) {
        switch (a.kind) {
            case UiActionKind::ShowWindow:
            case UiActionKind::HideWindow:
            case UiActionKind::SwitchWindow: {
                const int w = static_cast<int>(a.window.id);
                game_production::SceneEntity* e = w ? scene.Find(w) : nullptr;
                if (!e || !IsWindow(scene, w)) {
                    tell(1, "ウィンドウ (キャンバスのエンティティ) が選ばれていません");
                    break;
                }
                if (a.kind == UiActionKind::SwitchWindow) {
                    SwitchToWindow(scene, w);
                } else {
                    e->data.active = a.kind == UiActionKind::ShowWindow;
                }
                break;
            }
            case UiActionKind::Back:
                if (!CloseWindow(scene, state, window)) tell(0, "戻る先がありません (このウィンドウは始めから出ていました)");
                break;
            case UiActionKind::LoadScene:
                if (a.scene.Empty()) {
                    tell(1, "シーンが選ばれていません");
                } else if (!host.loadScene || !host.loadScene(a.scene)) {
                    tell(1, "そのシーンはゲームのシーンの一覧にありません (シーン パネルで足します)");
                }
                break;
            case UiActionKind::SetVariable: {
                game_production::GlobalValue* v = host.globals ? host.globals->Find(a.variable) : nullptr;
                std::string why;
                if (!v) {
                    tell(1, "「" + a.variable + "」というグローバル変数がありません (ゲームの設定の「グローバル変数」)");
                } else if (!ChangeVariable(*v, a, &why)) {
                    tell(1, "「" + a.variable + "」: " + why);
                }
                break;
            }
            case UiActionKind::PlaySound:
                if (!a.sound.Empty() && host.playSound) host.playSound(a.sound, a.volume);
                break;
            case UiActionKind::Quit:
                if (host.quit) host.quit();
                break;
        }
    }
}

namespace {

template <class T>
T* Part(game_production::SceneEntity& e) {
    game_production::Component* c = e.FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

// One tween's value now (and whether it has ended).
Vec2 TweenAt(UiTween& t, float dt) {
    t.time += std::max(dt, 0.f);
    const float f = t.seconds > 1e-4f ? t.time / t.seconds : 1.f;
    if (f >= 1) t.active = false;
    const float e = Ease(t.ease, f);
    return {t.from.x + (t.to.x - t.from.x) * e, t.from.y + (t.to.y - t.from.y) * e};
}

}  // namespace

void StepMotion(game_production::Scene& scene, float dt, UiMotionWorld& world) {
    const bool first = !world.started;
    world.started = true;
    for (const auto& t : scene.TreeOrder()) {
        const int id = t.entity->data.id;
        game_production::SceneEntity* e = scene.Find(id);
        if (!e) continue;
        UiRect* place = Part<UiRect>(*e);
        UiCanvas* canvas = Part<UiCanvas>(*e);
        if (!place && !canvas) continue;
        UiLive live;
        // 出る・消える動き: started when it comes on (or at the start), ended - and its window let go - at its end.
        if (UiMotion* m = Part<UiMotion>(*e)) {
            const bool shown = t.activeInHierarchy;
            auto phase = static_cast<UiMotionPhase>(m->phase);
            if (shown && !m->seen) {
                phase = first && !m->atStart ? UiMotionPhase::Still : UiMotionPhase::Showing;
                m->time = 0;
            } else if (!shown && m->seen) {
                // Switched off itself (not with a parent): it goes as its 消えるとき says.
                phase = !e->data.active && m->hide.enabled ? UiMotionPhase::Hiding : UiMotionPhase::Still;
                m->time = 0;
            } else if (!shown && phase == UiMotionPhase::Hiding && e->data.active) {
                phase = UiMotionPhase::Still;  // (a parent switched off meanwhile: gone with it)
            }
            m->seen = shown;
            if (phase != UiMotionPhase::Still) {
                m->time += std::max(dt, 0.f);
                const bool showing = phase == UiMotionPhase::Showing;
                const UiTransition& tr = showing ? m->show : m->hide;
                live = TransitionAt(tr, showing, m->time);
                if (TransitionDone(tr, m->time)) {
                    phase = UiMotionPhase::Still;
                    if (showing) live = {};
                }
            }
            m->phase = static_cast<int>(phase);
        }
        // 道に沿って動く: walked while it is shown.
        if (UiPath* p = Part<UiPath>(*e); p && place && t.activeInHierarchy) {
            const UiPathPose pose = StepPath(*p, PathLine(*p), dt);
            UiLive along;
            along.offset = pose.offset;
            if (p->orient) along.turn = pose.angle;
            live = Combine(live, along);
        }
        // The scripts' tweens: 位置 and 回転 themselves; the size and the transparency kept here.
        if (const auto it = world.tweens.find(id); it != world.tweens.end()) {
            UiTweens& tw = it->second;
            if (tw.move.active && place) place->position = TweenAt(tw.move, dt);
            if (tw.turn.active && place) place->rotation = TweenAt(tw.turn, dt).x;
            if (tw.scale.active) tw.scaleNow = std::max(0.f, TweenAt(tw.scale, dt).x);
            if (tw.fade.active) tw.alphaNow = std::clamp(TweenAt(tw.fade, dt).x, 0.f, 1.f);
            UiLive tl;
            tl.scale = tw.scaleNow;
            tl.alpha = tw.alphaNow;
            live = Combine(live, tl);
        }
        if (place) place->live = live;
        if (canvas) canvas->live = live;
    }
}

bool StartTween(game_production::Scene& scene, UiMotionWorld& world, int entity, UiTweenKind kind, Vec2 to, float seconds, UiEase ease) {
    game_production::SceneEntity* e = scene.Find(entity);
    UiRect* place = e ? Part<UiRect>(*e) : nullptr;
    if (!place && !(e && Part<UiCanvas>(*e) && (kind == UiTweenKind::Scale || kind == UiTweenKind::Fade))) return false;
    UiTweens& tw = world.tweens[entity];
    UiTween* t = nullptr;
    Vec2 from{0, 0};
    switch (kind) {
        case UiTweenKind::Move: t = &tw.move, from = place->position; break;
        case UiTweenKind::Turn: t = &tw.turn, from = {place->rotation, 0}; break;
        case UiTweenKind::Scale: t = &tw.scale, from = {tw.scaleNow, 0}; break;
        case UiTweenKind::Fade: t = &tw.fade, from = {tw.alphaNow, 0}; break;
    }
    *t = {true, from, to, 0, std::isfinite(seconds) ? std::max(seconds, 0.f) : 0.f, ease};
    return true;
}

bool PlayPath(game_production::Scene& scene, int entity, int how) {
    game_production::SceneEntity* e = scene.Find(entity);
    UiPath* p = e ? Part<UiPath>(*e) : nullptr;
    if (!p) return false;
    p->started = true;
    if (how == 2) {
        p->travelled = 0;
        p->backward = false;
    }
    p->playing = how != 0;
    return true;
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
    StepMotion(f.scene, f.dt, motion_);  // (first: what is going away is still laid out - not clicked)
    const UiTree tree = BuildUi(f.scene, ScreenOf(*host_.settings, f.screen));
    // (the mouse is the pointer - not a binding of an action)
    const bool down = f.input && game_production::ControlValue(*f.input, InputControl::MouseLeft) >= 0.5f;
    const bool before = f.inputBefore && game_production::ControlValue(*f.inputBefore, InputControl::MouseLeft) >= 0.5f;
    const std::span<const InputAction> actions = f.actions ? std::span<const InputAction>(*f.actions) : std::span<const InputAction>();
    // Went down / up: what the window was told, or what changed since the last frame. A quick click has
    // both in one frame (the button up at both looks): StepPointer takes that as a click.
    const bool wentDown = f.pointerWentDown || (down && !before);
    const bool wentUp = f.pointerWentUp || (!down && before);
    const UiStep step = StepUi(f.scene, tree, f.pointer, down, wentDown, wentUp, actions, f.input, f.inputBefore, f.dt, state_);
    // For each button pressed: its sound, then what it does, then the scripts' OnClick (spec §6-6: an
    // OnClick reads what the actions did; the scene and the end are asked, done after the frame).
    game_production::SoundPlayer* sounds = f.services ? f.services->Sounds() : nullptr;
    auto play = [&](int id, bool click) {
        const UiButton* b = MutableButton(f.scene, id);
        if (!b || !sounds) return;
        const AssetRef& sound = click ? b->clickSound : b->hoverSound;
        if (!sound.Empty()) sounds->PlaySound(sound, click ? b->clickVolume : b->hoverVolume);
    };
    for (const int id : step.hovered) play(id, false);
    // 動画 play their sound themselves: as loud as music is now (全体 × 音楽).
    const float musicGain = sounds ? sounds->MusicVolume() : host_.settings->volume.master * host_.settings->volume.music;
    for (const auto& t : f.scene.TreeOrder()) {
        game_production::SceneEntity* e = f.scene.Find(t.entity->data.id);
        game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<UiVideo>().name) : nullptr;
        if (c && c->Known()) static_cast<UiVideo*>(c->value.Get())->gain = musicGain;
    }
    UiActionHost actionHost;
    if (game_production::ScriptWorld* world = f.world) {
        actionHost.globals = &world->globals;
        actionHost.loadScene = [this, world](const AssetRef& scene) {
            // The scene by its id (two of one name in two folders are two); else by its file's name, as
            // Game::LoadScene("…") takes it.
            std::optional<size_t> at;
            for (size_t i = 0; i < world->sceneIds.size() && !at; ++i) {
                if (world->sceneIds[i] == scene.guid) at = i;
            }
            const std::string file = at || !host_.name ? std::string() : host_.name(scene);
            if (!at && !file.empty()) at = game_production::FindSceneByName(world->sceneNames, game_production::SceneNameOf(file));
            if (!at) return false;
            world->sceneAsked = static_cast<int>(*at);
            return true;
        };
        actionHost.quit = [world] { world->quitAsked = true; };
    }
    if (sounds) actionHost.playSound = [sounds](const AssetRef& s, float v) { sounds->PlaySound(s, v); };
    actionHost.log = host_.log;
    for (const int id : step.clicked) {
        play(id, true);
        const UiButton* b = MutableButton(f.scene, id);
        const UiNode* n = tree.Find(id);
        if (b && !b->actions.empty()) RunActions(f.scene, id, n ? n->canvas : 0, *b, state_, actionHost);
    }
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
