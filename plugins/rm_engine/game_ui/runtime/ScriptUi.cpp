// スクリプトの API (RMUI.h / RMUIAbi.h) のうち、ゲーム UI の部分。動いているシーンに対して働く。
// RmApi::ctx はゲーム制作の ScriptWorld。

#include "rm_engine/game_ui/runtime/ScriptUi.h"

#include "base/Utf.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"
#include "rm_engine/game_ui/runtime/UiControls.h"
#include "rm_engine/game_ui/runtime/UiService.h"
#include "rm_engine/game_ui/runtime/sdk/RMUIAbi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

namespace rm::game_ui {

namespace {

using game_production::ScriptWorld;

ScriptWorld& World(void* ctx) { return *static_cast<ScriptWorld*>(ctx); }

template <class T>
T* PartOf(void* ctx, uint64_t id) {
    ScriptWorld& w = World(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return nullptr;
    game_production::SceneEntity* e = w.scene->Find(static_cast<int>(id));
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<T>().name) : nullptr;
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

Color ColorOf(const float v[4]) { return {v[0], v[1], v[2], v[3]}; }

// 名前で探す UI のウィンドウ (キャンバス): 階層でその名前の最初のもの。なければ 1 回だけ知らせる。
game_production::SceneEntity* Window(void* ctx, const char* name) {
    ScriptWorld& w = World(ctx);
    if (!w.scene || !name) return nullptr;
    for (const auto& t : w.scene->TreeOrder()) {
        if (t.entity->data.name == name && t.entity->FindComponent(reflect::TypeOf<UiCanvas>().name)) return w.scene->Find(t.entity->data.id);
    }
    w.TellOnce(std::string("ui:") + name, std::format("rm::UI: 「{}」という名前のキャンバス (UI のウィンドウ) がシーンにありません", name));
    return nullptr;
}

int SetText(void* ctx, uint64_t id, const char* utf8) {
    // 入力欄: その入っている文字 (同じエンティティの文字 (UI) は見た目だけ)。
    if (UiInputField* f = PartOf<UiInputField>(ctx, id)) {
        f->text = utf8 ? utf8 : "";
        f->caret = f->anchor = static_cast<int>(Utf8ToWide(f->text).size());
        return 1;
    }
    UiText* t = PartOf<UiText>(ctx, id);
    if (!t) return 0;
    t->text = utf8 ? utf8 : "";
    return 1;
}

uint32_t GetText(void* ctx, uint64_t id, char* buf, uint32_t cap) {
    const UiInputField* f = PartOf<UiInputField>(ctx, id);
    const UiText* t = PartOf<UiText>(ctx, id);
    const std::string s = f ? f->text : t ? t->text : std::string();
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(s.size(), cap - 1);
        std::memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    return static_cast<uint32_t>(s.size());
}

int SetTextColor(void* ctx, uint64_t id, const float rgba[4]) {
    UiText* t = PartOf<UiText>(ctx, id);
    if (!t || !rgba) return 0;
    t->color = ColorOf(rgba);
    return 1;
}

int SetImage(void* ctx, uint64_t id, const char* assetId) {
    UiImage* i = PartOf<UiImage>(ctx, id);
    if (!i) return 0;
    i->image.guid = assetId ? assetId : "";
    return 1;
}

int SetImageColor(void* ctx, uint64_t id, const float rgba[4]) {
    UiImage* i = PartOf<UiImage>(ctx, id);
    if (!i || !rgba) return 0;
    i->color = ColorOf(rgba);
    return 1;
}

int SetBar(void* ctx, uint64_t id, float value) {
    UiBar* b = PartOf<UiBar>(ctx, id);
    if (!b) return 0;
    b->value = std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
    return 1;
}

float GetBar(void* ctx, uint64_t id) {
    const UiBar* b = PartOf<UiBar>(ctx, id);
    return b ? b->value : -1.f;
}

int SetInteractable(void* ctx, uint64_t id, int on) {
    UiButton* b = PartOf<UiButton>(ctx, id);
    if (!b) return 0;
    b->interactable = on != 0;
    return 1;
}

int ShowWindow(void* ctx, const char* name, int shown) {
    game_production::SceneEntity* e = Window(ctx, name);
    if (!e) return 0;
    e->data.active = shown != 0;
    return 1;
}

int SwitchWindow(void* ctx, const char* name) {
    game_production::SceneEntity* target = Window(ctx, name);
    if (!target) return 0;
    SwitchToWindow(*World(ctx).scene, target->data.id);  // (ボタンの ウィンドウを切り替える と同じ)
    return 1;
}

int WindowShown(void* ctx, const char* name) {
    ScriptWorld& w = World(ctx);
    if (!w.scene || !name) return 0;
    for (const auto& t : w.scene->TreeOrder()) {
        if (t.entity->data.name == name && t.entity->FindComponent(reflect::TypeOf<UiCanvas>().name)) return t.activeInHierarchy ? 1 : 0;
    }
    return 0;
}

int Focus(void* ctx, uint64_t id) {
    ScriptWorld& w = World(ctx);
    if (!PartOf<UiButton>(ctx, id) || !w.services) return 0;
    auto* ui = dynamic_cast<UiService*>(w.services->Find(kUiServiceName));
    if (!ui) return 0;
    ui->Focus(static_cast<int>(id));
    return 1;
}

// トゥイーンは UI サービスを通す (サービスが毎フレームそれを進める)。
UiService* Service(void* ctx) {
    ScriptWorld& w = World(ctx);
    return w.services ? dynamic_cast<UiService*>(w.services->Find(kUiServiceName)) : nullptr;
}
UiEase EaseOf(int ease) { return ease >= 0 && ease <= static_cast<int>(UiEase::Back) ? static_cast<UiEase>(ease) : UiEase::Out; }
int Tween(void* ctx, uint64_t id, UiTweenKind kind, Vec2 to, float seconds, int ease) {
    ScriptWorld& w = World(ctx);
    UiService* ui = Service(ctx);
    return ui && w.scene && StartTween(*w.scene, ui->Motion(), static_cast<int>(id), kind, to, seconds, EaseOf(ease)) ? 1 : 0;
}
int MoveTo(void* ctx, uint64_t id, float x, float y, float seconds, int ease) { return Tween(ctx, id, UiTweenKind::Move, {x, y}, seconds, ease); }
int ScaleTo(void* ctx, uint64_t id, float scale, float seconds, int ease) { return Tween(ctx, id, UiTweenKind::Scale, {scale, 0}, seconds, ease); }
int RotateTo(void* ctx, uint64_t id, float degrees, float seconds, int ease) { return Tween(ctx, id, UiTweenKind::Turn, {degrees, 0}, seconds, ease); }
int FadeTo(void* ctx, uint64_t id, float alpha, float seconds, int ease) { return Tween(ctx, id, UiTweenKind::Fade, {alpha, 0}, seconds, ease); }
int PlayPathOf(void* ctx, uint64_t id, int how) {
    ScriptWorld& w = World(ctx);
    return w.scene && PlayPath(*w.scene, static_cast<int>(id), how) ? 1 : 0;
}

// --- 版 3: 入力の部品 ---------------------------------------------------------------------------

// エンティティの一番外のキャンバス (トグルのグループはその中で 1 つ)。
int TopCanvas(const game_production::Scene& scene, int id) {
    int canvas = 0;
    size_t steps = 0;
    for (const game_production::SceneEntity* e = scene.Find(id); e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
        if (e->FindComponent(reflect::TypeOf<UiCanvas>().name)) canvas = e->data.id;
    }
    return canvas;
}

int GetValue(void* ctx, uint64_t id, float* out) {
    float v = 0;
    if (const UiSlider* s = PartOf<UiSlider>(ctx, id)) {
        v = SliderSnap(*s, s->value);
    } else if (const UiToggle* t = PartOf<UiToggle>(ctx, id)) {
        v = t->on ? 1.f : 0.f;
    } else {
        return 0;
    }
    if (out) *out = v;
    return 1;
}

int SetValue(void* ctx, uint64_t id, float value) {
    if (UiSlider* s = PartOf<UiSlider>(ctx, id)) {
        s->value = SliderSnap(*s, value);
        return 1;
    }
    UiToggle* t = PartOf<UiToggle>(ctx, id);
    if (!t) return 0;
    t->on = value != 0;
    if (t->on && !t->group.empty()) {
        // 同じウィンドウの同じグループのほかは切る (ラジオボタン)。
        game_production::Scene& scene = *World(ctx).scene;
        const int canvas = TopCanvas(scene, static_cast<int>(id));
        for (const auto& row : scene.TreeOrder()) {
            const int other = row.entity->data.id;
            if (other == static_cast<int>(id)) continue;
            UiToggle* o = PartOf<UiToggle>(ctx, static_cast<uint64_t>(other));
            if (o && o->group == t->group && TopCanvas(scene, other) == canvas) o->on = false;
        }
    }
    return 1;
}

uint64_t Typing(void* ctx) {
    UiService* ui = Service(ctx);
    return ui ? static_cast<uint64_t>(ui->State().typing) : 0;
}

int StartTypingIn(void* ctx, uint64_t id) {
    ScriptWorld& w = World(ctx);
    UiService* ui = Service(ctx);
    if (!ui || !w.scene) return 0;
    if (id == 0) {
        StopTyping(*w.scene, ui->MutableState(), false);
        return 1;
    }
    if (!PartOf<UiInputField>(ctx, id)) return 0;
    return StartTyping(*w.scene, ui->MutableState(), static_cast<int>(id)) ? 1 : 0;
}

// offset と 0..1 (範囲は UI サービスが毎フレーム書いたもの)。
float Fraction(float v, float low, float high) { return high - low > 1e-4f ? std::clamp((v - low) / (high - low), 0.f, 1.f) : 0.f; }

int GetScroll(void* ctx, uint64_t id, float out[2]) {
    const UiScroll* s = PartOf<UiScroll>(ctx, id);
    if (!s) return 0;
    if (out) {
        out[0] = Fraction(s->offset.x, s->low.x, s->high.x);
        out[1] = Fraction(s->offset.y, s->low.y, s->high.y);
    }
    return 1;
}

int SetScroll(void* ctx, uint64_t id, float x, float y) {
    UiScroll* s = PartOf<UiScroll>(ctx, id);
    if (!s) return 0;
    auto at = [](float f, float low, float high) { return low + (high - low) * (std::isfinite(f) ? std::clamp(f, 0.f, 1.f) : 0.f); };
    s->offset = {at(x, s->low.x, s->high.x), at(y, s->low.y, s->high.y)};
    return 1;
}

constexpr RmUiApi kTable{&SetText,    &GetText,      &SetTextColor, &SetImage, &SetImageColor, &SetBar,   &GetBar,   &SetInteractable,
                         &ShowWindow, &SwitchWindow, &WindowShown,  &Focus,    &MoveTo,        &ScaleTo,  &RotateTo, &FadeTo,
                         &PlayPathOf, &GetValue,     &SetValue,     &Typing,   &StartTypingIn, &GetScroll, &SetScroll};

}  // namespace

const game_production::ScriptApiPart& ScriptUiPart() {
    static const game_production::ScriptApiPart part{RM_PART_UI, RM_PART_UI_VERSION, &kTable};
    return part;
}

const game_production::ScriptApiPart& ScriptUiPartVersion1() {
    // バージョン 1 の表は、今の版の先頭部分 (同じ関数が同じ場所にある)。
    static const game_production::ScriptApiPart part{RM_PART_UI, 1u, &kTable};
    return part;
}

const game_production::ScriptApiPart& ScriptUiPartVersion2() {
    // バージョン 2 (トゥイーンと道まで) も今の版の先頭部分。
    static const game_production::ScriptApiPart part{RM_PART_UI, 2u, &kTable};
    return part;
}

}  // namespace rm::game_ui
