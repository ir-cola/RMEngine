#pragma once
// ゲーム UI while the game plays (game production's PlayService: the editor's Play and the exported game
// run it the same way). Each frame, after the scripts and the systems:
//
//   the mouse      over a button: 乗った; the left button down on it: 押した; let go on the same one: OnClick
//   the keys / pad standard in every game, nothing to set: the frontmost window (canvas) with
//                  キーとパッドで選ぶ always has a chosen button (its 最初に選ぶボタン, or the top left one) -
//                  shown from the start, hidden while the mouse is used. UIUp / UIDown / UILeft / UIRight
//                  (the arrows, the pad's cross, the left stick) step to the nearest button that way
//                  (NextButton); held, they go on stepping. UISubmit presses the chosen button. UICancel
//                  presses the window's 戻るで押すボタン, or else closes the window opened last and shows
//                  again what it hid (Show / SwitchTo by a script, or a canvas switched on).
//   then           every button's state (UiButton::state / focused: not saved) and its look eased toward
//                  the state's (UiButton::now: 変わる速さ), for the drawing; the buttons' sounds (乗った /
//                  押した: whichever service plays sounds - PlayServices::Sounds); then the OnClick hooks
//                  for the scripts of the pressed buttons' entities (PlayFrame::hooks)
//
// The keys and the pad are read only through the game settings' 入力 (game production's actions, a
// game without them: their standard ones) - never a key or a pad itself: changing the bindings (or
// another kind of controller) changes the UI's too.

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rm::game_ui {

inline constexpr char kUiServiceName[] = "rm_engine.game_ui";
inline constexpr float kRepeatDelay = 0.4f;  // a direction held this long ...
inline constexpr float kRepeatEvery = 0.12f;  // ... steps again this often

// What the actions say this frame (pure: tests give their own frames).
struct UiKeys {
    int dx = 0, dy = 0;          // a direction pressed this frame (screen Y down; the stronger way only)
    int holdDx = 0, holdDy = 0;  // the direction held now
    bool submit = false;         // UISubmit went down
    bool cancel = false;         // UICancel went down
};
UiKeys ReadUiKeys(std::span<const game_production::InputAction> actions, const game_production::InputFrame& now,
                  const game_production::InputFrame& before);

// A window shown over others (or instead of them): what 戻る closes, and what it shows again.
struct UiVisit {
    int window = 0;
    std::vector<int> hid;
};

// The focus, the mouse and the windows of one Play (kept between frames).
struct UiFocus {
    PointerState pointer;
    int window = 0;          // the window the keys worked in last frame
    int focus = 0;           // its chosen button
    bool keyboard = true;    // the choice is shown (hidden while the mouse is used)
    std::optional<Vec2> lastPointer;
    int heldDx = 0, heldDy = 0;  // a direction held: it repeats
    float heldFor = 0, repeatAt = 0;
    bool started = false;        // the windows shown at first are known (none of them is closed by 戻る)
    std::vector<int> shown;      // the windows shown last frame
    std::vector<UiVisit> visits; // windows opened since, oldest first
};

// What one frame did.
struct UiStep {
    std::vector<int> clicked;  // buttons pressed, in order (OnClick)
    std::vector<int> hovered;  // buttons that became 乗った (or pressed, or chosen) this frame: their 乗ったときの音
    int closed = 0;            // the window 戻る closed (0: none)
};

// One frame (pure: the scene's buttons get their state, a window closed by 戻る is switched off and what it
// hid switched on again).
UiStep StepUi(game_production::Scene& scene, const UiTree& tree, std::optional<Vec2> pointer, bool down, bool wentDown, bool wentUp,
              std::span<const game_production::InputAction> actions, const game_production::InputFrame* now,
              const game_production::InputFrame* before, float dt, UiFocus& state);

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
