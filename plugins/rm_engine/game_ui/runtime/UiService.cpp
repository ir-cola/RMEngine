#include "rm_engine/game_ui/runtime/UiService.h"

#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include "base/Utf.h"
#include "platform/Shell.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace rm::game_ui {

namespace {

using game_production::ActionOrStandard;
using game_production::EvaluateAction;
using game_production::InputAction;
using game_production::InputControl;
using game_production::InputFrame;

// このフレームのアクションの押し具合 (0..1: その軸。逆向きに倒したスティックは 0)。
float Push(std::span<const InputAction> actions, const char* name, const InputFrame& f) {
    const InputAction* a = ActionOrStandard(actions, name);
    return a ? std::max(0.f, EvaluateAction(*a, f).axis) : 0.f;
}

// 1 フレームで押されている向き: 強いほうの向きだけ (斜めに倒したスティックも 1 方向)、画面 Y は下向き。
void Held(std::span<const InputAction> actions, const InputFrame& f, int& dx, int& dy) {
    using namespace game_production;
    const float h = Push(actions, kUiRight, f) - Push(actions, kUiLeft, f);
    const float v = Push(actions, kUiDown, f) - Push(actions, kUiUp, f);
    dx = dy = 0;
    if (std::abs(h) >= 0.5f && std::abs(h) > std::abs(v)) dx = h > 0 ? 1 : -1;
    if (std::abs(v) >= 0.5f && std::abs(v) >= std::abs(h)) dy = v > 0 ? 1 : -1;
}

template <class T>
T* Mutable(game_production::Scene& scene, int entity) {
    game_production::SceneEntity* e = entity ? scene.Find(entity) : nullptr;
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<T>().name) : nullptr;
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

UiButton* MutableButton(game_production::Scene& scene, int entity) { return Mutable<UiButton>(scene, entity); }

// 点 (画面 DIP) をノード自身の単位に。
std::optional<Vec2> Local(const UiNode& n, Vec2 p) {
    const auto inv = Invert(n.self);
    if (!inv) return std::nullopt;
    return Apply(*inv, p);
}

// ノードの箱を画面で囲む箱。
UiBox ScreenBox(const UiNode& n) {
    const UiBox& b = n.box;
    const Vec2 c[4] = {Apply(n.self, {b.x, b.y}), Apply(n.self, {b.Right(), b.y}), Apply(n.self, {b.Right(), b.Bottom()}), Apply(n.self, {b.x, b.Bottom()})};
    float l = c[0].x, t = c[0].y, r = c[0].x, bt = c[0].y;
    for (const Vec2& v : c) l = std::min(l, v.x), t = std::min(t, v.y), r = std::max(r, v.x), bt = std::max(bt, v.y);
    return {l, t, r - l, bt - t};
}

// --- 入力の部品の 1 フレーム (StepUi の中) ---------------------------------------------------------

// スライダーを `v` に (刻みに合わせる)。変わったら *changes に。
void SetSlider(UiSlider& s, int entity, float v, std::vector<UiChange>& changes) {
    const float before = SliderSnap(s, s.value);
    s.value = SliderSnap(s, v);
    if (s.value != before) changes.push_back({entity, false, s.value, {}});
}

// トグルを押した: 入れ替える (グループなら自分をオンに、同じウィンドウの同じグループのほかをオフに)。
void PressToggle(game_production::Scene& scene, const UiTree& tree, const UiNode& n, std::vector<UiChange>& changes) {
    UiToggle* t = Mutable<UiToggle>(scene, n.entity);
    if (!t) return;
    const bool was = t->on;
    if (t->group.empty()) {
        t->on = !t->on;
    } else {
        t->on = true;
        for (const UiNode& o : tree.nodes) {
            if (o.entity == n.entity || o.canvas != n.canvas || !o.toggle || o.toggle->group != t->group) continue;
            UiToggle* other = Mutable<UiToggle>(scene, o.entity);
            if (!other || !other->on) continue;
            other->on = false;
            changes.push_back({o.entity, false, 0.0, {}});
        }
    }
    if (t->on != was) changes.push_back({n.entity, false, t->on ? 1.0 : 0.0, {}});
}

// ホイールとドラッグ (スクロールバーのつまみ・溝、中身の空いた所)。
void StepScroll(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown, float wheel, int hoveredButton,
                UiFocus& state) {
    if (!down) state.scrolling = 0;
    if (pointer && wheel != 0) {
        const int si = ScrollAt(tree, *pointer);
        const UiNode* n = si >= 0 ? &tree.nodes[static_cast<size_t>(si)] : nullptr;
        if (UiScroll* sc = n ? Mutable<UiScroll>(scene, n->entity) : nullptr) {
            const UiScrollRange r = ScrollRange(*sc, n->box, n->content);
            // (縦に動けば縦、動かなければ横へ。+ は奥へ回す = 上を見る)
            if (r.high.y - r.low.y > 0.5f) {
                sc->offset.y -= wheel * sc->wheelStep;
            } else {
                sc->offset.x -= wheel * sc->wheelStep;
            }
            sc->offset = ClampScroll(sc->offset, r);
        }
    }
    if (pointer && wentDown && !state.scrolling) {
        const int si = ScrollAt(tree, *pointer);
        const UiNode* n = si >= 0 ? &tree.nodes[static_cast<size_t>(si)] : nullptr;
        UiScroll* sc = n ? Mutable<UiScroll>(scene, n->entity) : nullptr;
        const auto local = n ? Local(*n, *pointer) : std::nullopt;
        if (sc && local) {
            const UiScrollRange r = ScrollRange(*sc, n->box, n->content);
            for (const bool vertical : {true, false}) {
                const UiScrollBar bar = ScrollBar(*sc, n->box, n->content, sc->offset, vertical);
                if (!bar.shown || !bar.track.Contains(*local)) continue;
                if (!bar.thumb.Contains(*local)) {
                    // 溝: 枠 1 つ分だけその向きへ。
                    const float page = vertical ? n->box.h : n->box.w;
                    const bool before = vertical ? local->y < bar.thumb.y : local->x < bar.thumb.x;
                    (vertical ? sc->offset.y : sc->offset.x) += before ? -page : page;
                    sc->offset = ClampScroll(sc->offset, r);
                }
                state.scrolling = n->entity;
                state.thumb = true;
                state.thumbVertical = vertical;
                state.dragFrom = *local;
                state.offsetFrom = sc->offset;
                return;
            }
            if (sc->drag && !hoveredButton) {
                state.scrolling = n->entity;
                state.thumb = false;
                state.dragFrom = *local;
                state.offsetFrom = sc->offset;
            }
        }
        return;
    }
    if (pointer && down && state.scrolling) {
        const UiNode* n = tree.Find(state.scrolling);
        UiScroll* sc = n ? Mutable<UiScroll>(scene, n->entity) : nullptr;
        const auto local = n ? Local(*n, *pointer) : std::nullopt;
        if (!sc || !local) {
            state.scrolling = 0;
            return;
        }
        const UiScrollRange r = ScrollRange(*sc, n->box, n->content);
        const Vec2 moved{local->x - state.dragFrom.x, local->y - state.dragFrom.y};
        Vec2 to = state.offsetFrom;
        if (state.thumb) {
            const UiScrollBar bar = ScrollBar(*sc, n->box, n->content, sc->offset, state.thumbVertical);
            if (state.thumbVertical) {
                to.y += ThumbDrag(bar, moved.y, r.low.y, r.high.y, true);
            } else {
                to.x += ThumbDrag(bar, moved.x, r.low.x, r.high.x, false);
            }
        } else {
            // 中身をつかんでいる: マウスと一緒に動く (動かない向きは範囲が 0)。
            to = {to.x - moved.x, to.y - moved.y};
        }
        sc->offset = ClampScroll(to, r);
    }
}

bool PressableIn(const UiTree& tree, int canvas, int entity) {
    const UiNode* n = entity ? tree.Find(entity) : nullptr;
    return n && n->canvas == canvas && n->button && n->button->interactable;
}

bool Contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// 表示されているウィンドウ: 前のフレームから開いたものは、隠したものと一緒に覚える (戻る の戻り道)。
void FollowWindows(const UiTree& tree, UiFocus& state) {
    std::vector<int> now;
    for (const UiNode& n : tree.nodes) {
        if (n.parent < 0 && !n.closing) now.push_back(n.entity);  // (去っていくものはもう閉じている)
    }
    if (!state.started) {
        state.started = true;  // (最初のウィンドウ: 戻る先はない)
    } else {
        std::vector<int> gone;
        for (const int w : state.shown) {
            if (!Contains(now, w)) gone.push_back(w);
        }
        for (const int w : now) {
            if (!Contains(state.shown, w)) state.visits.push_back({w, gone});
        }
        std::erase_if(state.visits, [&](const UiVisit& v) { return !Contains(now, v.window); });  // ほかの方法で閉じられた
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

bool StartTyping(game_production::Scene& scene, UiFocus& state, int entity) {
    UiInputField* f = Mutable<UiInputField>(scene, entity);
    const UiButton* b = Mutable<UiButton>(scene, entity);
    if (!f || (b && !b->interactable)) return false;
    if (state.typing == entity) return true;
    if (state.typing) StopTyping(scene, state, false);
    state.typing = entity;
    state.typedBefore = f->text;
    f->typing = true;
    f->caret = f->anchor = static_cast<int>(Utf8ToWide(f->text).size());
    f->composing.clear();
    f->composingCaret = 0;
    return true;
}

void StopTyping(game_production::Scene& scene, UiFocus& state, bool restore, std::vector<UiChange>* changes) {
    const int id = std::exchange(state.typing, 0);
    UiInputField* f = Mutable<UiInputField>(scene, id);
    if (!f) return;
    f->typing = false;
    f->composing.clear();
    f->composingCaret = 0;
    if (restore && f->text != state.typedBefore) {
        f->text = state.typedBefore;
        if (changes) changes->push_back({id, false, 0.0, f->text});
    }
    f->caret = f->anchor = std::min(f->caret, static_cast<int>(Utf8ToWide(f->text).size()));
}

UiStep StepUi(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown, bool wentUp,
              std::span<const InputAction> actions, const InputFrame* now, const InputFrame* before, float dt, UiFocus& state,
              const ui::TypedText* typed, const UiClipboard* clip) {
    UiStep step;
    FollowWindows(tree, state);
    // 打っている入力欄: このフレームの打鍵 (Enter で決まり、Esc で前に戻る)。押せなくなった・見えなく
    // なった欄はやめる。
    const int typingAtStart = state.typing;
    if (state.typing) {
        const UiNode* n = tree.Find(state.typing);
        UiInputField* f = Mutable<UiInputField>(scene, state.typing);
        if (!n || !f || n->closing || (n->button && !n->button->interactable)) {
            StopTyping(scene, state, false);
        } else if (typed) {
            std::wstring text = Utf8ToWide(f->text);
            const UiTypeResult r = TypeInto(text, f->caret, f->anchor, *f, *typed, clip ? *clip : UiClipboard{});
            if (r.changed) {
                f->text = WideToUtf8(text);
                step.changes.push_back({state.typing, false, 0.0, f->text});
            }
            f->composing = WideToUtf8(typed->imeComposition);
            f->composingCaret = typed->imeCursor;
            if (r.submitted) {
                step.changes.push_back({state.typing, true, 0.0, f->text});
                StopTyping(scene, state, false);
            } else if (r.cancelled) {
                StopTyping(scene, state, true, &step.changes);
            }
        }
    }
    // マウス。
    const PointerResult pr = StepPointer(tree, pointer, down, wentDown, wentUp, state.pointer);
    const bool moved = pointer && state.lastPointer && std::abs(pointer->x - state.lastPointer->x) + std::abs(pointer->y - state.lastPointer->y) > 0.5f;
    if (moved || (pointer && (wentDown || (now && now->wheel != 0)))) state.keyboard = false;  // マウスを使っている: 選択は表示しない
    state.lastPointer = pointer;
    // ほかの所を押した: 打つのをやめる (文字はそのまま)。
    if (state.typing && pointer && wentDown && pr.hovered != state.typing) StopTyping(scene, state, false);
    StepScroll(scene, tree, pointer, down, wentDown, now ? now->wheel : 0.f, pr.hovered, state);
    if (pr.clicked) {
        step.clicked.push_back(pr.clicked);
        state.focus = pr.clicked;  // (キーはそこから続ける)
    }
    // スライダー: 押している間 (外へ出ても)、マウスの所の値。速いクリックも。
    if (pointer) {
        const int held = state.pointer.pressed ? state.pointer.pressed : pr.clicked;
        const UiNode* n = held ? tree.Find(held) : nullptr;
        UiSlider* s = n && n->slider ? Mutable<UiSlider>(scene, held) : nullptr;
        if (s) {
            if (const auto local = Local(*n, *pointer)) SetSlider(*s, held, SliderAt(*s, n->box, *local), step.changes);
        }
    }
    // キーとパッドは、それを受ける一番手前のウィンドウで: そこには必ず選ばれたボタンがある。
    const int window = NavigationCanvas(tree);
    const UiNode* wn = window ? tree.Find(window) : nullptr;
    if (window != state.window) {
        state.window = window;
        // スクリプトがこのフレームに選んだボタン (rm::UI::Focus - ウィンドウの Show の後、Start で) は
        // 選ばれたまま。そうでなければウィンドウの 最初に選ぶボタン。
        if (!(state.scriptChose && PressableIn(tree, window, state.focus))) {
            state.focus = wn ? FirstFocus(tree, window, static_cast<int>(wn->canvasPart->firstButton.id)) : 0;
        }
    } else if (wn && !PressableIn(tree, window, state.focus)) {
        state.focus = FirstFocus(tree, window, static_cast<int>(wn->canvasPart->firstButton.id));
    }
    state.scriptChose = false;
    if (wn && now && before) {
        UiKeys keys = ReadUiKeys(actions, *now, *before);
        // 押しっぱなし: kRepeatDelay の後に 1 歩、その後は kRepeatEvery ごと。
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
        if (typingAtStart) {
            // 文字を打っている (打ち始めたフレームも): 矢印・Enter・Space は欄のもの。戻る (パッドの B) は
            // 打つのをやめるだけ (ウィンドウは閉じない。Esc は欄が受けた)。
            if (keys.cancel && state.typing) StopTyping(scene, state, true, &step.changes);
        } else {
            if (keys.dx || keys.dy) {
                // 選んでいるスライダーの向きのキー: 値を動かす (ほかの向きは選択が移る)。
                const UiNode* fn = state.keyboard && state.focus ? tree.Find(state.focus) : nullptr;
                UiSlider* s = fn && fn->slider && fn->button && fn->button->interactable ? Mutable<UiSlider>(scene, state.focus) : nullptr;
                const int nudge = s ? SliderKey(*s, keys.dx, keys.dy) : 0;
                if (s && nudge) {
                    SetSlider(*s, state.focus, SliderNudge(*s, nudge), step.changes);
                } else if (state.keyboard && state.focus) {
                    // マウスの後の最初の 1 歩は選択を再び見せるだけ。その次から 1 歩ずつ動かす。
                    state.focus = NextButton(tree, window, state.focus, keys.dx, keys.dy);
                }
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
    } else if (typingAtStart && now && before && state.typing && ReadUiKeys(actions, *now, *before).cancel) {
        StopTyping(scene, state, true, &step.changes);  // (キーとパッドで選ばないウィンドウの欄)
    }
    // 押された入力の部品: トグルは入れ替わり、入力欄は打ち始める。
    for (const int id : step.clicked) {
        const UiNode* n = tree.Find(id);
        if (!n || !n->button || !n->button->interactable) continue;
        if (n->toggle) PressToggle(scene, tree, *n, step.changes);
        if (n->input && id != typingAtStart) StartTyping(scene, state, id);
    }
    // このフレームの各ボタンの見た目。
    for (const UiNode& n : tree.nodes) {
        if (!n.button) continue;
        UiButton* b = MutableButton(scene, n.entity);
        if (!b) continue;
        // 押されている間は押した状態。乗った はほかのボタンが押されていないときだけ (あるボタンから別の
        // ボタンへドラッグしたとき)。
        const UiButtonState s = !b->interactable                                    ? UiButtonState::Disabled
                                : pr.held == n.entity                               ? UiButtonState::Pressed
                                : pr.hovered == n.entity && !state.pointer.pressed ? UiButtonState::Hover
                                                                                    : UiButtonState::Normal;
        b->state = static_cast<int>(s);
        b->focused = state.keyboard && state.focus == n.entity && n.canvas == window;
        // その見た目を状態のものへなめらかに近づける (最初のフレーム: そのまま)。
        const UiButtonLook target = ButtonTarget(*b);
        b->now = b->ready ? EaseLook(b->now, target, EaseAmount(dt, b->transition)) : target;
        // 乗った (マウス、キーの選択) か押された: そうなったときに 1 度だけその音 - 押して離すのは 2 回では
        // なく 1 回の音。最初のフレームは鳴らさない (最初から表示されている選択)。
        const bool hot = b->interactable && (s == UiButtonState::Hover || s == UiButtonState::Pressed || b->focused);
        if (hot && !b->hot && b->ready) step.hovered.push_back(n.entity);
        b->hot = hot;
        b->ready = true;
    }
    // いま表示されていないボタン (そのウィンドウが隠れている): 通常に戻す。再び表示されたとき、
    // 見た目は前の状態からでなくそこから始まり、音はマウスが乗ったときに鳴る (失われない)。
    std::unordered_set<int> shown;
    for (const UiNode& n : tree.nodes) {
        if (n.button) shown.insert(n.entity);
    }
    // (全エンティティ: オフのものも - それが隠れているもの)
    for (const auto& t : scene.TreeOrder()) {
        if (shown.contains(t.entity->data.id)) continue;
        UiButton* b = MutableButton(scene, t.entity->data.id);
        if (!b || (!b->hot && b->state == static_cast<int>(UiButtonState::Normal) && !b->focused)) continue;
        b->state = static_cast<int>(UiButtonState::Normal);
        b->focused = false;
        b->hot = false;
        if (b->ready) b->now = ButtonTarget(*b);
    }
    // トグルの見た目 (スイッチのつまみ) をオン / オフへなめらかに。スクロール: 選んだボタンを見せ、範囲に
    // 収め、その範囲を書く (スクリプトの 0..1)。
    for (const UiNode& n : tree.nodes) {
        if (n.toggle) {
            if (UiToggle* t = Mutable<UiToggle>(scene, n.entity)) {
                const float goal = t->on ? 1.f : 0.f;
                const float k = EaseAmount(dt, n.button ? n.button->transition : 0.08f);
                t->shown = t->ready ? t->shown + (goal - t->shown) * k : goal;
                if (std::abs(goal - t->shown) < 1e-3f) t->shown = goal;
                t->ready = true;
            }
        }
    }
    const UiNode* focusNode = state.keyboard && state.focus && state.focus != state.shownFocus ? tree.Find(state.focus) : nullptr;
    if (focusNode) {
        // 内側のスクロールから順に (どれも同じフレームの並びで)。
        size_t guard = 0;
        for (int c = focusNode->clip; c >= 0 && guard++ <= tree.nodes.size(); c = tree.nodes[static_cast<size_t>(c)].clip) {
            const UiNode& sn = tree.nodes[static_cast<size_t>(c)];
            UiScroll* sc = Mutable<UiScroll>(scene, sn.entity);
            if (!sc || !sc->followFocus) continue;
            sc->offset = ScrollToShow(sc->offset, sn.box, BoxInScroll(tree, c, *focusNode), ScrollRange(*sc, sn.box, sn.content));
        }
    }
    if (state.keyboard) state.shownFocus = state.focus;
    for (const UiNode& n : tree.nodes) {
        if (!n.scroll) continue;
        if (UiScroll* sc = Mutable<UiScroll>(scene, n.entity)) {
            const UiScrollRange r = ScrollRange(*sc, n.box, n.content);
            sc->offset = ClampScroll(sc->offset, r);
            sc->low = r.low;
            sc->high = r.high;
        }
    }
    // 打っている欄 (ホストは IME をつなぎ、候補の窓をその下に出す)。
    step.typing = state.typing;
    if (const UiNode* n = state.typing ? tree.Find(state.typing) : nullptr) step.typingBox = ScreenBox(*n);
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
    // このウィンドウをオフにし、隠したものをオンに戻す (そして覚える: 次のフレームに新しい訪問は作らない)。
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
                // 下限 / 上限をそれぞれ別に (両方使われていて向きが逆のときだけ入れ替える)。
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

// トゥイーン 1 つの今の値 (と終わったかどうか)。
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
        // 出る・消える動き: オンになったとき (または最初に) 始まり、終わりで終わる - ウィンドウも手放す。
        if (UiMotion* m = Part<UiMotion>(*e)) {
            const bool shown = t.activeInHierarchy;
            auto phase = static_cast<UiMotionPhase>(m->phase);
            if (shown && !m->seen) {
                phase = first && !m->atStart ? UiMotionPhase::Still : UiMotionPhase::Showing;
                m->time = 0;
            } else if (!shown && m->seen) {
                // 自分自身がオフになった (親ごとでなく): 消えるとき が言うとおりに去る。
                phase = !e->data.active && m->hide.enabled ? UiMotionPhase::Hiding : UiMotionPhase::Still;
                m->time = 0;
            } else if (!shown && phase == UiMotionPhase::Hiding && e->data.active) {
                phase = UiMotionPhase::Still;  // (その間に親がオフになった: 一緒に消える)
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
        // 道に沿って動く: 表示されている間歩く。
        if (UiPath* p = Part<UiPath>(*e); p && place && t.activeInHierarchy) {
            const UiPathPose pose = StepPath(*p, PathLine(*p), dt);
            UiLive along;
            along.offset = pose.offset;
            if (p->orient) along.turn = pose.angle;
            live = Combine(live, along);
        }
        // スクリプトのトゥイーン: 位置 と 回転 はそのもの自身に、大きさと透明度はここに持つ。
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
    // セーフエリアはゲーム設定の 画面 の DIP で決める: 描く画面に合わせて拡大縮小する。
    const float kx = settings.display.width > 0 ? size.x / static_cast<float>(settings.display.width) : 1.f;
    const float ky = settings.display.height > 0 ? size.y / static_cast<float>(settings.display.height) : 1.f;
    s.safeTopLeft = {settings.display.safeAreaTopLeft.x * kx, settings.display.safeAreaTopLeft.y * ky};
    s.safeBottomRight = {settings.display.safeAreaBottomRight.x * kx, settings.display.safeAreaBottomRight.y * ky};
    return s;
}

void UiService::Frame(const game_production::PlayFrame& f) {
    if (!host_.settings) return;
    StepMotion(f.scene, f.dt, motion_);  // (最初に: 去っていくものはまだ配置される - クリックはされない)
    const UiTree tree = BuildUi(f.scene, ScreenOf(*host_.settings, f.screen));
    // (マウスはポインター - アクションの割り当てではない)
    const bool down = f.input && game_production::ControlValue(*f.input, InputControl::MouseLeft) >= 0.5f;
    const bool before = f.inputBefore && game_production::ControlValue(*f.inputBefore, InputControl::MouseLeft) >= 0.5f;
    const std::span<const InputAction> actions = f.actions ? std::span<const InputAction>(*f.actions) : std::span<const InputAction>();
    // 押した / 離した: ウィンドウが知らされたこと、または前のフレームから変わったこと。速いクリックは
    // 1 フレームに両方ある (ボタンはどちらの見た目でも上がっている): StepPointer はそれをクリックと扱う。
    const bool wentDown = f.pointerWentDown || (down && !before);
    const bool wentUp = f.pointerWentUp || (!down && before);
    // 入力欄のコピーと貼り付け: Windows のクリップボード (持ち主はこのスレッドの手前のウィンドウ: 打っている
    // ゲームの / エディターのウィンドウ。null では書けない)。
    UiClipboard clip;
    clip.get = [] { return platform::GetClipboardText(GetActiveWindow()); };
    clip.set = [](const std::wstring& s) { platform::SetClipboardText(GetActiveWindow(), s); };
    const UiStep step = StepUi(f.scene, tree, f.pointer, down, wentDown, wentUp, actions, f.input, f.inputBefore, f.dt, state_, f.typed, &clip);
    if (f.textInput) {
        f.textInput->want = step.typing != 0;
        f.textInput->x = step.typingBox.x, f.textInput->y = step.typingBox.y;
        f.textInput->w = step.typingBox.w, f.textInput->h = step.typingBox.h;
    }
    // 値が変わった入力の部品: そのエンティティのスクリプトの OnEvent へ (このフレームの終わりに届く)。
    for (const UiChange& c : step.changes) {
        if (c.submitted && host_.log) {
            if (const game_production::SceneEntity* e = f.scene.Find(c.entity)) host_.log(0, "ゲーム UI: 「" + e->data.name + "」で Enter を押しました");
        }
        if (!f.world || f.world->events.size() >= 10000) continue;
        game_production::QueuedEvent q;
        q.name = c.submitted ? kSubmittedEvent : kChangedEvent;
        q.number = c.number;
        q.text = c.text;
        q.entity = c.entity;
        q.scope = 1;
        q.target = c.entity;
        f.world->events.push_back(std::move(q));
    }
    // 押された各ボタンについて: その音、次にそれがすること、次にスクリプトの OnClick (仕様 §6-6:
    // OnClick はアクションがしたことを読む。シーンと終了は依頼され、フレームの後に行う)。
    game_production::SoundPlayer* sounds = f.services ? f.services->Sounds() : nullptr;
    auto play = [&](int id, bool click) {
        const UiButton* b = MutableButton(f.scene, id);
        if (!b || !sounds) return;
        const AssetRef& sound = click ? b->clickSound : b->hoverSound;
        if (!sound.Empty()) sounds->PlaySound(sound, click ? b->clickVolume : b->hoverVolume);
    };
    for (const int id : step.hovered) play(id, false);
    // 動画 は自分で音を鳴らす: 今の音楽と同じ大きさで (全体 × 音楽)。
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
            // ID でのシーン (2 つのフォルダにある同じ名前の 2 つは別のもの)。なければファイル名で、
            // Game::LoadScene("…") が受け取るとおりに。
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
