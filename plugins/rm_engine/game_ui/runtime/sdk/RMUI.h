// RMUI.h - the game UI for scripts (the plugin ゲーム UI gives it).
//
//   #include "RMUI.h"
//   rm::UI::SetText(scoreLabel, "スコア " + std::to_string(Global::score));   // a 文字 entity
//   rm::UI::SetBar(hpBar, hp / maxHp);                                         // a バー entity: 0..1
//   rm::UI::SetImage(icon, iconImage);                                         // a 画像 entity (rm::ImageRef)
//   rm::UI::SwitchTo("Pause");     // that UI window (a キャンバス entity, by name) shown, the others hidden
//   rm::UI::Show("HUD"); rm::UI::Hide("HUD"); rm::UI::IsShown("HUD");
//   rm::UI::SetInteractable(button, false);
//   rm::UI::MoveTo(panel, {0, 40}, 0.3f);              // 位置 there over 0.3 s (rm::UI::Ease::Out)
//   rm::UI::ScaleTo(icon, 1.2f, 0.2f, rm::UI::Ease::Back); rm::UI::RotateTo(arrow, 90, 0.5f); rm::UI::FadeTo(hint, 0, 1);
//   rm::UI::PlayPath(bird); rm::UI::StopPath(bird);    // its 道に沿って動く
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
// How a tween goes over its time (as the inspector's 速さの曲線).
enum class Ease : int { Linear = 0, In = 1, Out = 2, InOut = 3, Bounce = 4, Back = 5 };
// From where it is now to there, over `seconds` (0: at once). Another of the same kind takes over.
inline void MoveTo(Entity e, Vec2 position, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->moveTo(Api()->ctx, e.id, position.x, position.y, seconds, static_cast<int>(ease));
}
inline void ScaleTo(Entity e, float scale, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->scaleTo(Api()->ctx, e.id, scale, seconds, static_cast<int>(ease));
}
inline void RotateTo(Entity e, float degrees, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->rotateTo(Api()->ctx, e.id, degrees, seconds, static_cast<int>(ease));
}
// 0: unseen .. 1: as it is (its children fade with it).
inline void FadeTo(Entity e, float alpha, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->fadeTo(Api()->ctx, e.id, alpha, seconds, static_cast<int>(ease));
}
// Its 道に沿って動く: on from where it is / stopped / from the start.
inline void PlayPath(Entity e) {
    if (const RmUiApi* u = detail::Get()) u->playPath(Api()->ctx, e.id, 1);
}
inline void StopPath(Entity e) {
    if (const RmUiApi* u = detail::Get()) u->playPath(Api()->ctx, e.id, 0);
}
inline void RestartPath(Entity e) {
    if (const RmUiApi* u = detail::Get()) u->playPath(Api()->ctx, e.id, 2);
}
}  // namespace UI
}  // namespace rm
