#include "rm_engine/game_production/runtime/Input.h"

#include "core/reflect/Data.h"
#include "platform/Win.h"

#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

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

float ControlValue(const InputFrame& f, InputControl c, int player, int keyboardPlayer) {
    if (player < 0 || player >= kPlayers) return ControlValue(f, c);
    const int v = static_cast<int>(c);
    if (v > 0 && v < 256) return player == keyboardPlayer && f.keys[static_cast<size_t>(v)] ? 1.0f : 0.0f;
    if (v >= kPadControlBase && v < kPadControlBase + static_cast<int>(kPadControls)) {
        return f.pads[static_cast<size_t>(player)][static_cast<size_t>(v - kPadControlBase)];
    }
    return 0;
}

ActionValue EvaluateAction(const InputAction& a, const InputFrame& f, int player, int keyboardPlayer) {
    if (player < 0) return EvaluateAction(a, f);
    ActionValue out;
    float sum = 0;
    for (const InputBinding& b : a.bindings) {
        const float v = ControlValue(f, b.control, player, keyboardPlayer) * b.scale;
        sum += v;
        if (std::fabs(v) >= 0.5f) out.down = true;
    }
    out.axis = std::clamp(sum, -1.0f, 1.0f);
    return out;
}

InputControl AnyPressed(const InputFrame& now, const InputFrame& before, int player, int keyboardPlayer, float* direction) {
    if (direction) *direction = 1;
    if (player < 0 || player == keyboardPlayer) {
        for (size_t vk = 1; vk < now.keys.size(); ++vk) {
            if (now.keys[vk] && !before.keys[vk]) return static_cast<InputControl>(vk);
        }
    }
    auto look = [&](const std::array<float, kPadControls>& n, const std::array<float, kPadControls>& b) -> InputControl {
        for (size_t i = 0; i < kPadControls; ++i) {
            if (std::fabs(n[i]) >= 0.5f && std::fabs(b[i]) < 0.5f) {
                if (direction) *direction = n[i] < 0 ? -1.0f : 1.0f;
                return static_cast<InputControl>(kPadControlBase + static_cast<int>(i));
            }
        }
        return InputControl::None;
    };
    if (player >= 0 && player < kPlayers) return look(now.pads[static_cast<size_t>(player)], before.pads[static_cast<size_t>(player)]);
    if (player < 0) {
        for (int p = 0; p < kPlayers; ++p) {
            if (const InputControl c = look(now.pads[static_cast<size_t>(p)], before.pads[static_cast<size_t>(p)]); c != InputControl::None) return c;
        }
    }
    return InputControl::None;
}

std::string ControlName(int control) {
    if (const reflect::EnumDesc* d = reflect::FindEnum(typeid(InputControl))) {
        for (const reflect::EnumItem& it : d->items) {
            if (it.value == control && control != 0) return it.name;
        }
    }
    if (control <= 0) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Key0x%02X", control);
    return buf;
}

json::Value BindingsToJson(std::span<const InputAction> actions) {
    json::Value doc = json::Value::MakeObject();
    doc.Set("kind", json::Value(std::string(kBindingsKind)));
    doc.Set("format", json::Value(int64_t{1}));
    json::Value list = json::Value::MakeArray();
    for (const InputAction& a : actions) list.Push(reflect::Save(a));
    doc.Set("actions", std::move(list));
    return doc;
}

std::vector<InputAction> BindingsFromJson(std::span<const InputAction> game, const json::Value& saved) {
    std::vector<InputAction> out(game.begin(), game.end());
    if (saved.Get("kind").AsString() != kBindingsKind) return out;
    for (const json::Value& item : saved.Get("actions").Items()) {
        InputAction read;
        reflect::Load(read, item);
        for (InputAction& a : out) {
            if (a.name == read.name) a.bindings = std::move(read.bindings);
        }
    }
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

void AddMissedPresses(InputFrame& f, const KeyPresses& now, const KeyPresses& seen) {
    for (size_t vk = 1; vk < f.keys.size(); ++vk) {
        if (now.count[vk] != seen.count[vk]) f.keys[vk] = 1;
    }
}

namespace {

// Counts the key-down messages of the thread it is made on (one per thread: the first Read makes it).
class PressHook {
public:
    PressHook() : hook_(SetWindowsHookExW(WH_GETMESSAGE, &PressHook::Proc, nullptr, GetCurrentThreadId())) {}
    ~PressHook() {
        if (hook_) UnhookWindowsHookEx(hook_);
    }
    PressHook(const PressHook&) = delete;
    PressHook& operator=(const PressHook&) = delete;

    static PressHook& OfThisThread() {
        thread_local PressHook hook;
        return hook;
    }
    const KeyPresses& Presses() const { return presses_; }
    // The wheel's notches since the last call.
    float TakeWheel() { return std::exchange(wheel_, 0.0f); }

private:
    static LRESULT CALLBACK Proc(int code, WPARAM wp, LPARAM lp) {
        if (code == HC_ACTION && wp == PM_REMOVE) OfThisThread().Count(*reinterpret_cast<const MSG*>(lp));
        return CallNextHookEx(nullptr, code, wp, lp);
    }
    void Count(const MSG& m) {
        auto add = [&](WPARAM vk) {
            if (vk > 0 && vk < presses_.count.size()) ++presses_.count[vk];
        };
        switch (m.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(m.lParam & (LPARAM{1} << 30))) add(m.wParam);  // (not the repeats of a key held)
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: add(VK_LBUTTON); break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: add(VK_RBUTTON); break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK: add(VK_MBUTTON); break;
        case WM_MOUSEWHEEL: wheel_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(m.wParam)) / WHEEL_DELTA; break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK: add(GET_XBUTTON_WPARAM(m.wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2); break;
        default: break;
        }
    }

    HHOOK hook_ = nullptr;
    KeyPresses presses_;
    float wheel_ = 0;
};

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

std::array<PadState, kPlayers> PadWatcher::Read() {
    std::array<PadState, kPlayers> out;
    const uint64_t now = GetTickCount64();
    for (DWORD slot = 0; slot < static_cast<DWORD>(kPlayers); ++slot) {
        if (now < nextLook_[slot]) continue;
        XINPUT_STATE s{};
        if (XInputGetState(slot, &s) != ERROR_SUCCESS) {
            nextLook_[slot] = now + 1000;
            battery_[slot] = -1;
            continue;
        }
        nextLook_[slot] = 0;
        PadState& p = out[slot];
        p.connected = true;
        p.values = PadValues(s.Gamepad);
        if (now >= batteryAt_[slot]) {  // (asked now and then: it changes slowly)
            batteryAt_[slot] = now + 5000;
            XINPUT_BATTERY_INFORMATION b{};
            battery_[slot] = XInputGetBatteryInformation(slot, BATTERY_DEVTYPE_GAMEPAD, &b) == ERROR_SUCCESS &&
                                     (b.BatteryType == BATTERY_TYPE_ALKALINE || b.BatteryType == BATTERY_TYPE_NIMH)
                                 ? static_cast<int>(b.BatteryLevel)
                                 : -1;
        }
        p.battery = battery_[slot];
    }
    return out;
}

void InputReader::Forget() {
    seen_ = PressHook::OfThisThread().Presses();
    PressHook::OfThisThread().TakeWheel();
}

InputReader::~InputReader() { StopVibration(); }

void InputReader::Vibrate(int slot, float low, float high, double seconds) {
    if (slot < 0 || slot >= kPlayers) return;
    // (NaN: still - a NaN would become any number on its way to a WORD; a day at most)
    if (!std::isfinite(low)) low = 0;
    if (!std::isfinite(high)) high = 0;
    seconds = std::isfinite(seconds) ? std::min(seconds, 86400.0) : 0.0;
    XINPUT_VIBRATION v{};
    v.wLeftMotorSpeed = static_cast<WORD>(std::clamp(low, 0.0f, 1.0f) * 65535.0f);
    v.wRightMotorSpeed = static_cast<WORD>(std::clamp(high, 0.0f, 1.0f) * 65535.0f);
    if (XInputSetState(static_cast<DWORD>(slot), &v) != ERROR_SUCCESS) return;
    const bool still = v.wLeftMotorSpeed == 0 && v.wRightMotorSpeed == 0;
    shakeUntil_[static_cast<size_t>(slot)] = still ? 0 : GetTickCount64() + static_cast<uint64_t>(std::max(0.0, seconds) * 1000.0);
}

void InputReader::StopVibration() {
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (shakeUntil_[slot] == 0) continue;
        XINPUT_VIBRATION v{};
        XInputSetState(slot, &v);
        shakeUntil_[slot] = 0;
    }
}

InputFrame InputReader::Read() {
    InputFrame f;
    const KeyPresses& presses = PressHook::OfThisThread().Presses();
    const KeyPresses seen = std::exchange(seen_, presses);
    const float wheel = PressHook::OfThisThread().TakeWheel();
    const uint64_t now = GetTickCount64();
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (shakeUntil_[slot] != 0 && now >= shakeUntil_[slot]) Vibrate(static_cast<int>(slot), 0, 0, 0);
    }
    if (!InFront()) {
        StopVibration();  // (the player went to another app)
        return f;
    }
    f.wheel = wheel;
    for (int vk = 1; vk < 256; ++vk) f.keys[static_cast<size_t>(vk)] = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
    // GetAsyncKeyState reads the mouse's physical buttons: swapped (left-handed) -> the logical ones
    // (the messages are already logical).
    if (GetSystemMetrics(SM_SWAPBUTTON)) std::swap(f.keys[VK_LBUTTON], f.keys[VK_RBUTTON]);
    AddMissedPresses(f, presses, seen);
    for (DWORD slot = 0; slot < 4; ++slot) {
        if (now < nextLook_[slot]) continue;
        XINPUT_STATE s{};
        if (XInputGetState(slot, &s) != ERROR_SUCCESS) {
            nextLook_[slot] = now + 1000;
            continue;
        }
        nextLook_[slot] = 0;
        const auto v = PadValues(s.Gamepad);
        f.pads[slot] = v;
        f.padConnected[slot] = 1;
        for (size_t i = 0; i < kPadControls; ++i) {
            if (std::fabs(v[i]) > std::fabs(f.pad[i])) f.pad[i] = v[i];
        }
    }
    return f;
}

}  // namespace rm::game_production
