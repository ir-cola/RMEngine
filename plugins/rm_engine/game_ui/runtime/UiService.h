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
//   pressed        its 押したときの音, then its 押したときの動作 in order (RunActions: windows, the scene,
//                  a variable, a sound, the end of the game), then the scripts' OnClick
//   then           every button's state (UiButton::state / focused: not saved) and its look eased toward
//                  the state's (UiButton::now: 変わる速さ), for the drawing; the buttons' sounds (乗った /
//                  押した: whichever service plays sounds - PlayServices::Sounds); then the OnClick hooks
//                  for the scripts of the pressed buttons' entities (PlayFrame::hooks)
//
// The keys and the pad are read only through the game settings' 入力 (game production's actions, a
// game without them: their standard ones) - never a key or a pad itself: changing the bindings (or
// another kind of controller) changes the UI's too.

#include "rm_engine/game_production/runtime/Globals.h"
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
    bool scriptChose = false;  // a script chose `focus` since the last frame (kept when its window comes in front)
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

// --- The windows (canvases): shared by the buttons' actions and the scripts' rm::UI -------------
// A window: a canvas not inside another canvas (one inside is a part of its window).
bool IsWindow(const game_production::Scene& scene, int entity);
// Only `window` shown of the windows (every other one hidden; what is inside it stays as it is).
void SwitchToWindow(game_production::Scene& scene, int window);
// 戻る: `window` hidden and what it hid shown again, when it was opened over others since (state.visits);
// false (nothing changes) when it was not - a window of the start is never closed so.
bool CloseWindow(game_production::Scene& scene, UiFocus& state, int window);

// --- 押したときの動作 -------------------------------------------------------------------------
// What the actions reach: the game's (the service gives them from PlayFrame::world and the host), a
// test its own. One left empty: that kind of action does nothing (told).
struct UiActionHost {
    game_production::GlobalStore* globals = nullptr;
    std::function<bool(const AssetRef& scene)> loadScene;           // asked; switched between frames (false: not a scene of the game)
    std::function<void()> quit;                                    // asked; the game ends after this frame
    std::function<void(const AssetRef& sound, float volume)> playSound;
    std::function<void(int level, std::string_view text)> log;
};
// A global variable changed as `a` says (変え方, 数, 下限 / 上限). False (*why) when it cannot be so
// (a text, a Vec2 - or 足す on ON / OFF).
bool ChangeVariable(game_production::GlobalValue& v, const UiAction& a, std::string* why);
// The actions of the button `button` (in the window `window`), in order.
void RunActions(game_production::Scene& scene, int button, int window, const UiButton& b, UiFocus& state, const UiActionHost& host);

class UiService final : public game_production::PlayService {
public:
    explicit UiService(const game_production::PlayHost& host) : host_(host) {}
    void Frame(const game_production::PlayFrame& f) override;
    void SceneSwitched() override { state_ = {}; }
    // A script chose a button (rm::UI::Focus): shown as chosen until the mouse moves.
    void Focus(int entity) {
        state_.focus = entity;
        state_.keyboard = true;
        state_.scriptChose = true;
    }

private:
    const game_production::PlayHost& host_;
    UiFocus state_;
};

std::unique_ptr<game_production::PlayService> CreateUiService(const game_production::PlayHost& host);

// The game's screen and its safe area, from the game settings.
UiScreen ScreenOf(const game_production::GameSettings& settings, Vec2 size);

}  // namespace rm::game_ui
