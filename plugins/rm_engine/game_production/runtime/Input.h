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

#include "rm_engine/game_production/runtime/GameSettings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rm::game_production {

inline constexpr int kPadControlBase = 0x100;  // InputControl::PadA
inline constexpr size_t kPadControls = 20;     // PadA .. PadRightStickY

struct InputFrame {
    std::array<uint8_t, 256> keys{};        // by virtual-key code (the mouse buttons too): 1 = held down
    std::array<float, kPadControls> pad{};  // by InputControl - PadA: buttons 0 / 1, triggers 0..1, sticks -1..1
};

// The value of one control on a frame: a key or a button 0 / 1, a trigger 0..1, a stick -1..1
// (0 for None and for a value this version does not know).
float ControlValue(const InputFrame& f, InputControl c);

struct ActionValue {
    float axis = 0;     // every binding's value x scale, added, cut to -1..1
    bool down = false;  // one binding at 0.5 or more (either way: a stick to the left with scale 1 too)
};
ActionValue EvaluateAction(const InputAction& a, const InputFrame& f);
// The action of that name (the first; names are compared as they are, case and all); null: none.
const InputAction* FindAction(std::span<const InputAction> actions, std::string_view name);

// A stick (XInput's -32768..32767 per axis) with a round dead zone: 0 inside it; outside, the length
// is scaled so the edge of the dead zone is 0 and the full push 1 (the direction kept).
std::array<float, 2> StickValue(int x, int y, int deadZone);
// A trigger (0..255) with its threshold: 0 below, then 0..1.
float TriggerValue(int v, int threshold);

// Reads the keyboard, the mouse buttons and the game pads (all four: each control takes the pad that
// pushes it most). A pad slot found empty is asked again only after a second (XInput is slow on an
// empty slot: asking all four every frame costs time).
class InputReader {
public:
    InputFrame Read();

private:
    std::array<uint64_t, 4> nextLook_{};  // GetTickCount64 when an empty slot is asked again
};

}  // namespace rm::game_production
