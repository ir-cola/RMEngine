#include "rm_engine/game_production/runtime/Scripts.h"

#include "rm_engine/game_production/runtime/Systems.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "platform/Shell.h"
#include "platform/Win.h"
#include "rm_engine/game_production/runtime/SceneFile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <system_error>
#include <array>
#include <mutex>
#include <utility>

namespace rm::game_production {

namespace {

ScriptWorld& W(void* ctx) { return *static_cast<ScriptWorld*>(ctx); }

SceneEntity* EntityOf(void* ctx, RmEntityId id) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || id == 0 || id > static_cast<RmEntityId>(std::numeric_limits<int>::max())) return nullptr;
    return w.scene->Find(static_cast<int>(id));
}

// --- 核の表 (RmCoreApi): どのエンジンも渡す ----------------------------------------------------
void ApiLog(void* ctx, int level, const char* text) {
    if (W(ctx).log) W(ctx).log(level, text ? text : "");
}
float ApiDeltaTime(void* ctx) { return W(ctx).dt; }
double ApiTime(void* ctx) { return W(ctx).time; }
RmEntityId ApiFindByName(void* ctx, const char* name) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || !name) return 0;
    for (const TreeEntry& t : w.scene->TreeOrder()) {
        if (t.entity->data.name == name) return static_cast<RmEntityId>(t.entity->data.id);
    }
    return 0;
}
int ApiExists(void* ctx, RmEntityId e) { return EntityOf(ctx, e) ? 1 : 0; }
int ApiIsActive(void* ctx, RmEntityId e) {
    ScriptWorld& w = W(ctx);
    size_t steps = 0;
    for (const SceneEntity* x = EntityOf(ctx, e); x && steps++ <= w.scene->Entities().size(); x = w.scene->Find(x->data.parent)) {
        if (!x->data.active) return 0;
        if (x->data.parent == 0) return 1;
    }
    return 0;
}
void ApiSetActive(void* ctx, RmEntityId e, int active) {
    if (SceneEntity* x = EntityOf(ctx, e)) x->data.active = active != 0;
}
uint32_t ApiGetName(void* ctx, RmEntityId e, char* buf, uint32_t cap) {
    const SceneEntity* x = EntityOf(ctx, e);
    const std::string& name = x ? x->data.name : std::string();
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(name.size(), cap - 1);
        std::memcpy(buf, name.data(), n);
        buf[n] = '\0';
    }
    return static_cast<uint32_t>(name.size());
}
RmEntityId ApiParent(void* ctx, RmEntityId e) {
    const SceneEntity* x = EntityOf(ctx, e);
    return x ? static_cast<RmEntityId>(x->data.parent) : 0;
}
bool KeyIn(const InputFrame& f, int vk) { return vk > 0 && vk < 256 && f.keys[static_cast<size_t>(vk)] != 0; }
int ApiKeyDown(void* ctx, int vk) { return KeyIn(W(ctx).inputNow, vk) ? 1 : 0; }
int ApiKeyPressed(void* ctx, int vk) { return KeyIn(W(ctx).inputNow, vk) && !KeyIn(W(ctx).inputBefore, vk) ? 1 : 0; }
int ApiKeyReleased(void* ctx, int vk) { return !KeyIn(W(ctx).inputNow, vk) && KeyIn(W(ctx).inputBefore, vk) ? 1 : 0; }
RmEntityId ApiCamera(void* ctx) { return W(ctx).camera; }
void ApiSetCamera(void* ctx, RmEntityId e) { W(ctx).camera = e; }
void* ApiGetScript(void* ctx, RmEntityId e, const char* typeName, uint32_t nth) {
    SceneEntity* x = EntityOf(ctx, e);
    Component* c = x && typeName ? x->FindComponent(typeName, nth) : nullptr;
    return c && c->Known() ? c->value.Get() : nullptr;
}
int ApiLoadSceneAt(void* ctx, int32_t index) {
    ScriptWorld& w = W(ctx);
    if (index < 0 || static_cast<size_t>(index) >= w.sceneNames.size()) {
        if (w.log) w.log(1, std::format("Game::LoadScene({}): その番号のシーンはありません (ゲームのシーンは {} 個)", index, w.sceneNames.size()));
        return 0;
    }
    w.sceneAsked = index;
    return 1;
}
int ApiLoadScene(void* ctx, const char* name) {
    ScriptWorld& w = W(ctx);
    const std::string_view n = name ? name : "";
    if (const auto at = FindSceneByName(w.sceneNames, n)) {
        w.sceneAsked = static_cast<int>(*at);
        return 1;
    }
    if (w.log) w.log(1, std::format("Game::LoadScene(\"{}\"): そのシーンはゲームのシーンの一覧にありません (シーン パネルで足します)", n));
    return 0;
}
uint32_t ApiSceneName(void* ctx, char* buf, uint32_t cap) {
    const ScriptWorld& w = W(ctx);
    static const std::string kNone;
    const std::string& name = w.sceneIndex >= 0 && static_cast<size_t>(w.sceneIndex) < w.sceneNames.size() ? w.sceneNames[static_cast<size_t>(w.sceneIndex)] : kNone;
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(name.size(), cap - 1);
        std::memcpy(buf, name.data(), n);
        buf[n] = '\0';
    }
    return static_cast<uint32_t>(name.size());
}
int32_t ApiSceneIndex(void* ctx) { return W(ctx).sceneIndex; }
int32_t ApiSceneCount(void* ctx) { return static_cast<int32_t>(W(ctx).sceneNames.size()); }

// --- ゲーム制作の持つ部品 (RmApi::part に名前で渡す。プラグインの部品と同じ) ----------------------
// アクション名での入力: 今のフレームと前のフレーム。ゲーム設定の 入力 で調べる。
int ApiInputDown(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a && EvaluateAction(*a, W(ctx).inputNow).down ? 1 : 0;
}
int ApiInputPressed(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a && EvaluateAction(*a, W(ctx).inputNow).down && !EvaluateAction(*a, W(ctx).inputBefore).down ? 1 : 0;
}
int ApiInputReleased(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a && !EvaluateAction(*a, W(ctx).inputNow).down && EvaluateAction(*a, W(ctx).inputBefore).down ? 1 : 0;
}
float ApiInputAxis(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a ? EvaluateAction(*a, W(ctx).inputNow).axis : 0.0f;
}
constexpr RmInputApi kInput{&ApiInputDown, &ApiInputPressed, &ApiInputReleased, &ApiInputAxis};

// グローバル変数を名前で: 数値はスクリプトの型と変数の型の間で変換する。文字の変数は文字としてだけ
// 読み書きする。
int Components(uint32_t type) { return type == RmField_Vec3 ? 3 : type == RmField_Vec2 ? 2 : 1; }
bool NumberKind(uint32_t type) {
    return type == RmField_Bool || type == RmField_Int || type == RmField_Float || type == RmField_Vec2 || type == RmField_Vec3;
}
int ApiGlobalGet(void* ctx, const char* name, uint32_t type, void* out) {
    ScriptWorld& w = W(ctx);
    const GlobalValue* g = w.Global(name);
    if (!g || !out || !NumberKind(type)) return 0;
    if (g->type == GlobalType::String) {
        w.TellOnce(std::string("global-text:") + name, std::format("グローバル変数「{}」は文字です。GlobalVar<std::string> で読みます", name));
        return 0;
    }
    if (type == RmField_Bool) {
        *static_cast<int32_t*>(out) = g->v[0] != 0 ? 1 : 0;
    } else if (type == RmField_Int) {
        *static_cast<int32_t*>(out) = static_cast<int32_t>(g->v[0]);
    } else {
        for (int i = 0; i < Components(type); ++i) static_cast<float*>(out)[i] = static_cast<float>(g->v[i]);
    }
    return 1;
}
int ApiGlobalSet(void* ctx, const char* name, uint32_t type, const void* value) {
    ScriptWorld& w = W(ctx);
    GlobalValue* g = w.Global(name);
    if (!g || !value || !NumberKind(type)) return 0;
    if (g->type == GlobalType::String) {
        w.TellOnce(std::string("global-text:") + name, std::format("グローバル変数「{}」は文字です。GlobalVar<std::string> で書きます", name));
        return 0;
    }
    double in[3]{};
    const int n = Components(type);
    if (type == RmField_Bool || type == RmField_Int) {
        in[0] = static_cast<double>(*static_cast<const int32_t*>(value));
    } else {
        for (int i = 0; i < n; ++i) in[i] = static_cast<double>(static_cast<const float*>(value)[i]);
    }
    // 変数の型に合わせる: ふつうの数値は x を取る。ベクトルはスクリプトが渡したものを取る (残りはそのまま)。
    GlobalValue next = *g;
    if (g->type == GlobalType::Vec2 || g->type == GlobalType::Vec3) {
        for (int i = 0; i < n && i < (g->type == GlobalType::Vec3 ? 3 : 2); ++i) next.v[i] = in[i];
    } else if (auto fitted = GlobalFromJson(g->type, json::Value(in[0]))) {
        next = std::move(*fitted);
    }
    *g = next;
    return 1;
}
uint64_t ApiGlobalGetString(void* ctx, const char* name, char* buf, uint64_t cap) {
    const GlobalValue* g = W(ctx).Global(name);
    const std::string text = g ? FormatGlobalValue(*g) : std::string();
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(text.size(), static_cast<size_t>(cap - 1));
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
    }
    return text.size();
}
int ApiGlobalSetString(void* ctx, const char* name, const char* utf8, uint64_t len) {
    ScriptWorld& w = W(ctx);
    GlobalValue* g = w.Global(name);
    if (!g) return 0;
    const std::string_view text = utf8 ? std::string_view(utf8, static_cast<size_t>(len)) : std::string_view();
    auto v = ParseGlobalValue(g->type, text);
    if (!v) {
        w.TellOnce(std::string("global-parse:") + name, std::format("グローバル変数「{}」に「{}」は入りません (型が違います)", name, text));
        return 0;
    }
    *g = std::move(*v);
    return 1;
}
constexpr RmGlobalsApi kGlobals{&ApiGlobalGet, &ApiGlobalSet, &ApiGlobalGetString, &ApiGlobalSetString};

// 保存: すべてのグローバル変数を <saveFolder>/save_<slot>.json に書く。
int ApiSaveWrite(void* ctx, const char* slot) {
    ScriptWorld& w = W(ctx);
    std::string why;
    const std::string s = slot ? slot : "";
    if (WriteSave(w.globals, w.saveFolder, s, &why)) return 1;
    if (w.log) w.log(1, std::format("Save::Write(\"{}\"): 書けませんでした: {}", s, why));
    return 0;
}
int ApiSaveRead(void* ctx, const char* slot) {
    ScriptWorld& w = W(ctx);
    std::string why;
    std::vector<std::string> warnings;
    const std::string s = slot ? slot : "";
    const bool ok = ReadSave(w.globals, w.saveFolder, s, &warnings, &why);
    if (w.log) {
        for (const std::string& m : warnings) w.log(1, m);
        // (ゲームの初回起動では「まだ保存がない」のがふつう。スクリプトの false がそれを伝える。
        // 読めない保存がある場合だけ知らせる)
        if (!ok && SaveExists(w.saveFolder, s)) w.log(1, std::format("Save::Read(\"{}\"): 読めませんでした: {}", s, why));
    }
    return ok ? 1 : 0;
}
int ApiSaveExists(void* ctx, const char* slot) { return SaveExists(W(ctx).saveFolder, slot ? slot : "") ? 1 : 0; }
int ApiSaveRemove(void* ctx, const char* slot) {
    ScriptWorld& w = W(ctx);
    std::string why;
    const std::string s = slot ? slot : "";
    if (DeleteSave(w.saveFolder, s, &why)) return 1;
    if (w.log) w.log(1, std::format("Save::Delete(\"{}\"): 消せませんでした: {}", s, why));
    return 0;
}
constexpr RmSaveApi kSave{&ApiSaveWrite, &ApiSaveRead, &ApiSaveExists, &ApiSaveRemove};

// 入力 バージョン 2: プレイヤーごと、とプレイヤーの割り当て。
int ApiInput2Down(void* ctx, const char* name, int32_t player) {
    ScriptWorld& w = W(ctx);
    const InputAction* a = w.Action(name);
    return a && EvaluateAction(*a, w.inputNow, player, w.keyboardPlayer).down ? 1 : 0;
}
int ApiInput2Pressed(void* ctx, const char* name, int32_t player) {
    ScriptWorld& w = W(ctx);
    const InputAction* a = w.Action(name);
    return a && EvaluateAction(*a, w.inputNow, player, w.keyboardPlayer).down && !EvaluateAction(*a, w.inputBefore, player, w.keyboardPlayer).down ? 1 : 0;
}
int ApiInput2Released(void* ctx, const char* name, int32_t player) {
    ScriptWorld& w = W(ctx);
    const InputAction* a = w.Action(name);
    return a && !EvaluateAction(*a, w.inputNow, player, w.keyboardPlayer).down && EvaluateAction(*a, w.inputBefore, player, w.keyboardPlayer).down ? 1 : 0;
}
float ApiInput2Axis(void* ctx, const char* name, int32_t player) {
    ScriptWorld& w = W(ctx);
    const InputAction* a = w.Action(name);
    return a ? EvaluateAction(*a, w.inputNow, player, w.keyboardPlayer).axis : 0.0f;
}
int ApiPadConnected(void* ctx, int32_t player) {
    const ScriptWorld& w = W(ctx);
    return player >= 0 && player < kPlayers && w.inputNow.padConnected[static_cast<size_t>(player)] ? 1 : 0;
}
void ApiVibrate(void* ctx, int32_t player, float low, float high, float seconds) {
    ScriptWorld& w = W(ctx);
    if (w.vibrate && player >= 0 && player < kPlayers) w.vibrate(player, low, high, seconds);
}
// プレイヤー自身の一覧 (スクリプトが初めて変えたときに、設定のものから作る)。
std::vector<InputAction>& PlayerActions(ScriptWorld& w) {
    if (!w.playerActions) w.playerActions = w.Actions();
    return *w.playerActions;
}
InputAction* PlayerAction(ScriptWorld& w, const char* name) {
    if (!name || !w.Action(name)) return nullptr;  // (無いときは 1 回だけ知らせる)
    for (InputAction& a : PlayerActions(w)) {
        if (a.name == name) return &a;
    }
    return nullptr;
}
uint32_t ApiBindingCount(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a ? static_cast<uint32_t>(a->bindings.size()) : 0;
}
int ApiBinding(void* ctx, const char* name, uint32_t index, int32_t* control, float* scale) {
    const InputAction* a = W(ctx).Action(name);
    if (!a || index >= a->bindings.size()) return 0;
    if (control) *control = static_cast<int32_t>(a->bindings[index].control);
    if (scale) *scale = a->bindings[index].scale;
    return 1;
}
int ApiSetBinding(void* ctx, const char* name, uint32_t index, int32_t control, float scale) {
    InputAction* a = PlayerAction(W(ctx), name);
    if (!a || index > a->bindings.size() || !std::isfinite(scale)) return 0;  // (NaN がアクションと input.json に残ってしまう)
    const InputBinding b{static_cast<InputControl>(control), std::clamp(scale, -1.0f, 1.0f)};
    if (index == a->bindings.size()) {
        a->bindings.push_back(b);
    } else {
        a->bindings[index] = b;
    }
    return 1;
}
int ApiRemoveBinding(void* ctx, const char* name, uint32_t index) {
    InputAction* a = PlayerAction(W(ctx), name);
    if (!a || index >= a->bindings.size()) return 0;
    a->bindings.erase(a->bindings.begin() + index);
    return 1;
}
void ApiResetBindings(void* ctx) { W(ctx).playerActions.reset(); }
int ApiSaveBindings(void* ctx) {
    ScriptWorld& w = W(ctx);
    std::string why;
    if (w.SaveBindings(&why)) return 1;
    if (w.log) w.log(1, std::format("Input::SaveBindings: 書けませんでした: {}", why));
    return 0;
}
int ApiLoadBindings(void* ctx) {
    ScriptWorld& w = W(ctx);
    std::string why;
    if (w.LoadBindings(&why)) return 1;
    if (w.log && !why.empty()) w.log(1, std::format("Input::LoadBindings: 読めませんでした: {}", why));
    return 0;
}
int32_t ApiAnyPressed(void* ctx, int32_t player, float* direction) {
    const ScriptWorld& w = W(ctx);
    return static_cast<int32_t>(AnyPressed(w.inputNow, w.inputBefore, player, w.keyboardPlayer, direction));
}
uint32_t CopyText(const std::string& text, char* buf, uint32_t cap) {
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(text.size(), cap - 1);
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
    }
    return static_cast<uint32_t>(text.size());
}
uint32_t ApiControlName(void*, int32_t control, char* buf, uint32_t cap) { return CopyText(ControlName(control), buf, cap); }
constexpr RmInputApi2 kInput2{&ApiInput2Down,    &ApiInput2Pressed, &ApiInput2Released, &ApiInput2Axis,   &ApiPadConnected,
                              &ApiVibrate,       &ApiBindingCount,  &ApiBinding,        &ApiSetBinding,   &ApiRemoveBinding,
                              &ApiResetBindings, &ApiSaveBindings,  &ApiLoadBindings,   &ApiAnyPressed,   &ApiControlName};

// ゲーム画面上のマウス。
int ApiMousePosition(void* ctx, float out[2]) {
    const ScriptWorld& w = W(ctx);
    const Vec2 p = w.pointer.value_or(w.lastPointer);
    out[0] = p.x;
    out[1] = p.y;
    return w.pointer ? 1 : 0;
}
void ApiMouseDelta(void* ctx, float out[2]) {
    out[0] = W(ctx).pointerDelta.x;
    out[1] = W(ctx).pointerDelta.y;
}
float ApiMouseWheel(void* ctx) { return W(ctx).inputNow.wheel; }
void ApiScreenSize(void* ctx, float out[2]) {
    out[0] = W(ctx).screen.x;
    out[1] = W(ctx).screen.y;
}
constexpr RmMouseApi kMouse{&ApiMousePosition, &ApiMouseDelta, &ApiMouseWheel, &ApiScreenSize};

// 時間。
float ApiTimeScale(void* ctx) { return W(ctx).timeScale; }
void ApiSetTimeScale(void* ctx, float scale) { W(ctx).timeScale = std::isfinite(scale) ? std::clamp(scale, 0.0f, 100.0f) : 1.0f; }
float ApiUnscaledDeltaTime(void* ctx) { return W(ctx).unscaledDt; }
double ApiUnscaledTime(void* ctx) { return W(ctx).unscaledTime; }
uint64_t ApiFrameCount(void* ctx) { return W(ctx).frame; }
constexpr RmTimeApi kTime{&ApiTimeScale, &ApiSetTimeScale, &ApiUnscaledDeltaTime, &ApiUnscaledTime, &ApiFrameCount};

// コルーチン。
uint64_t ApiCoStart(void* ctx, void* frame, int (*resume)(void*, RmWait*), void (*destroy)(void*)) {
    return W(ctx).StartCoroutine(frame, resume, destroy);
}
void ApiCoStop(void* ctx, uint64_t id) { W(ctx).StopCoroutine(id); }
void ApiCoStopAll(void* ctx) {
    ScriptWorld& w = W(ctx);
    if (w.currentScript) w.StopCoroutinesOf(w.currentScript);
}
int ApiCoRunning(void* ctx, uint64_t id) { return W(ctx).CoroutineRunning(id) ? 1 : 0; }
constexpr RmCoroutinesApi kCoroutines{&ApiCoStart, &ApiCoStop, &ApiCoStopAll, &ApiCoRunning};

// イベント: 待たせておき、フレームの Tick の後に届ける (ScriptRunner::DeliverEvents)。
void ApiSendEvent(void* ctx, uint32_t scope, RmEntityId target, const char* name, double number, const char* text, uint64_t textLen,
                  RmEntityId entity) {
    ScriptWorld& w = W(ctx);
    if (!name || !*name || scope > 2) return;
    if (scope != 0 && !EntityOf(ctx, target)) return;  // (消えた相手に送った: 誰も受け取らない)
    if (w.events.size() >= 10000) {
        w.TellOnce("events:many", "rm::Events: 1 フレームに送るイベントが多すぎます (10000 個まで。OnEvent の中で送り合っていませんか)");
        return;
    }
    QueuedEvent q;
    q.name = name;
    q.sender = w.currentEntity;
    q.number = number;
    q.text = text ? std::string(text, static_cast<size_t>(textLen)) : std::string();
    q.entity = EntityOf(ctx, entity) ? static_cast<int>(entity) : 0;
    q.scope = scope;
    q.target = scope == 0 ? 0 : static_cast<int>(target);
    w.events.push_back(std::move(q));
}
constexpr RmEventsApi kEvents{&ApiSendEvent};

// エンティティ: タグ、レイヤー、子、親。
uint32_t ApiGetTag(void* ctx, RmEntityId e, char* buf, uint32_t cap) {
    const SceneEntity* x = EntityOf(ctx, e);
    return CopyText(x ? x->data.tag : std::string(), buf, cap);
}
void ApiSetTag(void* ctx, RmEntityId e, const char* tag) {
    if (SceneEntity* x = EntityOf(ctx, e)) x->data.tag = tag ? tag : "";
}
uint32_t ApiGetLayer(void* ctx, RmEntityId e, char* buf, uint32_t cap) {
    const SceneEntity* x = EntityOf(ctx, e);
    return CopyText(x ? x->data.layer : std::string(), buf, cap);
}
void ApiSetLayer(void* ctx, RmEntityId e, const char* layer) {
    if (SceneEntity* x = EntityOf(ctx, e)) x->data.layer = layer ? layer : "";
}
int32_t ApiLayerIndex(void* ctx, const char* name) {
    const ScriptWorld& w = W(ctx);
    const std::string_view n = name ? name : "";
    if (n.empty()) return 0;
    for (size_t i = 0; w.layers && i < w.layers->size(); ++i) {
        if ((*w.layers)[i] == n) return static_cast<int32_t>(i + 1);
    }
    return -1;
}
uint32_t ApiFindWithTag(void* ctx, const char* tag, RmEntityId* out, uint32_t cap) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || !tag) return 0;
    uint32_t n = 0;
    for (const TreeEntry& t : w.scene->TreeOrder()) {
        if (t.entity->data.tag != tag) continue;
        if (out && n < cap) out[n] = static_cast<RmEntityId>(t.entity->data.id);
        ++n;
    }
    return n;
}
uint32_t ApiChildren(void* ctx, RmEntityId e, RmEntityId* out, uint32_t cap) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || (e != 0 && !EntityOf(ctx, e))) return 0;
    const std::vector<int> kids = w.scene->Children(static_cast<int>(e));
    for (size_t i = 0; i < kids.size() && i < cap && out; ++i) out[i] = static_cast<RmEntityId>(kids[i]);
    return static_cast<uint32_t>(kids.size());
}
int ApiSetParent(void* ctx, RmEntityId e, RmEntityId parent, int keepWorld, int32_t index) {
    ScriptWorld& w = W(ctx);
    if (!EntityOf(ctx, e) || (parent != 0 && !EntityOf(ctx, parent))) return 0;
    const int id = static_cast<int>(e), to = static_cast<int>(parent);
    if (to != 0 && w.scene->IsUnder(to, id)) {
        w.TellOnce("parent-loop", "Entity::SetParent: 自分の下 (子や孫) を親にはできません");
        return 0;
    }
    if (keepWorld && w.registry) {
        for (const auto& c : w.registry->Contributions<PlaceInfo>(kPlacePoint)) {
            if (c.item->keepWorld) c.item->keepWorld(*w.scene, id, to);
        }
    }
    const int ids[] = {id};
    const size_t at = index < 0 ? Scene::kEnd : static_cast<size_t>(index);
    w.scene->Move(ids, to, at);  // (false: もうそこにある。何もしない)
    return 1;
}
void ApiSetName(void* ctx, RmEntityId e, const char* name) {
    if (SceneEntity* x = EntityOf(ctx, e)) x->data.name = name ? name : "";
}
constexpr RmEntityApi kEntity{&ApiGetTag,     &ApiSetTag,   &ApiGetLayer,  &ApiSetLayer, &ApiLayerIndex,
                              &ApiFindWithTag, &ApiChildren, &ApiSetParent, &ApiSetName};

// ゲーム自体: Game::Quit は終了を頼む (ホストはこのフレームの後でゲームを終える)。
void ApiQuit(void* ctx) { W(ctx).quitAsked = true; }
constexpr RmGameApi kGame{&ApiQuit};

const void* ApiPart(void* ctx, const char* name, uint32_t version) {
    if (name && version == RM_PART_INPUT_VERSION && std::strcmp(name, RM_PART_INPUT) == 0) return &kInput;
    if (name && version == RM_PART_GLOBALS_VERSION && std::strcmp(name, RM_PART_GLOBALS) == 0) return &kGlobals;
    if (name && version == RM_PART_SAVE_VERSION && std::strcmp(name, RM_PART_SAVE) == 0) return &kSave;
    if (name && version == RM_PART_GAME_VERSION && std::strcmp(name, RM_PART_GAME) == 0) return &kGame;
    if (name && version == RM_PART_INPUT_VERSION2 && std::strcmp(name, RM_PART_INPUT) == 0) return &kInput2;
    if (name && version == RM_PART_MOUSE_VERSION && std::strcmp(name, RM_PART_MOUSE) == 0) return &kMouse;
    if (name && version == RM_PART_TIME_VERSION && std::strcmp(name, RM_PART_TIME) == 0) return &kTime;
    if (name && version == RM_PART_COROUTINES_VERSION && std::strcmp(name, RM_PART_COROUTINES) == 0) return &kCoroutines;
    if (name && version == RM_PART_EVENTS_VERSION && std::strcmp(name, RM_PART_EVENTS) == 0) return &kEvents;
    if (name && version == RM_PART_ENTITY_VERSION && std::strcmp(name, RM_PART_ENTITY) == 0) return &kEntity;
    return FindScriptApiPart(W(ctx).parts, name, version);
}

// エンティティと部品 (ABI 4)。
RmEntityId ApiSpawn(void* ctx, const char* name, RmEntityId parent) {
    ScriptWorld& w = W(ctx);
    if (!w.scene) return 0;
    const int under = EntityOf(ctx, parent) ? static_cast<int>(parent) : 0;
    return static_cast<RmEntityId>(w.scene->Create(name && *name ? name : "エンティティ", under));
}
RmEntityId ApiSpawnPrefab(void* ctx, const char* assetId, RmEntityId parent) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || !assetId) return 0;
    const int under = EntityOf(ctx, parent) ? static_cast<int>(parent) : 0;
    reflect::Warnings warnings;
    const int made = SpawnPrefab(*w.scene, AssetRef{assetId}, under, w.prefab, warnings);
    for (const std::string& m : warnings) w.TellOnce("spawn:" + m, m);
    if (!made) w.TellOnce(std::string("prefab:") + assetId, std::format("rm::Spawn: Prefab ({}) がゲームにありません (消されたか、書き出しに入っていません)", assetId));
    return static_cast<RmEntityId>(made);
}
void ApiDestroy(void* ctx, RmEntityId e) {
    if (EntityOf(ctx, e)) W(ctx).destroyAsked.push_back(static_cast<int>(e));
}
const reflect::TypeDesc* ComponentType(const ScriptWorld& w, const char* typeName) {
    if (!w.scene || !typeName) return nullptr;
    for (const reflect::TypeDesc* t : w.scene->ComponentTypes()) {
        if (std::strcmp(t->name, typeName) == 0) return t;
    }
    return nullptr;
}
void* ApiAddComponent(void* ctx, RmEntityId e, const char* typeName) {
    ScriptWorld& w = W(ctx);
    SceneEntity* x = EntityOf(ctx, e);
    const reflect::TypeDesc* type = ComponentType(w, typeName);
    if (!x || !type) {
        if (x && typeName) w.TellOnce(std::string("add:") + typeName, std::format("Add: 部品「{}」はこのゲームにありません", typeName));
        return nullptr;
    }
    if (!w.scene->AllowsMultiple(type->name)) {
        if (Component* there = x->FindComponent(type->name)) return there->Known() ? there->value.Get() : nullptr;  // (多くても 1 つ: そこにあるもの)
    }
    if (!w.scene->AddComponent(x->data.id, *type)) return nullptr;
    Component* made = EntityOf(ctx, e)->FindComponent(type->name, EntityOf(ctx, e)->CountComponents(type->name) - 1);
    return made ? made->value.Get() : nullptr;
}
int ApiRemoveComponent(void* ctx, RmEntityId e, const char* typeName, uint32_t nth) {
    SceneEntity* x = EntityOf(ctx, e);
    Component* c = x && typeName ? x->FindComponent(typeName, nth) : nullptr;
    if (!c) return 0;
    W(ctx).removeAsked.push_back({x->data.id, c->value.Get()});
    return 1;
}
uint32_t ApiCountComponents(void* ctx, RmEntityId e, const char* typeName) {
    const SceneEntity* x = EntityOf(ctx, e);
    return x && typeName ? static_cast<uint32_t>(x->CountComponents(typeName)) : 0;
}

constexpr RmCoreApi kCore{&ApiLog,        &ApiDeltaTime,   &ApiTime,        &ApiFindByName,  &ApiExists,
                          &ApiIsActive,   &ApiSetActive,   &ApiGetName,     &ApiParent,      &ApiKeyDown,
                          &ApiKeyPressed, &ApiKeyReleased, &ApiCamera,      &ApiSetCamera,   &ApiGetScript,
                          &ApiLoadScene,  &ApiLoadSceneAt, &ApiSceneName,   &ApiSceneIndex,  &ApiSceneCount,
                          &ApiSpawn,      &ApiSpawnPrefab, &ApiDestroy,     &ApiAddComponent, &ApiRemoveComponent,
                          &ApiCountComponents};

// 全スクリプトの C++ の型。エンジンから見える範囲 (スクリプト自身の型は DLL の中にある)。
struct ScriptTag {};
const std::type_info& ScriptCppType() { return typeid(ScriptTag); }

bool KnownField(uint32_t type) { return type <= RmField_Asset; }

reflect::FieldType FieldTypeOf(uint32_t t) {
    switch (t) {
        case RmField_Bool: return reflect::FieldType::Bool;
        case RmField_Int: return reflect::FieldType::Int;
        case RmField_Float: return reflect::FieldType::Float;
        case RmField_String: return reflect::FieldType::String;
        case RmField_Vec2: return reflect::FieldType::Vec2;
        case RmField_Vec3: return reflect::FieldType::Vec3;
        case RmField_Color: return reflect::FieldType::Color;
        case RmField_Enum: return reflect::FieldType::Enum;
        case RmField_Struct: return reflect::FieldType::Struct;
        case RmField_List: return reflect::FieldType::Array;
        case RmField_Asset: return reflect::FieldType::AssetRef;
        default: return reflect::FieldType::EntityRef;
    }
}

const char* HookName(int hook) {
    switch (hook) {
        case RM_HOOK_BEGIN_PLAY: return "BeginPlay";
        case RM_HOOK_TICK: return "Tick";
        case RM_HOOK_RELOADED: return "OnReloaded";
        case RM_HOOK_HIT: return "OnHit";
        case RM_HOOK_TRIGGER_ENTER: return "OnTriggerEnter";
        case RM_HOOK_TRIGGER_EXIT: return "OnTriggerExit";
        case RM_HOOK_CLICK: return "OnClick";
        case RM_HOOK_EVENT: return "OnEvent";
        default: return "EndPlay";
    }
}

// スクリプトのアセット欄: char guid[RM_ASSET_ID_SIZE]。ここで読み書きする (ただのバイト列)。
size_t AssetGet(const void*, const void* field, char* buf, size_t cap) {
    const char* g = static_cast<const char*>(field);
    const size_t n = strnlen(g, RM_ASSET_ID_SIZE - 1);
    if (buf && cap > 0) {
        const size_t k = std::min(n, cap - 1);
        std::memcpy(buf, g, k);
        buf[k] = '\0';
    }
    return n;
}
void AssetSet(const void*, void* field, const char* utf8, size_t len) {
    char* g = static_cast<char*>(field);
    const size_t n = std::min<size_t>(utf8 ? len : 0, RM_ASSET_ID_SIZE - 1);
    if (n > 0) std::memcpy(g, utf8, n);
    std::memset(g + n, 0, RM_ASSET_ID_SIZE - n);
}

// `fields` (スクリプトや構造体の) の欄が使えない理由。`seen`: もう調べた構造体。
std::string FieldsProblem(const RmFieldDesc* fields, uint32_t count, std::unordered_set<const RmStructDesc*>& seen, int depth) {
    if (count > 0 && !fields) return "欄の一覧が空です";
    if (depth > 16) return "構造体の入れ子が深すぎます";
    auto element = [&](uint32_t type, const RmFieldDesc& f) -> std::string {
        if (type == RmField_Enum && (!f.enumDesc || !f.enumDesc->get || !f.enumDesc->set || (f.enumDesc->itemCount > 0 && !f.enumDesc->items))) {
            return std::format("欄 {} の列挙の説明がありません", f.name);
        }
        if (type == RmField_Struct) {
            const RmStructDesc* s = f.structDesc;
            if (!s || !s->name || s->size == 0 || !s->construct || !s->destruct || !s->copy) return std::format("欄 {} の構造体の説明がありません", f.name);
            if (s->align > 16) return std::format("構造体 {} は alignas が大きすぎます (16 まで)", s->name);
            if (seen.insert(s).second) {
                if (std::string why = FieldsProblem(s->fields, s->fieldCount, seen, depth + 1); !why.empty()) return why;
            }
        }
        return {};
    };
    for (uint32_t i = 0; i < count; ++i) {
        const RmFieldDesc& f = fields[i];
        if (!f.name || !f.access || !KnownField(f.type)) return "この版のエンジンが知らない欄があります";
        if (f.type == RmField_List) {
            if (!f.list || !f.list->size || !f.list->resize || !f.list->at || !KnownField(f.list->element) || f.list->element == RmField_List) {
                return std::format("欄 {} の一覧の説明がありません", f.name);
            }
            if (std::string why = element(f.list->element, f); !why.empty()) return why;
        } else if (std::string why = element(f.type, f); !why.empty()) {
            return why;
        }
    }
    return {};
}

const reflect::TypeDesc* StructOf(ScriptType& t, const RmStructDesc* s);
const reflect::EnumDesc* EnumOf(ScriptType& t, const RmEnumDesc* e);

// 欄の値 (またはリストの要素の値: `type` は要素の型)。
reflect::ValueType ValueOf(ScriptType& t, uint32_t type, const RmFieldDesc& in) {
    reflect::ValueType v{FieldTypeOf(type), nullptr, nullptr};
    if (type == RmField_Struct) v.structType = StructOf(t, in.structDesc);
    if (type == RmField_Enum) v.enumType = EnumOf(t, in.enumDesc);
    return v;
}

reflect::FieldDesc MakeField(ScriptType& t, const RmFieldDesc& in) {
    reflect::FieldDesc fd{};
    fd.name = in.name;
    fd.access = in.access;
    fd.transient = (in.flags & RmFieldFlag_Transient) != 0;
    fd.edit = (in.flags & RmFieldFlag_Edit) != 0;
    fd.readOnly = (in.flags & RmFieldFlag_ReadOnly) != 0;
    fd.label = in.label;
    fd.tooltip = in.tooltip;
    fd.hasMin = (in.flags & RmFieldFlag_HasMin) != 0;
    fd.hasMax = (in.flags & RmFieldFlag_HasMax) != 0;
    fd.hasStep = (in.flags & RmFieldFlag_HasStep) != 0;
    fd.min = in.min;
    fd.max = in.max;
    fd.step = in.step;
    fd.line = in.line;
    const uint32_t base = in.type == RmField_List ? in.list->element : in.type;
    if (in.type == RmField_List) {
        auto ops = std::make_unique<reflect::ArrayOps>();
        ops->element = ValueOf(t, base, in);
        ops->size = in.list->size;  // (DLL のもの: vector のメモリは DLL が持つ)
        ops->resize = in.list->resize;
        ops->at = in.list->at;
        fd.value = {reflect::FieldType::Array, nullptr, nullptr};
        fd.array = ops.get();
        t.lists.push_back(std::move(ops));
    } else {
        fd.value = ValueOf(t, base, in);
    }
    // 文字: std::string は DLL を通してだけ。アセットの id はただのバイト列で。
    if (base == RmField_String) fd.stringAccess = &t.strings;
    if (base == RmField_Asset) {
        fd.stringAccess = &t.assets;
        fd.assetKinds = in.assetKinds && *in.assetKinds ? in.assetKinds : nullptr;
    }
    return fd;
}

// --- ガード付きの構築 / 破棄 / コピー ----------------------------------------------------------
// TypeDesc の構築 / 破棄 / コピーは、核が呼ぶただの関数ポインターだ (シーンがスクリプトを足す、消す、
// Play のためにシーンをコピーする場面)。スクリプトの型では、固定のプールにある代役がそれぞれ DLL 自身の
// 関数を __try の中で呼ぶ (ScriptGuard.cpp)。そこで落ちても (null ポインターを読む初期値や、例外を
// 投げる破棄) 知らせたうえで、そのオブジェクトはゼロのままにする。おかしくなるのはそのオブジェクトだけで、
// エディターやゲームではない (2026-11-19)。プールが一杯のときは、DLL 自身の関数をガード無しで使う。
struct GuardSlot {
    void (*construct)(void*) = nullptr;
    void (*destruct)(void*) = nullptr;
    void (*copy)(void*, const void*) = nullptr;
    size_t size = 0;
    const char* name = "";
    bool used = false;
};
constexpr size_t kGuardSlots = 512;
GuardSlot g_guards[kGuardSlots];
std::mutex g_guardsMutex;

void TellGuardCrash(const char* what, const char* name, unsigned long code) {
    log::Error("スクリプト「{}」の{}で例外が起きました (0x{:08X})。その物は空のまま動きます", name, what, code);
}
template <size_t N>
void GuardedConstruct(void* p) {
    const GuardSlot& s = g_guards[N];
    if (const unsigned long code = CallScriptConstruct(s.construct, p); code != 0) {
        std::memset(p, 0, s.size);
        TellGuardCrash("初めの値 (作るとき)", s.name, code);
    }
}
template <size_t N>
void GuardedDestruct(void* p) {
    const GuardSlot& s = g_guards[N];
    if (const unsigned long code = CallScriptDestruct(s.destruct, p); code != 0) TellGuardCrash("後片付け (消すとき)", s.name, code);
}
template <size_t N>
void GuardedCopy(void* dst, const void* src) {
    const GuardSlot& s = g_guards[N];
    if (const unsigned long code = CallScriptCopy(s.copy, dst, src); code != 0) {
        std::memset(dst, 0, s.size);
        TellGuardCrash("写し (Play を始めるときなど)", s.name, code);
    }
}
template <size_t... I>
constexpr std::array<void (*)(void*), sizeof...(I)> Constructs(std::index_sequence<I...>) { return {&GuardedConstruct<I>...}; }
template <size_t... I>
constexpr std::array<void (*)(void*), sizeof...(I)> Destructs(std::index_sequence<I...>) { return {&GuardedDestruct<I>...}; }
template <size_t... I>
constexpr std::array<void (*)(void*, const void*), sizeof...(I)> Copies(std::index_sequence<I...>) { return {&GuardedCopy<I>...}; }
constexpr auto kConstructs = Constructs(std::make_index_sequence<kGuardSlots>{});
constexpr auto kDestructs = Destructs(std::make_index_sequence<kGuardSlots>{});
constexpr auto kCopies = Copies(std::make_index_sequence<kGuardSlots>{});

// `desc` の関数をガード付きの代役に替える (`t` に持ち、`t` と一緒に返す)。
void Guard(ScriptType& t, reflect::TypeDesc& desc) {
    std::lock_guard lock(g_guardsMutex);
    for (size_t i = 0; i < kGuardSlots; ++i) {
        if (g_guards[i].used) continue;
        g_guards[i] = {desc.construct, desc.destruct, desc.copy, desc.size, desc.name, true};
        desc.construct = kConstructs[i];
        desc.destruct = kDestructs[i];
        desc.copy = kCopies[i];
        t.guards.push_back(i);
        return;
    }
}

const reflect::TypeDesc* StructOf(ScriptType& t, const RmStructDesc* s) {
    if (auto it = t.structs.find(s); it != t.structs.end()) return &it->second->desc;
    auto n = std::make_unique<ScriptType::Nested>();
    ScriptType::Nested* at = n.get();
    t.structs.emplace(s, std::move(n));  // 先に入れる: 自分自身のリストを持つ構造体がこれを見つけられるように
    for (uint32_t f = 0; f < s->fieldCount; ++f) at->fields.push_back(MakeField(t, s->fields[f]));
    at->desc = reflect::TypeDesc{s->name,
                                 s->label && *s->label ? s->label : s->name,
                                 reflect::TypeKind::Struct,
                                 static_cast<size_t>(s->size),
                                 std::span<const reflect::FieldDesc>(at->fields),
                                 s->construct,
                                 s->destruct,
                                 s->copy,
                                 &ScriptCppType,
                                 ""};
    Guard(t, at->desc);
    return &at->desc;
}

const reflect::EnumDesc* EnumOf(ScriptType& t, const RmEnumDesc* e) {
    if (auto it = t.enums.find(e); it != t.enums.end()) return &it->second->desc;
    auto n = std::make_unique<ScriptType::EnumType>();
    for (uint32_t i = 0; i < e->itemCount; ++i) {
        const RmEnumItem& it = e->items[i];
        n->items.push_back({it.name, it.label ? it.label : it.name, it.value});
    }
    n->desc = reflect::EnumDesc{e->name, std::span<const reflect::EnumItem>(n->items), e->get, e->set, &ScriptCppType};
    const reflect::EnumDesc* out = &n->desc;
    t.enums.emplace(e, std::move(n));
    return out;
}

}  // namespace

// モジュールの init。ガード付き (ScriptGuard.cpp)。
unsigned long CallModuleInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out);

void ScriptWorld::ReadInput() {
    inputBefore = inputNow;
    inputNow = readInput ? readInput() : InputFrame{};
}

void ScriptWorld::SetPointer(std::optional<Vec2> at, Vec2 screenSize) {
    screen = screenSize;
    pointerDelta = at && pointer ? Vec2{at->x - pointer->x, at->y - pointer->y} : Vec2{0, 0};
    if (pointer) lastPointer = *pointer;
    pointer = at;
    if (at) lastPointer = *at;
}

const std::vector<InputAction>& ScriptWorld::Actions() const {
    static const std::vector<InputAction> kNone;
    if (playerActions) return *playerActions;
    return actions ? *actions : kNone;
}

std::filesystem::path ScriptWorld::BindingsFile() const { return saveFolder.empty() ? std::filesystem::path() : saveFolder / L"input.json"; }

bool ScriptWorld::LoadBindings(std::string* why) {
    const std::filesystem::path file = BindingsFile();
    std::error_code ec;
    if (file.empty() || !std::filesystem::is_regular_file(file, ec)) {
        playerActions.reset();  // (保存が無い: ゲーム設定のもの)
        return false;
    }
    const auto bytes = platform::ReadFileBytes(file, why);
    const auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
    if (!doc || doc->Get("kind").AsString() != kBindingsKind) {
        if (why && bytes) *why = "input.json が壊れています";
        return false;
    }
    playerActions = BindingsFromJson(actions ? std::span<const InputAction>(*actions) : std::span<const InputAction>(), *doc);
    return true;
}

bool ScriptWorld::SaveBindings(std::string* why) {
    const std::filesystem::path file = BindingsFile();
    if (file.empty()) {
        if (why) *why = "保存する場所がありません";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(saveFolder, ec);
    return platform::WriteFileAtomic(file, json::Write(BindingsToJson(Actions())), why);
}

namespace {
// コルーチンを次の待ちまで進める (その間に動いているのは、持ち主のスクリプト)。
void RunCoroutine(ScriptWorld& w, CoroutineSlot& slot) {
    const void* script = std::exchange(w.currentScript, slot.owner);
    const int entity = std::exchange(w.currentEntity, slot.ownerEntity);
    slot.running = true;
    RmWait next{};
    int alive = 0;
    const void* frame = slot.frame;
    auto destroy = slot.destroy;
    const uint64_t id = slot.id;
    const unsigned long code = CallCoroutineResume(slot.resume, slot.frame, &next, &alive);
    w.currentScript = script;
    w.currentEntity = entity;
    // (その間にリストが伸びたかもしれない: 場所を探し直す)
    CoroutineSlot* s = nullptr;
    for (CoroutineSlot& c : w.coroutines) {
        if (c.id == id) s = &c;
    }
    if (code != 0) {
        if (w.log) w.log(2, std::format("コルーチンが止まりました (例外 0x{:08X})。このコルーチンだけを止めます", code));
        if (s) {
            s->dead = true;
            s->running = false;
        }
        return;  // (フレームはそのままにする: 壊れているかもしれない)
    }
    if (!s) return;
    s->running = false;
    if (!alive || s->dead) {
        s->dead = true;
        CallCoroutineDestroy(destroy, const_cast<void*>(frame));
        return;
    }
    s->wait = next;
    s->waitFrom = w.frame;
    // このフレームの Tick の後に始まる待ちは、フレームの終わりから数える。
    const double scaled = w.time + (w.inCoroutinePass ? w.dt : 0.0);
    const double real = w.unscaledTime + (w.inCoroutinePass ? w.unscaledDt : 0.0);
    s->until = (next.kind == RmWait_RealSeconds ? real : scaled) + std::max(0.0, next.seconds);
}
}  // namespace

uint64_t ScriptWorld::StartCoroutine(void* coFrame, int (*resume)(void*, RmWait*), void (*destroy)(void*)) {
    if (!coFrame || !resume || !destroy) return 0;
    if (coroutines.size() >= 100000) {
        TellOnce("coroutines:many", "rm::StartCoroutine: コルーチンが多すぎます (100000 個まで)");
        CallCoroutineDestroy(destroy, coFrame);
        return 0;
    }
    CoroutineSlot slot;
    slot.id = nextCoroutine++;
    slot.frame = coFrame;
    slot.resume = resume;
    slot.destroy = destroy;
    slot.owner = currentScript;
    slot.ownerEntity = currentEntity;
    coroutines.push_back(slot);
    RunCoroutine(*this, coroutines.back());
    for (const CoroutineSlot& c : coroutines) {
        if (c.id == slot.id) return c.dead ? 0 : slot.id;
    }
    return 0;
}

void ScriptWorld::StopCoroutine(uint64_t id) {
    for (CoroutineSlot& c : coroutines) {
        if (c.id != id || c.dead) continue;
        c.dead = true;
        if (!c.running) CallCoroutineDestroy(c.destroy, c.frame);  // (動いている最中: 戻ってきたときに解放する)
    }
}

void ScriptWorld::StopCoroutinesOf(const void* owner) {
    if (!owner) return;
    for (CoroutineSlot& c : coroutines) {
        if (c.owner != owner || c.dead) continue;
        c.dead = true;
        if (!c.running) CallCoroutineDestroy(c.destroy, c.frame);
    }
    if (!inCoroutinePass) std::erase_if(coroutines, [](const CoroutineSlot& c) { return c.dead && !c.running; });
}

void ScriptWorld::ClearCoroutines() {
    for (CoroutineSlot& c : coroutines) {
        if (!c.dead) CallCoroutineDestroy(c.destroy, c.frame);
    }
    coroutines.clear();
}

bool ScriptWorld::CoroutineRunning(uint64_t id) const {
    for (const CoroutineSlot& c : coroutines) {
        if (c.id == id) return !c.dead;
    }
    return false;
}

void ScriptWorld::ResumeCoroutines() {
    inCoroutinePass = true;
    const double scaled = time + dt, real = unscaledTime + unscaledDt;
    // (今あるものだけ: 途中で始まったものはもう動いた)
    std::vector<uint64_t> ids;
    for (const CoroutineSlot& c : coroutines) ids.push_back(c.id);
    for (const uint64_t id : ids) {
        CoroutineSlot* s = nullptr;
        for (CoroutineSlot& c : coroutines) {
            if (c.id == id) s = &c;
        }
        if (!s || s->dead || s->waitFrom >= frame) continue;
        bool ready = false;
        switch (s->wait.kind) {
            case RmWait_Seconds: ready = scaled >= s->until; break;
            case RmWait_RealSeconds: ready = real >= s->until; break;
            case RmWait_Coroutine: ready = !CoroutineRunning(s->wait.coroutine); break;
            default: ready = frame >= s->waitFrom + std::max<uint32_t>(1, s->wait.frames); break;
        }
        if (ready) RunCoroutine(*this, *s);
    }
    inCoroutinePass = false;
    std::erase_if(coroutines, [](const CoroutineSlot& c) { return c.dead && !c.running; });
}

GlobalValue* ScriptWorld::Global(const char* name) {
    if (!name) return nullptr;
    if (GlobalValue* g = globals.Find(name)) return g;
    TellOnce(std::string("global:") + name, std::format("グローバル変数「{}」はゲームの設定の「グローバル変数」にありません", name));
    return nullptr;
}

void ScriptWorld::TellOnce(const std::string& key, std::string_view text) {
    if (told.insert(key).second && log) log(1, text);
}

const InputAction* ScriptWorld::Action(const char* name) {
    if (!name) return nullptr;
    if (const InputAction* a = FindAction(Actions(), name)) return a;  // (プレイヤーの割り当て。変えたとき)
    if (told.insert(std::string("input:") + name).second && log) {
        log(1, std::format("入力のアクション「{}」はゲームの設定の「入力」にありません (いつも押されていない扱いです)", name));
    }
    return nullptr;
}

RmApi MakeScriptApi(ScriptWorld& world) {
    RmApi api{};
    api.abi = RM_ABI_VERSION;
    api.ctx = &world;
    api.core = &kCore;
    api.part = &ApiPart;
    return api;
}

const void* FindScriptApiPart(std::span<const ScriptApiPart> parts, const char* name, uint32_t version) {
    if (!name) return nullptr;
    for (const ScriptApiPart& p : parts) {
        if (p.version == version && std::strcmp(p.name, name) == 0) return p.table;
    }
    return nullptr;
}

std::vector<ScriptApiPart> ScriptApiParts(const RuntimeRegistry& registry) {
    std::vector<ScriptApiPart> out;
    for (const auto& c : registry.Contributions<ScriptApiPart>(kScriptApiPoint)) out.push_back(*c.item);
    return out;
}

std::string CheckModule(const RmModule* m, uint32_t engineAbi, bool engineDebug) {
    if (!m) return "スクリプトの DLL がスクリプトを返しませんでした (RmModuleInit)。エンジンと RM.h の版が違うかもしれません。ビルドし直してください";
    if (m->abi != engineAbi) {
        return std::format("スクリプトの DLL の版 ({}) がエンジン ({}) と違います。ビルドし直してください", m->abi, engineAbi);
    }
    if (((m->flags & RmModuleFlag_Debug) != 0) != engineDebug) {
        return engineDebug ? "スクリプトの DLL が Release でビルドされています。このエンジン (Debug) ではビルドし直してから使います"
                           : "スクリプトの DLL が Debug でビルドされています。このエンジン (Release) ではビルドし直してから使います";
    }
    if (!m->stringGet || !m->stringSet) return "スクリプトの DLL に文字の欄を読み書きする関数がありません。ビルドし直してください";
    if (m->scriptCount > 0 && !m->scripts) return "スクリプトの DLL のスクリプトの一覧が空です";
    std::unordered_set<std::string> names;
    for (uint32_t i = 0; i < m->scriptCount; ++i) {
        const RmScriptDesc& s = m->scripts[i];
        if (!s.name || !*s.name) return "名前の無いスクリプトがあります";
        if (!names.insert(s.name).second) return std::format("スクリプト「{}」が 2 つあります", s.name);
        if (s.size == 0 || !s.construct || !s.destruct || !s.copy) return std::format("スクリプト「{}」の作り方がありません", s.name);
        if (s.align > 16) return std::format("スクリプト「{}」は alignas が大きすぎます (16 まで)", s.name);
        if (s.fieldCount > 0 && !s.fields) return std::format("スクリプト「{}」の欄の一覧が空です", s.name);
        std::unordered_set<const RmStructDesc*> seen;
        if (std::string why = FieldsProblem(s.fields, s.fieldCount, seen, 0); !why.empty()) {
            return std::format("スクリプト「{}」: {}", s.name, why);
        }
    }
    return {};
}

std::vector<std::unique_ptr<ScriptType>> MakeScriptTypes(const RmModule& m) {
    std::vector<std::unique_ptr<ScriptType>> out;
    for (uint32_t i = 0; i < m.scriptCount; ++i) {
        const RmScriptDesc& s = m.scripts[i];
        auto t = std::make_unique<ScriptType>();
        t->script = &s;
        // 文字列: DLL を通してだけ (メモリは DLL の C ランタイムが持つ)。
        t->strings.ctx = &m;
        t->strings.get = [](const void* ctx, const void* field, char* buf, size_t cap) -> size_t {
            return static_cast<size_t>(static_cast<const RmModule*>(ctx)->stringGet(field, buf, cap));
        };
        t->strings.set = [](const void* ctx, void* field, const char* utf8, size_t len) {
            static_cast<const RmModule*>(ctx)->stringSet(field, utf8, len);
        };
        t->assets = {nullptr, &AssetGet, &AssetSet};
        for (uint32_t f = 0; f < s.fieldCount; ++f) t->fields.push_back(MakeField(*t, s.fields[f]));
        t->desc = reflect::TypeDesc{s.name,
                                    s.label && *s.label ? s.label : s.name,
                                    reflect::TypeKind::Script,
                                    static_cast<size_t>(s.size),
                                    std::span<const reflect::FieldDesc>(t->fields),
                                    s.construct,
                                    s.destruct,
                                    s.copy,
                                    &ScriptCppType,
                                    s.header ? s.header : ""};
        Guard(*t, t->desc);
        out.push_back(std::move(t));
    }
    return out;
}

ScriptType::~ScriptType() {
    std::lock_guard lock(g_guardsMutex);
    for (const size_t i : guards) g_guards[i].used = false;
}

bool ScriptModule::Load(const std::filesystem::path& dll, const RmApi& api, std::string* why) {
    Unload();
    auto fail = [&](std::string msg) {
        if (why) *why = std::move(msg);
        Unload();
        return false;
    };
    HMODULE h = LoadLibraryW(dll.c_str());
    if (!h) return fail(std::format("スクリプトの DLL ({}) を読めませんでした (エラー {})", PathToUtf8(dll.filename()), GetLastError()));
    library_ = h;
    path_ = dll;
    const auto init = reinterpret_cast<RmModuleInitFn>(reinterpret_cast<void*>(GetProcAddress(h, "RmModuleInit")));
    if (!init) return fail("スクリプトの DLL に RmModuleInit がありません (RM Engine のスクリプトの DLL ではありません)");
    const RmModule* m = nullptr;
    if (const unsigned long code = CallModuleInit(init, &api, RM_ABI_VERSION, &m); code != 0) {
        return fail(std::format("スクリプトの DLL を始めるところで止まりました (例外 0x{:08X})", code));
    }
    if (std::string problem = CheckModule(m); !problem.empty()) return fail(problem);
    module_ = m;
    types_ = MakeScriptTypes(*m);
    return true;
}

void ScriptModule::Unload() {
    types_.clear();
    module_ = nullptr;
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
    path_.clear();
}

void ScriptModule::Adopt(const RmModule& module) {
    Unload();
    module_ = &module;
    types_ = MakeScriptTypes(module);
}

std::vector<const reflect::TypeDesc*> ScriptModule::Types() const {
    std::vector<const reflect::TypeDesc*> out;
    for (const auto& t : types_) out.push_back(&t->desc);
    return out;
}

const ScriptType* ScriptModule::Find(std::string_view name) const {
    for (const auto& t : types_) {
        if (name == t->desc.name) return t.get();
    }
    return nullptr;
}

int ScriptDllNumber(std::wstring_view f) {
    constexpr std::wstring_view pre = L"Scripts_", ext = L".dll";
    if (f.size() <= pre.size() + ext.size() || f.substr(0, pre.size()) != pre || f.substr(f.size() - ext.size()) != ext) return -1;
    const std::wstring_view digits = f.substr(pre.size(), f.size() - pre.size() - ext.size());
    int n = 0;
    for (const wchar_t c : digits) {
        if (c < L'0' || c > L'9' || n > 100000000) return -1;
        n = n * 10 + (c - L'0');
    }
    return n;
}

std::filesystem::path LatestScriptDll(const std::filesystem::path& folder) {
    std::filesystem::path best;
    int bestN = -1;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const int n = ScriptDllNumber(it->path().filename().native());
        if (n > bestN) {
            bestN = n;
            best = it->path();
        }
    }
    return best;
}

int ScriptOrderOf(std::string_view type, std::span<const ScriptOrderEntry> order) {
    for (const ScriptOrderEntry& e : order) {
        if (e.type == type) return e.order;
    }
    return 0;
}

std::vector<std::string> SortScriptTypes(std::vector<std::string> types, std::span<const ScriptOrderEntry> order) {
    std::sort(types.begin(), types.end(), [&](const std::string& a, const std::string& b) {
        const int oa = ScriptOrderOf(a, order), ob = ScriptOrderOf(b, order);
        return oa != ob ? oa < ob : a < b;
    });
    types.erase(std::unique(types.begin(), types.end()), types.end());
    return types;
}

std::vector<ScriptRunner::Instance> ScriptRunner::Collect(Scene& scene, const ScriptModule& module,
                                                          std::span<const ScriptOrderEntry> order) const {
    std::vector<Instance> out;
    std::vector<std::string> present;
    for (const TreeEntry& t : scene.TreeOrder()) {
        SceneEntity* e = scene.Find(t.entity->data.id);
        for (Component& c : e->components) {
            if (!c.Known()) continue;
            if (const ScriptType* type = module.Find(c.type)) {
                out.push_back({e->data.id, type, c.value.Get(), t.activeInHierarchy});
                present.push_back(c.type);
            }
        }
    }
    const std::vector<std::string> sorted = SortScriptTypes(present, order);
    std::unordered_map<std::string_view, size_t> rank;
    for (size_t i = 0; i < sorted.size(); ++i) rank[sorted[i]] = i;
    std::stable_sort(out.begin(), out.end(), [&](const Instance& a, const Instance& b) {
        const size_t ra = rank[a.type->desc.name], rb = rank[b.type->desc.name];
        return ra != rb ? ra < rb : a.entity < b.entity;  // (同じエンティティ: 付けた順)
    });
    return out;
}

bool ScriptRunner::Call(ScriptWorld& world, const Instance& in, int hook, float dt, int other, const RmEvent* event) {
    const void* script = std::exchange(world.currentScript, in.data);
    const int entity = std::exchange(world.currentEntity, in.entity);
    const auto t0 = timing_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    const unsigned long code = CallScriptHook(*in.type->script, hook, in.data, static_cast<RmEntityId>(in.entity), dt,
                                              static_cast<RmEntityId>(std::max(other, 0)), event);
    if (timing_) {
        ScriptTime& t = times_[in.type];
        t.type = in.type;
        t.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ++t.calls;
    }
    world.currentScript = script;
    world.currentEntity = entity;
    if (hook == RM_HOOK_END_PLAY || code != 0) world.StopCoroutinesOf(in.data);  // (コルーチンも一緒に終わる)
    if (code == 0) return true;
    crashed_.insert(in.data);
    if (world.log) {
        const SceneEntity* e = world.scene ? world.scene->Find(in.entity) : nullptr;
        world.log(2, std::format("スクリプト「{}」(エンティティ「{}」) が {} の中で止まりました (例外 0x{:08X})。このスクリプトだけを止めます",
                                 in.type->desc.label, e ? e->data.name : std::string(), HookName(hook), code));
    }
    return false;
}

std::vector<ScriptRunner::ScriptTime> ScriptRunner::Times() const {
    std::vector<ScriptTime> out;
    for (const auto& [type, t] : times_) out.push_back(t);
    std::sort(out.begin(), out.end(), [](const ScriptTime& a, const ScriptTime& b) { return a.ms > b.ms; });
    return out;
}

void ScriptRunner::Frame(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order, float dt) {
    times_.clear();
    world.scene = &scene;
    world.dt = dt;
    world.ReadInput();
    const std::vector<Instance> list = Collect(scene, module, order);
    std::unordered_set<void*> present;
    for (const Instance& in : list) present.insert(in.data);
    // 前のフレームから無効になった: EndPlay (有効に戻ったら BeginPlay をもう一度)。
    for (const Instance& in : list) {
        if (in.active || !started_.contains(in.data)) continue;
        if (!crashed_.contains(in.data)) Call(world, in, 2, 0);
        started_.erase(in.data);
    }
    // エンティティと一緒に消えた: オブジェクトも消えている (呼ぶものはない)。コルーチンも。
    for (void* p : started_) {
        if (!present.contains(p)) world.StopCoroutinesOf(p);
    }
    std::erase_if(started_, [&](void* p) { return !present.contains(p); });
    std::erase_if(crashed_, [&](void* p) { return !present.contains(p); });
    // 新しいもの (前のフレームが作ったものも) の BeginPlay を先に、そのあと全部の Tick。
    for (const Instance& in : list) {
        if (!in.active || started_.contains(in.data) || crashed_.contains(in.data)) continue;
        started_.insert(in.data);
        Call(world, in, 0, 0);
    }
    for (const Instance& in : list) {
        if (!in.active || crashed_.contains(in.data) || !started_.contains(in.data)) continue;
        Call(world, in, 1, dt);
    }
    ApplyAsked(scene, world, module, order);  // 壊した / 外したものを片付ける (EndPlay がコルーチンを止める)
    {
        const auto t0 = std::chrono::steady_clock::now();
        world.ResumeCoroutines();  // 待ちが終わったコルーチン
        if (timing_ && !world.coroutines.empty()) {
            ScriptTime& t = times_[nullptr];
            t.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            t.calls = static_cast<int>(world.coroutines.size());
        }
    }
    DeliverEvents(scene, world, module, order);
    ApplyAsked(scene, world, module, order);  // フレームの終わり
    world.time += dt;
    world.unscaledTime += world.unscaledDt;
    ++world.frame;
}

void ScriptRunner::DeliverEvents(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order) {
    if (!module.Loaded()) {
        world.events.clear();
        return;
    }
    for (int round = 0; round < 8 && !world.events.empty(); ++round) {
        const std::vector<QueuedEvent> events = std::exchange(world.events, {});
        const std::vector<Instance> list = Collect(scene, module, order);
        for (const QueuedEvent& q : events) {
            std::unordered_set<int> reach;
            if (q.scope == 2) {
                const int roots[] = {q.target};
                reach = scene.Subtree(roots);
            }
            const RmEvent ev{q.name.c_str(), static_cast<RmEntityId>(q.sender), q.number, q.text.c_str(), static_cast<RmEntityId>(q.entity)};
            for (const Instance& in : list) {
                if (!in.type->script->onEvent || !in.active || !started_.contains(in.data) || crashed_.contains(in.data)) continue;
                if ((q.scope == 1 && in.entity != q.target) || (q.scope == 2 && !reach.contains(in.entity))) continue;
                if (!scene.Find(in.entity)) continue;
                Call(world, in, RM_HOOK_EVENT, 0, 0, &ev);
            }
        }
        ApplyAsked(scene, world, module, order);
    }
    if (!world.events.empty()) {
        world.TellOnce("events:loop", "rm::Events: OnEvent の中で送ったイベントが 8 回続いたので、残りは捨てました (送り合いが止まらないかもしれません)");
        world.events.clear();
    }
}

void ScriptRunner::Deliver(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                           std::span<const HookCall> calls) {
    if (!module.Loaded()) {
        world.events.clear();  // (受け取るスクリプトが無い: サービスが送ったものも溜めない)
        return;
    }
    // フックが無くても、サービスが送ったイベント (ゲーム UI の "UIChanged" …) はここで届ける。
    if (calls.empty() && world.events.empty()) return;
    world.scene = &scene;
    const std::vector<Instance> list = Collect(scene, module, order);
    for (const HookCall& h : calls) {
        for (const Instance& in : list) {
            // BeginPlay を呼んだものだけ (EndPlay はまだのもの): フックはこの 2 つの間に入る。
            if (in.entity != h.entity || !in.active || !started_.contains(in.data) || crashed_.contains(in.data)) continue;
            Call(world, in, h.hook, 0, h.other);
        }
    }
    ApplyAsked(scene, world, module, order);
    DeliverEvents(scene, world, module, order);  // (それらのフックが送ったもの)
}

void ScriptRunner::ApplyAsked(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order) {
    for (int round = 0; round < 8 && (!world.destroyAsked.empty() || !world.removeAsked.empty()); ++round) {
        const std::vector<int> destroy = std::exchange(world.destroyAsked, {});
        const std::vector<std::pair<int, const void*>> remove = std::exchange(world.removeAsked, {});
        std::vector<int> roots;
        for (const int id : destroy) {
            if (scene.Find(id) && std::find(roots.begin(), roots.end(), id) == roots.end()) roots.push_back(id);
        }
        std::unordered_set<int> gone = scene.Subtree(roots);
        std::unordered_set<const void*> goneObjects;
        for (const auto& [id, object] : remove) goneObjects.insert(object);
        // 去るスクリプトの EndPlay (始まったスクリプトだけ: BeginPlay と EndPlay は必ず対で)。
        auto endPlay = [&](const std::unordered_set<int>& entities) {
            for (const Instance& in : Collect(scene, module, order)) {
                if (!entities.contains(in.entity) && !goneObjects.contains(in.data)) continue;
                if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 2, 0);
                started_.erase(in.data);  // (そのメモリに次の新しいスクリプトが入るかもしれない: このスクリプトとは決して見なさない)
                crashed_.erase(in.data);
            }
        };
        endPlay(gone);
        // EndPlay が、去るエンティティの下に別のエンティティを入れたかもしれない (Entity::SetParent)。
        // それも一緒に去るので、そのスクリプトの EndPlay も呼ぶ (2026-11-19: 前は EndPlay 無しで消えていた)。
        for (int pass = 0; pass < 8; ++pass) {
            std::unordered_set<int> joined;
            for (const int id : scene.Subtree(roots)) {
                if (!gone.contains(id)) joined.insert(id);
            }
            if (joined.empty()) break;
            gone.insert(joined.begin(), joined.end());
            endPlay(joined);
        }
        for (const auto& [id, object] : remove) {
            SceneEntity* e = scene.Find(id);
            if (!e || gone.contains(id)) continue;
            for (const Component& c : e->components) {
                if (c.value.Get() != object) continue;
                scene.RemoveComponent(id, c.type, e->NthOf(c));
                break;
            }
        }
        if (!roots.empty()) scene.Delete(roots);
    }
    if (!world.destroyAsked.empty() || !world.removeAsked.empty()) {
        world.TellOnce("destroy:loop", "Destroy / Remove: EndPlay の中で消す依頼が 8 回続いたので、残りは捨てました (消し合いが止まらないかもしれません)");
    }
    world.destroyAsked.clear();
    world.removeAsked.clear();
}

std::vector<ScriptRunner::RunState> ScriptRunner::Remember(const Scene& scene) const {
    std::vector<RunState> out;
    for (const SceneEntity& e : scene.Entities()) {
        for (const Component& c : e.components) {
            void* data = const_cast<void*>(c.value.Get());
            const bool started = data && started_.contains(data), crashed = data && crashed_.contains(data);
            if (started || crashed) out.push_back({e.data.id, c.type, e.NthOf(c), started, crashed});
        }
    }
    return out;
}

void ScriptRunner::Resume(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                          std::span<const RunState> before) {
    Reset();
    world.scene = &scene;
    for (const RunState& r : before) {
        SceneEntity* e = scene.Find(r.entity);
        Component* c = e ? e->FindComponent(r.type, r.nth) : nullptr;
        if (!c || !c->Known() || !module.Find(r.type)) continue;  // (消えたスクリプト: 欠けたものとして残す)
        if (r.started) started_.insert(c->value.Get());
        if (r.crashed) crashed_.insert(c->value.Get());
    }
    for (const Instance& in : Collect(scene, module, order)) {
        if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 3, 0);
    }
}

void ScriptRunner::Stop(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order) {
    world.scene = &scene;
    for (const Instance& in : Collect(scene, module, order)) {
        if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 2, 0);
    }
    Reset();
    world.ClearCoroutines();     // (全部: EndPlay 無しで消えたスクリプトのものも)
    world.events.clear();
    world.destroyAsked.clear();  // (シーンごと消える)
    world.removeAsked.clear();
    // Play したシーンは次に捨てる。部品を足したときや、スクリプトを入れ替えたときに作られるスクリプトの
    // 初期値が、ワールドに問い合わせても、そのシーンを見てはいけない。
    world.scene = nullptr;
}

SceneSwitch SwitchAskedScene(Scene& playing, ScriptWorld& world, ScriptRunner& runner, const ScriptModule& module,
                             std::span<const ScriptOrderEntry> order, const std::function<bool(size_t index, Scene& into, std::string* error)>& load,
                             std::string* error) {
    const int asked = std::exchange(world.sceneAsked, -1);
    if (asked < 0) return SceneSwitch::None;
    // 先に読む: 読めないシーンのときは、Play 中のシーンをそのまま残す (スクリプトは動き続ける)。
    Scene next;
    next.SetComponentTypes(playing.ComponentTypes(), playing.MultipleTypes());
    next.SetSettingTypes(playing.SettingTypes());
    if (!load(static_cast<size_t>(asked), next, error)) return SceneSwitch::Failed;
    if (module.Loaded()) runner.Stop(playing, world, module, order);  // このシーンのスクリプトの EndPlay
    runner.Reset();
    playing = std::move(next);
    world.sceneIndex = asked;
    world.sceneAsked = -1;  // (EndPlay の中でまた頼まれた: 受けない。切り替えはこの 1 回)
    world.scene = &playing;
    return SceneSwitch::Switched;
}

void ScriptRunner::Reset() {
    started_.clear();
    crashed_.clear();
}

}  // namespace rm::game_production
