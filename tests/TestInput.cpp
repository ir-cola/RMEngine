// アクション名による入力のテスト (ゲーム制作の runtime/Input と、スクリプトの部品
// "rm_engine.game_production.input")。1 フレームでのアクションの状態 (押している、複数の割り当ての
// 軸の足し算)、デッドゾーン、2 フレームにわたる Pressed / Released、一覧に無い名前、既定の一覧、
// 名前で保存される設定の 入力 (このバージョンが知らない入力は残す)。

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <algorithm>
#include <cmath>

using namespace rm;
using namespace rm::game_production;

namespace {

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

InputFrame Keys(std::initializer_list<int> held) {
    InputFrame f;
    for (int vk : held) f.keys[static_cast<size_t>(vk)] = 1;
    return f;
}

void Pad(InputFrame& f, InputControl c, float v) { f.pad[static_cast<size_t>(static_cast<int>(c) - kPadControlBase)] = v; }

}  // namespace

RM_TEST(Input_ControlValues) {
    InputFrame f = Keys({0x20, 0x01});
    Pad(f, InputControl::PadLeftStickX, -0.75f);
    CHECK(ControlValue(f, InputControl::Space) == 1 && ControlValue(f, InputControl::MouseLeft) == 1);
    CHECK(ControlValue(f, InputControl::Enter) == 0 && ControlValue(f, InputControl::None) == 0);
    CHECK(Near(ControlValue(f, InputControl::PadLeftStickX), -0.75f));
    CHECK(ControlValue(f, static_cast<InputControl>(0x500)) == 0);  // (このバージョンが読まない値)
}

RM_TEST(Input_AxisAddsTheBindings_DownAtHalf) {
    const std::vector<InputAction> list = DefaultInputActions();
    const InputAction* moveX = FindAction(list, "MoveX");
    CHECK(moveX != nullptr && FindAction(list, "movex") == nullptr && FindAction(list, "Nothing") == nullptr);
    if (!moveX) return;
    CHECK(EvaluateAction(*moveX, {}).axis == 0 && !EvaluateAction(*moveX, {}).down);
    CHECK(EvaluateAction(*moveX, Keys({0x25})).axis == -1 && EvaluateAction(*moveX, Keys({0x25})).down);  // ←
    CHECK(EvaluateAction(*moveX, Keys({0x27})).axis == 1);                                                // →
    CHECK(EvaluateAction(*moveX, Keys({0x25, 0x27})).axis == 0);  // 両方: 打ち消し合う
    CHECK(EvaluateAction(*moveX, Keys({0x27, 'D'})).axis == 1);   // → と D: 1 に切る
    // スティック: 倒した分だけ。半分から押している扱い (左も同じ)。
    InputFrame f;
    Pad(f, InputControl::PadLeftStickX, 0.3f);
    CHECK(Near(EvaluateAction(*moveX, f).axis, 0.3f) && !EvaluateAction(*moveX, f).down);
    Pad(f, InputControl::PadLeftStickX, -0.6f);
    CHECK(Near(EvaluateAction(*moveX, f).axis, -0.6f) && EvaluateAction(*moveX, f).down);
    // 割り当ての 向き が値を掛ける。
    const InputAction slow{"Slow", {{InputControl::Space, 0.25f}}};
    CHECK(Near(EvaluateAction(slow, Keys({0x20})).axis, 0.25f) && !EvaluateAction(slow, Keys({0x20})).down);
}

RM_TEST(Input_DefaultList) {
    const std::vector<InputAction> list = DefaultInputActions();
    for (const char* name : {"MoveX", "MoveY", "Jump", "Fire"}) CHECK(FindAction(list, name) != nullptr);
    const InputAction* jump = FindAction(list, "Jump");
    InputFrame pad;
    Pad(pad, InputControl::PadA, 1);
    CHECK(jump && EvaluateAction(*jump, Keys({0x20})).down && EvaluateAction(*jump, pad).down);
    const InputAction* moveY = FindAction(list, "MoveY");
    CHECK(moveY && EvaluateAction(*moveY, Keys({'W'})).axis == 1 && EvaluateAction(*moveY, Keys({0x28})).axis == -1);  // up = +
    // 新しいゲームの設定には、これがある。
    const GameSettings s;
    CHECK(s.input.size() == list.size());
}

RM_TEST(Input_StickDeadZoneAndTrigger) {
    CHECK(StickValue(7000, 0, 7849)[0] == 0);  // デッドゾーンの内側
    const auto full = StickValue(32767, 0, 7849);
    CHECK(Near(full[0], 1) && full[1] == 0);
    const auto half = StickValue(0, -(7849 + (32767 - 7849) / 2), 7849);
    CHECK(Near(half[1], -0.5f) && half[0] == 0);
    const auto diag = StickValue(30000, 30000, 7849);  // 丸く: 向きは保ち、長さは 1 に切る
    CHECK(Near(diag[0], diag[1]) && std::sqrt(diag[0] * diag[0] + diag[1] * diag[1]) <= 1.0001f);
    CHECK(StickValue(-32768, 0, 7849)[0] == -1);
    CHECK(TriggerValue(30, 30) == 0 && TriggerValue(255, 30) == 1 && TriggerValue(0, 30) == 0);
}

RM_TEST(Input_ThroughTheScriptsPart_PressedAndReleased) {
    ScriptWorld w;
    const std::vector<InputAction> list = DefaultInputActions();
    w.actions = &list;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    bool space = false;
    w.readInput = [&] { return space ? Keys({0x20}) : InputFrame{}; };
    const RmApi api = MakeScriptApi(w);
    const auto* in = static_cast<const RmInputApi*>(api.part(api.ctx, RM_PART_INPUT, RM_PART_INPUT_VERSION));
    CHECK(in != nullptr && api.part(api.ctx, RM_PART_INPUT, RM_PART_INPUT_VERSION2) != nullptr);
    CHECK(api.part(api.ctx, RM_PART_INPUT, RM_PART_INPUT_VERSION2 + 1) == nullptr);
    if (!in) return;
    w.ReadInput();
    CHECK(in->down(api.ctx, "Jump") == 0 && in->pressed(api.ctx, "Jump") == 0);
    space = true;
    w.ReadInput();
    CHECK(in->down(api.ctx, "Jump") == 1 && in->pressed(api.ctx, "Jump") == 1 && in->released(api.ctx, "Jump") == 0);
    w.ReadInput();
    CHECK(in->down(api.ctx, "Jump") == 1 && in->pressed(api.ctx, "Jump") == 0);  // 1 回の押下で 1 回
    space = false;
    w.ReadInput();
    CHECK(in->down(api.ctx, "Jump") == 0 && in->released(api.ctx, "Jump") == 1);
    // 一覧に無い名前: 何もせず、1 回だけ知らせる。
    CHECK(in->down(api.ctx, "Jmup") == 0 && in->axis(api.ctx, "Jmup") == 0);
    CHECK(told.find("「Jmup」") != std::string::npos && told.find("「Jmup」") == told.rfind("「Jmup」"));
    CHECK(in->down(api.ctx, nullptr) == 0);
    // 一覧がまったく無い (一覧を渡さないホスト): 何も押されていない。
    w.actions = nullptr;
    space = true;
    w.ReadInput();
    CHECK(in->down(api.ctx, "Jump") == 0);
}

RM_TEST(Input_SavedByName_AnUnknownInputKept) {
    GameSettings s;
    s.input = {{"Shoot", {{InputControl::PadRT, 1}, {InputControl::Z, -1}}}};
    json::Value saved = reflect::Save(s);
    const json::Value& b = saved.Get("input").Items()[0].Get("bindings");
    CHECK(b.Items()[0].Get("control").AsString() == "PadRT" && b.Items()[1].Get("control").AsString() == "Z");
    // 新しいバージョンの入力 ("PadPaddle1"): 知らないものとして表示し、そのまま書き戻す。
    json::Value edited = saved;
    json::Value action = saved.Get("input").Items()[0];
    json::Value bindings = json::Value::MakeArray();
    json::Value one = action.Get("bindings").Items()[0];
    one.Set("control", json::Value("PadPaddle1"));
    bindings.Push(one);
    action.Set("bindings", bindings);
    json::Value list = json::Value::MakeArray();
    list.Push(action);
    edited.Set("input", list);
    GameSettings back;
    reflect::Unknowns u;
    reflect::Warnings warnings;
    reflect::Load(back, edited, &u, &warnings);
    CHECK(back.input.size() == 1 && back.input[0].name == "Shoot" && back.input[0].bindings.size() == 1);
    const json::Value again = reflect::Save(back, &u);
    CHECK(again.Get("input").Items()[0].Get("bindings").Items()[0].Get("control").AsString() == "PadPaddle1");
}

RM_TEST(Input_MouseButtonsDropped_DefaultFireHasNoCtrl) {
    // エディターの Play: ゲームビューの外のクリックはゲームのものではない (キーはそのまま)。
    InputFrame f = Keys({0x01, 0x02, 0x20});
    DropMouseButtons(f);
    CHECK(ControlValue(f, InputControl::MouseLeft) == 0 && ControlValue(f, InputControl::MouseRight) == 0 && ControlValue(f, InputControl::Space) == 1);
    // Ctrl はエディターのもの (Ctrl+P): 既定の Fire には入れない。
    const std::vector<InputAction> list = DefaultInputActions();
    const InputAction* fire = FindAction(list, "Fire");
    CHECK(fire && std::none_of(fire->bindings.begin(), fire->bindings.end(), [](const InputBinding& b) { return b.control == InputControl::Ctrl; }));
    CHECK(fire && EvaluateAction(*fire, Keys({'Z'})).down);
}

RM_TEST(Input_KeyTappedBetweenFrames_IsDownForOneFrame) {
    // フレームが見る前に Backspace を押して離した: 今は押していないが、回数は増えた。
    KeyPresses seen;
    KeyPresses now = seen;
    ++now.count[0x08];
    InputFrame f = Keys({0x20});  // Space を押している
    AddMissedPresses(f, now, seen);
    CHECK(ControlValue(f, InputControl::Backspace) == 1 && ControlValue(f, InputControl::Space) == 1);
    CHECK(ControlValue(f, InputControl::Escape) == 0);
    // 次のフレーム: 新しく押したものは無い -> また上がった (Released が見る)。
    InputFrame next;
    AddMissedPresses(next, now, now);
    CHECK(ControlValue(next, InputControl::Backspace) == 0);
}
