#pragma once
// Input by action name (design TestEngine NewEngineBluePrint 5: a script never holds a key code - so a
// game pad reaches every script too). The RUNTIME part: the editor's Play and the exported game read the
// input the same way, with this code.
//
//   InputReader::Read()   once a frame (ScriptRunner::Frame): the keys and the mouse buttons
//                         (GetAsyncKeyState) and the game pads (XInput, in Windows) - only while a
//                         window of this program is in front (a key typed into another app never moves
//                         the game)
//   InputFrame            what was read (plain data: tests make their own)
//   EvaluateAction        an action of the game settings' 入力 on a frame: its axis, and whether it is down
//
// Pressed / Released compare this frame with the last (ScriptWorld keeps both).

#include "base/Json.h"
#include "rm_engine/game_production/runtime/GameSettings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::game_production {

inline constexpr int kPadControlBase = 0x100;  // InputControl::PadA
inline constexpr size_t kPadControls = 20;     // PadA .. PadRightStickY

inline constexpr int kPlayers = 4;  // game pads (XInput's slots) = players 0..3

struct InputFrame {
    std::array<uint8_t, 256> keys{};        // by virtual-key code (the mouse buttons too): 1 = held down
    std::array<float, kPadControls> pad{};  // by InputControl - PadA: buttons 0 / 1, triggers 0..1, sticks -1..1
                                            // (every pad: each control the one that pushes it most)
    std::array<std::array<float, kPadControls>, kPlayers> pads{};  // ... each pad on its own (by slot)
    std::array<uint8_t, kPlayers> padConnected{};                   // 1: a pad is in that slot
    float wheel = 0;  // the mouse wheel's notches since the last frame (+: away from you)
};

// The value of one control on a frame: a key or a button 0 / 1, a trigger 0..1, a stick -1..1
// (0 for None and for a value this version does not know).
float ControlValue(const InputFrame& f, InputControl c);

struct ActionValue {
    float axis = 0;     // every binding's value x scale, added, cut to -1..1
    bool down = false;  // one binding at 0.5 or more (either way: a stick to the left with scale 1 too)
};
ActionValue EvaluateAction(const InputAction& a, const InputFrame& f);
// The same for one player (0..3): pad controls from that player's pad only; keys and mouse buttons only
// when the player is `keyboardPlayer`. player < 0: everyone (as above).
float ControlValue(const InputFrame& f, InputControl c, int player, int keyboardPlayer);
ActionValue EvaluateAction(const InputAction& a, const InputFrame& f, int player, int keyboardPlayer);
// What went down between `before` and `now` for `player` (< 0: anyone): a key or mouse button, a pad's
// button or trigger, a stick pushed past half way (*direction -1 when pushed to the - side). None when
// nothing did. Keys first (low codes first), then the pad's controls in their order.
InputControl AnyPressed(const InputFrame& now, const InputFrame& before, int player, int keyboardPlayer, float* direction);
// A control's name as the game settings save it ("Space", "PadA"; a key the list does not name: "Key0x5B").
std::string ControlName(int control);

// --- The player's bindings (rm::Input::SetBinding ... SaveBindings): input.json on the player's PC ------
//   { "kind": "rm_engine.input_bindings", "format": 1, "actions": [ { "name": "Jump", "bindings": [...] } ] }
// Only actions the game has are taken (by name); an action the file does not have keeps the game's.
inline constexpr char kBindingsKind[] = "rm_engine.input_bindings";
json::Value BindingsToJson(std::span<const InputAction> actions);
std::vector<InputAction> BindingsFromJson(std::span<const InputAction> game, const json::Value& saved);
// The action of that name (the first; names are compared as they are, case and all); null: none.
const InputAction* FindAction(std::span<const InputAction> actions, std::string_view name);
// ... or, when the game settings have none of that name (a game made before it was standard), the
// standard one of DefaultInputActions; null when neither has it.
const InputAction* ActionOrStandard(std::span<const InputAction> actions, std::string_view name);

// The game UI's actions (in every new game's 入力; the UI reads only these - never a pad or a key itself).
// Up / Down / Left / Right: the axis at 0.5 or more (a stick pushed that way).
inline constexpr char kUiUp[] = "UIUp";
inline constexpr char kUiDown[] = "UIDown";
inline constexpr char kUiLeft[] = "UILeft";
inline constexpr char kUiRight[] = "UIRight";
inline constexpr char kUiSubmit[] = "UISubmit";  // press the chosen button
inline constexpr char kUiCancel[] = "UICancel";  // go back (close the window opened last)

// The mouse buttons let go of in a frame (the editor's Play: a click on the 階層 or the インスペクター is
// not the game's - only one over the game view is).
void DropMouseButtons(InputFrame& f);

// A stick (XInput's -32768..32767 per axis) with a round dead zone: 0 inside it; outside, the length
// is scaled so the edge of the dead zone is 0 and the full push 1 (the direction kept).
std::array<float, 2> StickValue(int x, int y, int deadZone);
// A trigger (0..255) with its threshold: 0 below, then 0..1.
float TriggerValue(int v, int threshold);

// The key-down messages this program's thread took, counted per virtual key (the mouse buttons too).
// A key pressed and let go between two frames (a quick tap, a computer that types for you, a slow
// frame) is not down when the frame looks: its count still went up.
struct KeyPresses {
    std::array<uint32_t, 256> count{};
};
// The keys pressed since `seen` are down in this frame (up again in the next, if let go: Pressed and
// Released both see it once).
void AddMissedPresses(InputFrame& f, const KeyPresses& now, const KeyPresses& seen);

// Reads the keyboard, the mouse buttons and the game pads (all four: each control takes the pad that
// pushes it most). A pad slot found empty is asked again only after a second (XInput is slow on an
// empty slot: asking all four every frame costs time). The keys held now (GetAsyncKeyState) and those
// pressed since the last frame (KeyPresses, from a WH_GETMESSAGE hook on the thread that reads: the
// editor's UI thread, the exported game's only one).
class InputReader {
public:
    ~InputReader();
    InputFrame Read();
    // Shakes the pad in `slot` (0..3; low: the left heavy motor, high: the right light one, 0..1) for
    // `seconds` - Read stops it when they are over. StopVibration: every pad still now.
    void Vibrate(int slot, float low, float high, double seconds);
    void StopVibration();
    // The presses counted so far are not the game's (the editor's typing between two Plays): the next
    // Read sees only those made after this.
    void Forget();

private:
    std::array<uint64_t, 4> nextLook_{};  // GetTickCount64 when an empty slot is asked again
    std::array<uint64_t, 4> shakeUntil_{};  // GetTickCount64 when a pad's shaking stops (0: not shaking)
    KeyPresses seen_;                     // the counts at the last Read
};

// Every pad as it is now, for the debug panel (not the game's reader: nothing of the game's input is
// taken). An empty slot is asked again only after a second, as InputReader does.
struct PadState {
    bool connected = false;
    std::array<float, kPadControls> values{};  // by InputControl - PadA (as InputFrame::pad)
    int battery = -1;                          // 0 empty .. 3 full; -1: wired or not known
};
class PadWatcher {
public:
    std::array<PadState, kPlayers> Read();

private:
    std::array<uint64_t, kPlayers> nextLook_{};
    std::array<uint64_t, kPlayers> batteryAt_{};
    std::array<int, kPlayers> battery_{-1, -1, -1, -1};
};

}  // namespace rm::game_production
