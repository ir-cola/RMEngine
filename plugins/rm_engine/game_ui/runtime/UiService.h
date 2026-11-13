#pragma once
// ゲーム UI while the game plays (game production's PlayService: the editor's Play and the exported game
// run it the same way). Each frame, after the scripts and the systems:
//
//   the mouse      over a button: 乗った; the left button down on it: 押した; let go on the same one: OnClick
//   the keys / pad the frontmost canvas with キーとパッドで選ぶ: its 上下左右 actions move the focus between
//                  its buttons (MoveFocus), 決定 presses the focused one, 戻る its 戻るで押すボタン
//   then           every button's state (UiButton::state / focused: not saved) for the drawing, and the
//                  OnClick hooks for the scripts of the pressed buttons' entities (PlayFrame::hooks)

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"

#include <memory>
#include <optional>

namespace rm::game_ui {

inline constexpr char kUiServiceName[] = "rm_engine.game_ui";

// What the keys did this frame (pure: tests give their own).
struct UiKeys {
    int dx = 0, dy = 0;     // a step of the focus (screen Y down)
    bool submit = false;    // 決定 went down
    bool cancel = false;    // 戻る went down
};
// The canvas's actions on two frames (an action not in the game settings: the usual keys - Enter / Space /
// Escape / the arrows and the pad's - so a game made before the UI's actions works too).
UiKeys ReadUiKeys(const UiCanvas& canvas, std::span<const game_production::InputAction> actions, const game_production::InputFrame& now,
                  const game_production::InputFrame& before);

// The focus and the mouse of one Play (kept between frames).
struct UiFocus {
    PointerState pointer;
    int focus = 0;           // the button chosen by the keys (0: none yet)
    bool keyboard = false;   // the focus is shown (the keys were used since the mouse last moved)
    std::optional<Vec2> lastPointer;
};
// One frame (pure: the scene's buttons get their state; the pressed buttons are returned, in order).
std::vector<int> StepUi(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown,
                        bool wentUp, std::span<const game_production::InputAction> actions, const game_production::InputFrame* now,
                        const game_production::InputFrame* before, UiFocus& state);

class UiService final : public game_production::PlayService {
public:
    explicit UiService(const game_production::PlayHost& host) : host_(host) {}
    void Frame(const game_production::PlayFrame& f) override;
    void SceneSwitched() override { state_ = {}; }
    // A script chose a button (rm::UI::Focus): shown as chosen until the mouse moves.
    void Focus(int entity) {
        state_.focus = entity;
        state_.keyboard = true;
    }

private:
    const game_production::PlayHost& host_;
    UiFocus state_;
};

std::unique_ptr<game_production::PlayService> CreateUiService(const game_production::PlayHost& host);

// The game's screen and its safe area, from the game settings.
UiScreen ScreenOf(const game_production::GameSettings& settings, Vec2 size);

}  // namespace rm::game_ui
