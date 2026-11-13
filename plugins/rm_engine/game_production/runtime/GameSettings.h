#pragma once
// Settings of the game being made (saved in the project; read by the exported game later).
// Written once here: saving, loading, the inspector and undo all come from these lines (rmgen).

#include "core/reflect/Reflect.h"

#include <string>
#include <vector>

namespace rm::game_production {

RM_ENUM(Label="表示のしかた")
enum class ScreenMode {
    RM_ITEM(Label="ウィンドウ") Windowed,
    RM_ITEM(Label="フルスクリーン") Fullscreen,
    RM_ITEM(Label="枠なしの全画面") Borderless,
};

RM_STRUCT(Label="画面")
struct DisplaySettings {
    RM_PROPERTY(Edit, Label="幅", Min=320, Max=7680) int width = 1280;
    RM_PROPERTY(Edit, Label="高さ", Min=240, Max=4320) int height = 720;
    RM_PROPERTY(Edit, Label="表示のしかた") ScreenMode mode = ScreenMode::Windowed;
    RM_PROPERTY(Edit, Label="垂直同期", Tooltip="画面の書き換えに合わせて描きます (ちらつきを防ぐ)") bool vsync = true;
};

// When scripts built while the game plays are used (design BluePrint 20-c).
RM_ENUM(Label="Play 中のスクリプト")
enum class ScriptReload {
    RM_ITEM(Label="Stop で反映 (Play 中は前のまま)") AtStop,
    RM_ITEM(Label="すぐ入れ替える (値は引き継ぎ、OnReloaded を呼ぶ)") AtOnce,
};

// One line of the scripts' run order: smaller numbers run first; a type not listed is 0 (then by name).
RM_STRUCT(Label="スクリプトの実行順")
struct ScriptOrderEntry {
    RM_PROPERTY(Edit, Label="スクリプト", Tooltip="スクリプトの型の名前 (C++ の名前。例: PlayerController)") std::string type;
    RM_PROPERTY(Edit, Label="順番", Tooltip="小さいほど先に動きます。一覧に無いスクリプトは 0 です") int order = 0;
};

// --- Input: actions by name (scripts ask rm::Input::Down("Jump"), never a key) ------------------------
// What can be pressed or moved. A key's or a mouse button's value is its Windows virtual-key code
// (GetAsyncKeyState); a game pad's starts at 0x100 (runtime/Input.cpp reads them with XInput). Saved
// by name ("Space", "PadA" ...).
RM_ENUM(Label="入力")
enum class InputControl {
    RM_ITEM(Label="(なし)") None = 0,
    RM_ITEM(Label="マウスの左ボタン") MouseLeft = 0x01,
    RM_ITEM(Label="マウスの右ボタン") MouseRight = 0x02,
    RM_ITEM(Label="マウスの中ボタン") MouseMiddle = 0x04,
    RM_ITEM(Label="マウスの戻るボタン") MouseX1 = 0x05,
    RM_ITEM(Label="マウスの進むボタン") MouseX2 = 0x06,
    Backspace = 0x08,
    Tab = 0x09,
    Enter = 0x0D,
    Shift = 0x10,
    Ctrl = 0x11,
    Alt = 0x12,
    Escape = 0x1B,
    Space = 0x20,
    PageUp = 0x21,
    PageDown = 0x22,
    End = 0x23,
    Home = 0x24,
    RM_ITEM(Label="← (矢印)") Left = 0x25,
    RM_ITEM(Label="↑ (矢印)") Up = 0x26,
    RM_ITEM(Label="→ (矢印)") Right = 0x27,
    RM_ITEM(Label="↓ (矢印)") Down = 0x28,
    Insert = 0x2D,
    Delete = 0x2E,
    RM_ITEM(Label="0") D0 = 0x30,
    RM_ITEM(Label="1") D1,
    RM_ITEM(Label="2") D2,
    RM_ITEM(Label="3") D3,
    RM_ITEM(Label="4") D4,
    RM_ITEM(Label="5") D5,
    RM_ITEM(Label="6") D6,
    RM_ITEM(Label="7") D7,
    RM_ITEM(Label="8") D8,
    RM_ITEM(Label="9") D9,
    A = 0x41, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    RM_ITEM(Label="パッドの A") PadA = 0x100,
    RM_ITEM(Label="パッドの B") PadB,
    RM_ITEM(Label="パッドの X") PadX,
    RM_ITEM(Label="パッドの Y") PadY,
    RM_ITEM(Label="パッドの LB") PadLB,
    RM_ITEM(Label="パッドの RB") PadRB,
    RM_ITEM(Label="パッドの BACK") PadBack,
    RM_ITEM(Label="パッドの START") PadStart,
    RM_ITEM(Label="パッドの左スティックの押し込み") PadLeftThumb,
    RM_ITEM(Label="パッドの右スティックの押し込み") PadRightThumb,
    RM_ITEM(Label="パッドの十字キーの上") PadUp,
    RM_ITEM(Label="パッドの十字キーの下") PadDown,
    RM_ITEM(Label="パッドの十字キーの左") PadLeft,
    RM_ITEM(Label="パッドの十字キーの右") PadRight,
    RM_ITEM(Label="パッドの LT (0〜1)") PadLT,
    RM_ITEM(Label="パッドの RT (0〜1)") PadRT,
    RM_ITEM(Label="パッドの左スティックの左右 (-1〜1)") PadLeftStickX,
    RM_ITEM(Label="パッドの左スティックの上下 (-1〜1)") PadLeftStickY,
    RM_ITEM(Label="パッドの右スティックの左右 (-1〜1)") PadRightStickX,
    RM_ITEM(Label="パッドの右スティックの上下 (-1〜1)") PadRightStickY,
};

// One thing that drives an action. Its value (a button: 0 or 1; a stick: -1..1) times `scale`.
RM_STRUCT(Label="割り当て")
struct InputBinding {
    RM_PROPERTY(Edit, Label="入力") InputControl control = InputControl::None;
    RM_PROPERTY(Edit, Label="向き", Min=-1, Max=1, Tooltip="軸の値に掛けます。左・下に動かすキーは -1、右・上は 1") float scale = 1;
};

// An action: a name the scripts use, and what drives it. rm::Input::Down / Pressed / Released: one of
// the bindings at 0.5 or more (either way); rm::Input::Axis: all of them added, cut to -1..1.
RM_STRUCT(Label="入力のアクション")
struct InputAction {
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトは rm::Input::Down や rm::Input::Axis にこの名前を渡して使います (大文字と小文字は別)") std::string name;
    RM_PROPERTY(Edit, Label="割り当て", Tooltip="キー・マウスのボタン・ゲームパッドをいくつでも") std::vector<InputBinding> bindings;
};

// The list a new game starts with (MoveX / MoveY: arrows, WASD, the left stick and the pad's cross;
// Jump; Fire).
std::vector<InputAction> DefaultInputActions();

// --- Global variables: values of the whole game (they stay when the scene is switched; rm::Save writes
// them). Scripts use them as Global::score (a header written from this list: GlobalsHeader).
RM_ENUM(Label="変数の型")
enum class GlobalType {
    RM_ITEM(Label="真偽 (bool)") Bool,
    RM_ITEM(Label="整数 (int)") Int,
    RM_ITEM(Label="小数 (float)") Float,
    RM_ITEM(Label="文字 (std::string)") String,
    RM_ITEM(Label="Vec2") Vec2,
    RM_ITEM(Label="Vec3") Vec3,
};

RM_STRUCT(Label="グローバル変数")
struct GlobalVariable {
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトからは Global::名前 で使います (英字・数字・_。数字から始めない)") std::string name;
    RM_PROPERTY(Edit, Label="型") GlobalType type = GlobalType::Int;
    RM_PROPERTY(Edit, Label="初めの値", Tooltip="型に合わせて書きます: 真偽は true / false、数は 0 や 1.5、文字はそのまま、Vec2 / Vec3 は 1, 2, 3。空なら 0 (空の文字)") std::string initial;
};

// How loud the game is (each 0..1 - more makes it louder than the sounds are). The 音 plugin uses
// them (rm_engine/game_x_audio); scripts may change them while the game plays (rm::Audio::SetVolume).
RM_STRUCT(Label="音量")
struct AudioVolumes {
    RM_PROPERTY(Edit, Label="全体", Min=0, Max=2, Step=0.01, Tooltip="ゲームの音すべてに掛かります") float master = 1;
    RM_PROPERTY(Edit, Label="BGM", Min=0, Max=2, Step=0.01, Tooltip="BGM として鳴らす音 (音源の「鳴らす先」が BGM・rm::Audio::PlayMusic)") float music = 1;
    RM_PROPERTY(Edit, Label="効果音", Min=0, Max=2, Step=0.01, Tooltip="BGM でない音すべて") float effects = 1;
};

RM_STRUCT(Label="ゲームの設定")
struct GameSettings {
    RM_PROPERTY(Edit, Label="ゲームの名前", Tooltip="書き出した exe の名前とウィンドウのタイトルに使います") std::string title = "My Game";
    RM_PROPERTY(Edit, Label="目標 FPS", Min=1, Max=240) int targetFps = 60;
    RM_PROPERTY(Edit, Label="物理の刻み (秒)", Min=0.001, Max=0.1, Step=0.001) float fixedTimestep = 0.02f;
    RM_PROPERTY(Edit, Label="重力") Vec3 gravity{0, -9.81f, 0};
    RM_PROPERTY(Edit, Label="背景の色", Min=0, Max=1) Color clearColor{0.10f, 0.10f, 0.12f, 1};
    RM_PROPERTY(Edit, Label="画面") DisplaySettings display;
    RM_PROPERTY(Edit, Label="タグ") std::vector<std::string> tags;
    // References (saved as ids). The icon is used once the game is exported.
    RM_PROPERTY(Edit, Label="アイコン", Tooltip="書き出したゲームのウィンドウとタスクバーの絵 (画像のアセット)", AssetKind="image") AssetRef icon;
    RM_PROPERTY(Edit, Label="最初のカメラ", Tooltip="ゲームビューと Play はこのエンティティのカメラから見ます (カメラの部品を付けたエンティティを選びます)") EntityRef startCamera;
    // The game's space (a scene view's SceneViewInfo::space: "3d", "2d" - game production does not know
    // them). Chosen in the scene panel's strip when there is more than one; empty: the default one.
    // Not in the inspector (the choices come from plugins); kept as written when its plugin is missing.
    RM_PROPERTY() std::string space;
    // Which scripts run before which (type by type; the same type: in the order they were attached).
    RM_PROPERTY(Edit, Label="スクリプトの実行順", Tooltip="プレイヤーが動いてからカメラが追う、のように型の順番を決めます。小さいほど先") std::vector<ScriptOrderEntry> scriptOrder;
    RM_PROPERTY(Edit, Label="Play 中のビルド", Tooltip="Play 中にスクリプトのビルドが終わったとき。すぐ入れ替えると、欄の値は名前で引き継ぎ、BeginPlay は呼ばずに OnReloaded を呼びます (RM_TRANSIENT の欄は既定の値に戻ります)") ScriptReload scriptReload = ScriptReload::AtStop;
    // What the scripts read by name (rm::Input::Down("Jump")): the same in the editor's Play and the game.
    RM_PROPERTY(Edit, Label="入力", Tooltip="スクリプトはキーではなくアクションの名前で入力を読みます (ゲームパッドもそのまま使えます)") std::vector<InputAction> input = DefaultInputActions();
    // Values of the whole game: back to these at every Play (and when it stops); rm::Save writes them.
    RM_PROPERTY(Edit, Label="グローバル変数", Tooltip="ゲーム全体の値 (シーンを切り替えても残る)。スクリプトからは Global::名前。rm::Save::Write で保存できます") std::vector<GlobalVariable> globals;
    RM_PROPERTY(Edit, Label="音量", Tooltip="全体・BGM・効果音の大きさ。Play 中に変えるとすぐ聞こえ方が変わります") AudioVolumes volume;
};

}  // namespace rm::game_production
