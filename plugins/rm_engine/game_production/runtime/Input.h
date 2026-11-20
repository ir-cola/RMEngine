#pragma once
// アクション名での入力 (設計 TestEngine NewEngineBluePrint 5: スクリプトはキーコードを持たない -
// だからゲームパッドもすべてのスクリプトに届く)。ランタイム側: エディターの Play と書き出した
// ゲームは、このコードで同じように入力を読む。
//
//   InputReader::Read()   1 フレームに 1 回 (ScriptRunner::Frame): キーとマウスのボタン
//                         (GetAsyncKeyState) とゲームパッド (XInput、Windows で) - このプログラムの
//                         ウィンドウが前にあるときだけ (他のアプリに打ったキーでゲームが動くことはない)
//   InputFrame            読んだもの (ただのデータ: テストは自分で作る)
//   EvaluateAction        ゲーム設定の 入力 のアクションを 1 フレームで評価する: その軸と、押されているか
//
// Pressed / Released は、このフレームと前のフレームを比べる (ScriptWorld が両方を持つ)。

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

inline constexpr int kPlayers = 4;  // ゲームパッド (XInput のスロット) = プレイヤー 0..3

struct InputFrame {
    std::array<uint8_t, 256> keys{};        // 仮想キーコードごと (マウスのボタンも): 1 = 押されている
    std::array<float, kPadControls> pad{};  // InputControl ごと - PadA: ボタンは 0 / 1、トリガーは 0..1、スティックは -1..1
                                            // (全パッド: 各コントロールは、一番強く押しているパッドのもの)
    std::array<std::array<float, kPadControls>, kPlayers> pads{};  // ... 各パッドを別々に (スロットごと)
    std::array<uint8_t, kPlayers> padConnected{};                   // 1: そのスロットにパッドがある
    float wheel = 0;  // 前のフレームからのマウスホイールのノッチ数 (+: 奥へ)
};

// フレームでの 1 つのコントロールの値: キーやボタンは 0 / 1、トリガーは 0..1、スティックは -1..1
// (None と、このバージョンが知らない値は 0)。
float ControlValue(const InputFrame& f, InputControl c);

struct ActionValue {
    float axis = 0;     // すべての割り当ての値 x scale を足して、-1..1 に切ったもの
    bool down = false;  // 割り当てのどれかが 0.5 以上 (どちら向きでも: scale 1 でスティックを左に倒したときも)
};
ActionValue EvaluateAction(const InputAction& a, const InputFrame& f);
// 1 人のプレイヤー (0..3) 用の同じもの: パッドのコントロールはそのプレイヤーのパッドだけから、
// キーとマウスのボタンは、そのプレイヤーが `keyboardPlayer` のときだけ。player < 0: 全員 (上と同じ)。
float ControlValue(const InputFrame& f, InputControl c, int player, int keyboardPlayer);
ActionValue EvaluateAction(const InputAction& a, const InputFrame& f, int player, int keyboardPlayer);
// `before` から `now` の間に `player` (< 0: 誰でも) に押されたもの: キーかマウスのボタン、パッドの
// ボタンかトリガー、半分を超えて倒されたスティック (- 側に倒したときは *direction が -1)。
// 何もなければ None。キーが先 (小さいコードから)、その後にパッドのコントロールをその順で。
InputControl AnyPressed(const InputFrame& now, const InputFrame& before, int player, int keyboardPlayer, float* direction);
// ゲーム設定が保存するときのコントロールの名前 ("Space"、"PadA"。一覧に名前がないキー: "Key0x5B")。
std::string ControlName(int control);

// --- プレイヤーの割り当て (rm::Input::SetBinding ... SaveBindings): プレイヤーの PC の input.json ---
//   { "kind": "rm_engine.input_bindings", "format": 1, "actions": [ { "name": "Jump", "bindings": [...] } ] }
// ゲームにあるアクションだけを (名前で) 取り込む。ファイルにないアクションは、ゲームのものを保つ。
inline constexpr char kBindingsKind[] = "rm_engine.input_bindings";
json::Value BindingsToJson(std::span<const InputAction> actions);
std::vector<InputAction> BindingsFromJson(std::span<const InputAction> game, const json::Value& saved);
// その名前のアクション (最初のもの。名前は大文字小文字も含めそのまま比べる)。なければ null。
const InputAction* FindAction(std::span<const InputAction> actions, std::string_view name);
// ... または、ゲーム設定にその名前のものがないとき (標準になる前に作ったゲーム)、DefaultInputActions の
// 標準のもの。どちらにもなければ null。
const InputAction* ActionOrStandard(std::span<const InputAction> actions, std::string_view name);

// ゲーム UI のアクション (どの新しいゲームの 入力 にもある。UI はこれだけを読み - パッドやキーを
// 自分では決して読まない)。Up / Down / Left / Right: 軸が 0.5 以上 (スティックをその向きに倒した)。
inline constexpr char kUiUp[] = "UIUp";
inline constexpr char kUiDown[] = "UIDown";
inline constexpr char kUiLeft[] = "UILeft";
inline constexpr char kUiRight[] = "UIRight";
inline constexpr char kUiSubmit[] = "UISubmit";  // 選んでいるボタンを押す
inline constexpr char kUiCancel[] = "UICancel";  // 戻る (最後に開いたウィンドウを閉じる)

// 1 フレームの間に離されたマウスのボタン (エディターの Play: 階層やインスペクターへのクリックは
// ゲームのものではない - ゲームビューの上のものだけ)。
void DropMouseButtons(InputFrame& f);

// 円形のデッドゾーンつきのスティック (XInput の軸ごとに -32768..32767): その内側は 0。外側は、
// デッドゾーンの縁が 0、いっぱいに倒したときが 1 になるよう長さを直す (向きは保つ)。
std::array<float, 2> StickValue(int x, int y, int deadZone);
// しきい値つきのトリガー (0..255): それより下は 0、その上は 0..1。
float TriggerValue(int v, int threshold);

// このプログラムのスレッドが受け取ったキー押下メッセージを、仮想キーごとに数えたもの (マウスの
// ボタンも)。2 つのフレームの間に押して離したキー (ちょんと叩く、勝手に打つコンピューター、遅い
// フレーム) は、フレームが見たときには押されていない: それでも、この数は増えている。
struct KeyPresses {
    std::array<uint32_t, 256> count{};
};
// `seen` より後に押されたキーは、このフレームで押されている (離されていれば次のフレームで上がる:
// Pressed と Released の両方が 1 回ずつ見る)。
void AddMissedPresses(InputFrame& f, const KeyPresses& now, const KeyPresses& seen);

// キーボード、マウスのボタン、ゲームパッドを読む (4 つすべて: 各コントロールは一番強く押している
// パッドのもの)。空だったパッドのスロットは、1 秒後にしかもう一度聞かない (XInput は空の
// スロットが遅い: 4 つを毎フレーム聞くと時間がかかる)。今押されているキー (GetAsyncKeyState) と、
// 前のフレームから押されたもの (KeyPresses。読むスレッド - エディターの UI スレッド、書き出した
// ゲームでは唯一のスレッド - の WH_GETMESSAGE フックから)。
class InputReader {
public:
    ~InputReader();
    InputFrame Read();
    // `slot` (0..3) のパッドを `seconds` 秒振動させる (low: 左の重いモーター、high: 右の軽いモーター、
    // 0..1) - 終わると Read が止める。StopVibration: まだ動いているすべてのパッドを止める。
    void Vibrate(int slot, float low, float high, double seconds);
    void StopVibration();
    // ここまでに数えた押下はゲームのものではない (2 回の Play の間のエディターでの入力): 次の Read は、
    // これより後のものだけを見る。
    void Forget();

private:
    std::array<uint64_t, 4> nextLook_{};  // 空のスロットをもう一度聞く時刻の GetTickCount64
    std::array<uint64_t, 4> shakeUntil_{};  // パッドの振動が止まる時刻の GetTickCount64 (0: 振動していない)
    KeyPresses seen_;                     // 最後の Read のときの数
};

// 今のすべてのパッド。デバッグパネル用 (ゲームの読み取りではない: ゲームの入力は何も取らない)。
// 空のスロットは、InputReader と同じく、1 秒後にしかもう一度聞かない。
struct PadState {
    bool connected = false;
    std::array<float, kPadControls> values{};  // InputControl ごと - PadA (InputFrame::pad と同じ)
    int battery = -1;                          // 0 空 .. 3 満タン、-1: 有線、または不明
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
