// RMUI.h - the game UI for scripts (the plugin ゲーム UI gives it).
//
//   #include "RMUI.h"
//   rm::UI::SetText(scoreLabel, "スコア " + std::to_string(Global::score));   // a 文字 entity
//   rm::UI::SetBar(hpBar, hp / maxHp);                                         // a バー entity: 0..1
//   rm::UI::SetImage(icon, iconImage);                                         // a 画像 entity (rm::ImageRef)
//   rm::UI::SwitchTo("Pause");     // that UI window (a キャンバス entity, by name) shown, the others hidden
//   rm::UI::Show("HUD"); rm::UI::Hide("HUD"); rm::UI::IsShown("HUD");
//   rm::UI::SetInteractable(button, false);
//
// A button calls the scripts of its own entity:
//   void OnClick(rm::Entity self);   // pressed with the mouse, or 決定 (the keys / a pad) on the chosen one
//
// To show or hide one thing, switch its entity: rm::Entity::SetActive (RM.h).
// An entity without that UI part: the setters do nothing, the getters give "" / -1.
#pragma once

#include "RM.h"
#include "RMUIAbi.h"

#include <string>

namespace rm {
namespace UI {
namespace detail {
inline const RmUiApi* Get() {
    static const RmUiApi* api = Part<RmUiApi>(RM_PART_UI, RM_PART_UI_VERSION);  // the engine's, for good
    return api;
}
}  // namespace detail

inline void SetText(Entity e, const std::string& text) {
    if (const RmUiApi* u = detail::Get()) u->setText(Api()->ctx, e.id, text.c_str());
}
inline std::string Text(Entity e) {
    const RmUiApi* u = detail::Get();
    if (!u) return {};
    char buf[256];
    const uint32_t n = u->getText(Api()->ctx, e.id, buf, sizeof(buf));
    if (n < sizeof(buf)) return std::string(buf, n);
    std::string all(n + 1, '\0');
    u->getText(Api()->ctx, e.id, all.data(), n + 1);
    all.resize(n);
    return all;
}
inline void SetTextColor(Entity e, Color c) {
    const float v[4] = {c.r, c.g, c.b, c.a};
    if (const RmUiApi* u = detail::Get()) u->setTextColor(Api()->ctx, e.id, v);
}
inline void SetImage(Entity e, const ImageRef& image) {
    if (const RmUiApi* u = detail::Get()) u->setImage(Api()->ctx, e.id, image.guid);
}
inline void SetImageColor(Entity e, Color c) {
    const float v[4] = {c.r, c.g, c.b, c.a};
    if (const RmUiApi* u = detail::Get()) u->setImageColor(Api()->ctx, e.id, v);
}
// A バー's value: 0 empty .. 1 full.
inline void SetBar(Entity e, float value) {
    if (const RmUiApi* u = detail::Get()) u->setBar(Api()->ctx, e.id, value);
}
inline float Bar(Entity e) {
    const RmUiApi* u = detail::Get();
    return u ? u->getBar(Api()->ctx, e.id) : -1.0f;
}
inline void SetInteractable(Entity e, bool on) {
    if (const RmUiApi* u = detail::Get()) u->setInteractable(Api()->ctx, e.id, on ? 1 : 0);
}
// UI windows (キャンバス entities) by name.
inline bool Show(const char* window) {
    const RmUiApi* u = detail::Get();
    return u && u->showWindow(Api()->ctx, window, 1) != 0;
}
inline bool Hide(const char* window) {
    const RmUiApi* u = detail::Get();
    return u && u->showWindow(Api()->ctx, window, 0) != 0;
}
inline bool SwitchTo(const char* window) {
    const RmUiApi* u = detail::Get();
    return u && u->switchWindow(Api()->ctx, window) != 0;
}
inline bool IsShown(const char* window) {
    const RmUiApi* u = detail::Get();
    return u && u->windowShown(Api()->ctx, window) != 0;
}
// The keys' focus on this button (shown as chosen until the mouse moves).
inline void Focus(Entity button) {
    if (const RmUiApi* u = detail::Get()) u->focus(Api()->ctx, button.id);
}
}  // namespace UI
}  // namespace rm
