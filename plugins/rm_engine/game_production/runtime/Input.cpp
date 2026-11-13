#include "rm_engine/game_production/runtime/Input.h"

#include "platform/Win.h"

#include <Xinput.h>

#include <algorithm>
#include <cmath>

// XInput 1.4 (in Windows 8 and later). A static library carries this into the exported game's link too.
#pragma comment(lib, "xinput.lib")

namespace rm::game_production {

std::vector<InputAction> DefaultInputActions() {
    using C = InputControl;
    auto axis = [](const char* name, std::initializer_list<InputBinding> b) { return InputAction{name, b}; };
    return {
        axis("MoveX", {{C::Left, -1}, {C::Right, 1}, {C::A, -1}, {C::D, 1}, {C::PadLeftStickX, 1}, {C::PadLeft, -1}, {C::PadRight, 1}}),
        axis("MoveY", {{C::Down, -1}, {C::Up, 1}, {C::S, -1}, {C::W, 1}, {C::PadLeftStickY, 1}, {C::PadDown, -1}, {C::PadUp, 1}}),
        axis("Jump", {{C::Space, 1}, {C::PadA, 1}}),
        axis("Fire", {{C::MouseLeft, 1}, {C::Z, 1}, {C::PadX, 1}, {C::PadRT, 1}}),  // (not Ctrl: the editor's Ctrl+P would fire)
        // The game UI's (rm_engine/game_ui): choose a button (the arrows, the pad's cross and left stick),
        // press it, go back. Every game has them; their bindings may be changed like any action's.
        axis(kUiUp, {{C::Up, 1}, {C::PadUp, 1}, {C::PadLeftStickY, 1}}),
        axis(kUiDown, {{C::Down, 1}, {C::PadDown, 1}, {C::PadLeftStickY, -1}}),
        axis(kUiLeft, {{C::Left, 1}, {C::PadLeft, 1}, {C::PadLeftStickX, -1}}),
        axis(kUiRight, {{C::Right, 1}, {C::PadRight, 1}, {C::PadLeftStickX, 1}}),
        axis(kUiSubmit, {{C::Enter, 1}, {C::Space, 1}, {C::PadA, 1}}),
        axis(kUiCancel, {{C::Escape, 1}, {C::PadB, 1}}),
    };
}

const InputAction* ActionOrStandard(std::span<const InputAction> actions, std::string_view name) {
    if (const InputAction* a = FindAction(actions, name)) return a;
    static const std::vector<InputAction> standard = DefaultInputActions();
    return FindAction(standard, name);
}

float ControlValue(const InputFrame& f, InputControl c) {
    const int v = static_cast<int>(c);
    if (v > 0 && v < 256) return f.keys[static_cast<size_t>(v)] ? 1.0f : 0.0f;
    if (v >= kPadControlBase && v < kPadControlBase + static_cast<int>(kPadControls)) return f.pad[static_cast<size_t>(v - kPadControlBase)];
    return 0;
}

ActionValue EvaluateAction(const InputAction& a, const InputFrame& f) {
    ActionValue out;
    float sum = 0;
    for (const InputBinding& b : a.bindings) {
        const float v = ControlValue(f, b.control) * b.scale;
        sum += v;
        if (std::fabs(v) >= 0.5f) out.down = true;
    }
    out.axis = std::clamp(sum, -1.0f, 1.0f);
    return out;
}

const InputAction* FindAction(std::span<const InputAction> actions, std::string_view name) {
    for (const InputAction& a : actions) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

void DropMouseButtons(InputFrame& f) {
    for (const InputControl b : {InputControl::MouseLeft, InputControl::MouseRight, InputControl::MouseMiddle, InputControl::MouseX1, InputControl::MouseX2}) {
        f.keys[static_cast<size_t>(b)] = 0;
    }
}

std::array<float, 2> StickValue(int x, int y, int deadZone) {
    const float fx = static_cast<float>(std::clamp(x, -32767, 32767));
    const float fy = static_cast<float>(std::clamp(y, -32767, 32767));
    const float length = std::sqrt(fx * fx + fy * fy);
    const float dz = static_cast<float>(std::clamp(deadZone, 0, 32766));
    if (length <= dz) return {0, 0};
    const float scaled = std::min(1.0f, (length - dz) / (32767.0f - dz));
    return {std::clamp(fx / length * scaled, -1.0f, 1.0f), std::clamp(fy / length * scaled, -1.0f, 1.0f)};
}

float TriggerValue(int v, int threshold) {
    const int t = std::clamp(threshold, 0, 254);
    if (v <= t) return 0;
    return std::min(1.0f, static_cast<float>(v - t) / static_cast<float>(255 - t));
}

namespace {

// A window of this process is in front (another app's keys are not the game's).
bool InFront() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// The pad's values in InputControl order (PadA ..).
std::array<float, kPadControls> PadValues(const XINPUT_GAMEPAD& g) {
    static constexpr WORD kButtons[] = {XINPUT_GAMEPAD_A,          XINPUT_GAMEPAD_B,           XINPUT_GAMEPAD_X,         XINPUT_GAMEPAD_Y,
                                        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_BACK,   XINPUT_GAMEPAD_START,
                                        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,  XINPUT_GAMEPAD_DPAD_UP,   XINPUT_GAMEPAD_DPAD_DOWN,
                                        XINPUT_GAMEPAD_DPAD_LEFT,  XINPUT_GAMEPAD_DPAD_RIGHT};
    std::array<float, kPadControls> v{};
    size_t i = 0;
    for (const WORD b : kButtons) v[i++] = (g.wButtons & b) ? 1.0f : 0.0f;
    v[i++] = TriggerValue(g.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    v[i++] = TriggerValue(g.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
    const auto left = StickValue(g.sThumbLX, g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    const auto right = StickValue(g.sThumbRX, g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    v[i++] = left[0];
    v[i++] = left[1];
    v[i++] = right[0];
    v[i++] = right[1];
    return v;
}

}  // namespace

InputFrame InputReader::Read() {
    InputFrame f;
    if (!InFront()) return f;
    for (int vk = 1; vk < 256; ++vk) f.keys[static_cast<size_t>(vk)] = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
    // GetAsyncKeyState reads the mouse's physical buttons: swapped (left-handed) -> the logical ones.
    if (GetSystemMetrics(SM_SWAPBUTTON)) std::swap(f.keys[VK_LBUTTON], f.keys[VK_RBUTTON]);
    const uint64_t now = GetTickCount64();
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (now < nextLook_[slot]) continue;
        XINPUT_STATE s{};
        if (XInputGetState(slot, &s) != ERROR_SUCCESS) {
            nextLook_[slot] = now + 1000;
            continue;
        }
        nextLook_[slot] = 0;
        const auto v = PadValues(s.Gamepad);
        for (size_t i = 0; i < kPadControls; ++i) {
            if (std::fabs(v[i]) > std::fabs(f.pad[i])) f.pad[i] = v[i];
        }
    }
    return f;
}

}  // namespace rm::game_production
