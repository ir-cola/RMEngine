// The game UI's part of the scripts' API (RMUI.h / RMUIAbi.h), on the scene that plays.
// RmApi::ctx is game production's ScriptWorld.

#include "rm_engine/game_ui/runtime/ScriptUi.h"

#include "rm_engine/game_ui/runtime/UiComponents.h"
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

// The UI windows (canvases) by name: the first in the 階層 of that name; told once when none.
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
    UiText* t = PartOf<UiText>(ctx, id);
    if (!t) return 0;
    t->text = utf8 ? utf8 : "";
    return 1;
}

uint32_t GetText(void* ctx, uint64_t id, char* buf, uint32_t cap) {
    const UiText* t = PartOf<UiText>(ctx, id);
    const std::string s = t ? t->text : std::string();
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
    SwitchToWindow(*World(ctx).scene, target->data.id);  // (as a button's ウィンドウを切り替える)
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

constexpr RmUiApi kTable{&SetText, &GetText, &SetTextColor, &SetImage, &SetImageColor, &SetBar, &GetBar, &SetInteractable,
                         &ShowWindow, &SwitchWindow, &WindowShown, &Focus};

}  // namespace

const game_production::ScriptApiPart& ScriptUiPart() {
    static const game_production::ScriptApiPart part{RM_PART_UI, RM_PART_UI_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_ui
