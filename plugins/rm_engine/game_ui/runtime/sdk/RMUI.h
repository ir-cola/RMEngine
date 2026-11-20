// RMUI.h - スクリプト用のゲーム UI (プラグイン ゲーム UI が提供する)。
//
//   #include "RMUI.h"
//   rm::UI::SetText(scoreLabel, "スコア " + std::to_string(Global::score));   // 文字 エンティティ
//   rm::UI::SetBar(hpBar, hp / maxHp);                                         // バー エンティティ: 0..1
//   rm::UI::SetImage(icon, iconImage);                                         // 画像 エンティティ (rm::ImageRef)
//   rm::UI::SwitchTo("Pause");     // その UI ウィンドウ (名前で指す キャンバス エンティティ) を表示し、ほかは隠す
//   rm::UI::Show("HUD"); rm::UI::Hide("HUD"); rm::UI::IsShown("HUD");
//   rm::UI::SetInteractable(button, false);
//   rm::UI::MoveTo(panel, {0, 40}, 0.3f);              // 位置 をそこへ 0.3 秒かけて (rm::UI::Ease::Out)
//   rm::UI::ScaleTo(icon, 1.2f, 0.2f, rm::UI::Ease::Back); rm::UI::RotateTo(arrow, 90, 0.5f); rm::UI::FadeTo(hint, 0, 1);
//   rm::UI::PlayPath(bird); rm::UI::StopPath(bird);    // その 道に沿って動く
//
// ボタンは自分のエンティティのスクリプトを呼ぶ:
//   void OnClick(rm::Entity self);   // マウスで押された、または選ばれたボタンへの 決定 (キー / パッド)
//
// 1 つのものを表示・非表示にするには、そのエンティティを切り替える: rm::Entity::SetActive (RM.h)。
// その UI の部分のないエンティティ: セッターは何もせず、ゲッターは "" / -1 を返す。
#pragma once

#include "RM.h"
#include "RMUIAbi.h"

#include <string>

namespace rm {
namespace UI {
namespace detail {
inline const RmUiApi* Get() {
    static const RmUiApi* api = Part<RmUiApi>(RM_PART_UI, RM_PART_UI_VERSION);  // エンジンのもの、ずっと
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
// このボタンへのキーのフォーカス (マウスが動くまで選ばれたものとして表示する)。
inline void Focus(Entity button) {
    if (const RmUiApi* u = detail::Get()) u->focus(Api()->ctx, button.id);
}
// トゥイーンが時間をかけてどう進むか (インスペクターの 速さの曲線 と同じ)。
enum class Ease : int { Linear = 0, In = 1, Out = 2, InOut = 3, Bounce = 4, Back = 5 };
// いまの所からそこまで、`seconds` かけて (0: すぐに)。同じ種類のほかのものは引き継ぐ。
inline void MoveTo(Entity e, Vec2 position, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->moveTo(Api()->ctx, e.id, position.x, position.y, seconds, static_cast<int>(ease));
}
inline void ScaleTo(Entity e, float scale, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->scaleTo(Api()->ctx, e.id, scale, seconds, static_cast<int>(ease));
}
inline void RotateTo(Entity e, float degrees, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->rotateTo(Api()->ctx, e.id, degrees, seconds, static_cast<int>(ease));
}
// 0: 見えない .. 1: そのまま (子も一緒に薄くなる)。
inline void FadeTo(Entity e, float alpha, float seconds, Ease ease = Ease::Out) {
    if (const RmUiApi* u = detail::Get()) u->fadeTo(Api()->ctx, e.id, alpha, seconds, static_cast<int>(ease));
}
// その 道に沿って動く: 今の所から続ける / 止める / 最初から。
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
