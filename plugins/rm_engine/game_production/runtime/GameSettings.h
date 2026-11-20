#pragma once
// 作っているゲームの設定 (プロジェクトに保存する。書き出したゲームが後で読む)。
// ここに 1 回書くだけ: 保存・読み込み・インスペクター・Undo はこの行たちから作る (rmgen)。

#include "core/reflect/Reflect.h"

#include <string>
#include <vector>

namespace rm::game_production {

RM_ENUM(Label="表示のしかた")
enum class ScreenMode {
    RM_ITEM(Label="ウィンドウで表示") Windowed,
    RM_ITEM(Label="フルスクリーン") Fullscreen,
    RM_ITEM(Label="枠なしの全画面") Borderless,
};

RM_STRUCT(Label="画面")
struct DisplaySettings {
    RM_PROPERTY(Edit, Label="幅", Min=320, Max=7680) int width = 1280;
    RM_PROPERTY(Edit, Label="高さ", Min=240, Max=4320) int height = 720;
    RM_PROPERTY(Edit, Label="表示のしかた") ScreenMode mode = ScreenMode::Windowed;
    RM_PROPERTY(Edit, Label="垂直同期", Tooltip="画面の書き換えに合わせて描きます (ちらつきを防ぐ)") bool vsync = true;
    // ゲームの UI が避けるべき所 (テレビの端、ノッチ): 画面の DIPs で、各辺から。
    // UI ビューの目安。キャンバスはこの中に自分を並べてもよい (キャンバス「セーフエリアの中に」)。
    RM_PROPERTY(Edit, Label="セーフエリア (左・上)", Tooltip="画面の左と上の縁から内側へ、UI を置かない幅 (DIP)。UI のビューに補助線が出ます", Min=0, Max=4000) Vec2 safeAreaTopLeft{0, 0};
    RM_PROPERTY(Edit, Label="セーフエリア (右・下)", Tooltip="画面の右と下の縁から内側へ、UI を置かない幅 (DIP)", Min=0, Max=4000) Vec2 safeAreaBottomRight{0, 0};
};

// ゲームが動いている間にビルドしたスクリプトを使うとき (設計 BluePrint 20-c)。
RM_ENUM(Label="Play 中のスクリプト")
enum class ScriptReload {
    RM_ITEM(Label="Stop で反映 (Play 中は前のまま)") AtStop,
    RM_ITEM(Label="すぐ入れ替える (値は引き継ぎ、OnReloaded を呼ぶ)") AtOnce,
};

// スクリプトの実行順の 1 行: 小さい番号が先に動く。載っていない型は 0 (その中は名前順)。
RM_STRUCT(Label="スクリプトの実行順")
struct ScriptOrderEntry {
    RM_PROPERTY(Edit, Label="スクリプト", Tooltip="スクリプトの型の名前 (C++ の名前。例: PlayerController)") std::string type;
    RM_PROPERTY(Edit, Label="順番", Tooltip="小さいほど先に動きます。一覧に無いスクリプトは 0 です") int order = 0;
};

// --- Input: 名前つきのアクション (スクリプトは rm::Input::Down("Jump") と聞き、キーは決して使わない) ---
// 押したり動かしたりできるもの。キーやマウスのボタンの値は Windows の仮想キーコード
// (GetAsyncKeyState)。ゲームパッドは 0x100 から (runtime/Input.cpp が XInput で読む)。名前
// ("Space"、"PadA" ...) で保存する。
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

// アクションを動かす 1 つのもの。その値 (ボタン: 0 か 1、スティック: -1..1) に `scale` をかける。
RM_STRUCT(Label="割り当て")
struct InputBinding {
    RM_PROPERTY(Edit, Label="入力") InputControl control = InputControl::None;
    RM_PROPERTY(Edit, Label="向き", Min=-1, Max=1, Tooltip="軸の値に掛けます。左・下に動かすキーは -1、右・上は 1") float scale = 1;
};

// アクション: スクリプトが使う名前と、それを動かすもの。rm::Input::Down / Pressed / Released:
// 割り当てのどれかが 0.5 以上 (どちら向きでも)。rm::Input::Axis: すべてを足して -1..1 に切る。
RM_STRUCT(Label="入力のアクション")
struct InputAction {
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトは rm::Input::Down や rm::Input::Axis にこの名前を渡して使います (大文字と小文字は別)") std::string name;
    RM_PROPERTY(Edit, Label="割り当て", Tooltip="キー・マウスのボタン・ゲームパッドをいくつでも") std::vector<InputBinding> bindings;
};

// 新しいゲームが最初に持つ一覧 (MoveX / MoveY: 矢印、WASD、左スティック、パッドの十字。
// Jump。Fire。そしてゲーム UI の UIUp / UIDown / UILeft / UIRight / UISubmit / UICancel - Input.h)。
std::vector<InputAction> DefaultInputActions();

// --- グローバル変数: ゲーム全体の値 (シーンを切り替えても残る。rm::Save がそれを書く)。
// スクリプトは Global::score として使う (この一覧から書かれるヘッダー: GlobalsHeader)。
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

// ゲームの音の大きさ (それぞれ 0..1 - 大きくすると、音そのものより大きくなる)。音 プラグイン
// (rm_engine/game_x_audio) が使う。スクリプトはゲームの動いている間に変えてよい (rm::Audio::SetVolume)。
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
    RM_PROPERTY(Edit, Label="背景の色", Min=0, Max=1) Color clearColor{0.10f, 0.10f, 0.12f, 1};
    RM_PROPERTY(Edit, Label="画面") DisplaySettings display;
    RM_PROPERTY(Edit, Label="タグ", Tooltip="エンティティに付ける名前の一覧 (スクリプトの rm::FindWithTag で探す)。エンティティのインスペクターの「タグ」に書きます") std::vector<std::string> tags;
    RM_PROPERTY(Edit, Label="レイヤー", Tooltip="エンティティの分け方の一覧。上から 1, 2, 3 … 番 (空のレイヤーは 0 番の既定)。スクリプトは rm::LayerIndex で番号を読みます") std::vector<std::string> layers;
    // 参照 (id で保存する)。アイコンはゲームを書き出したときに使う。
    RM_PROPERTY(Edit, Label="アイコン", Tooltip="書き出したゲームのウィンドウとタスクバーの絵 (画像のアセット)", AssetKind="image") AssetRef icon;
    RM_PROPERTY(Edit, Label="最初のカメラ", Tooltip="ゲームビューと Play はこのエンティティのカメラから見ます (カメラの部品を付けたエンティティを選びます)") EntityRef startCamera;
    // ゲームの空間 (シーンビューの SceneViewInfo::space: "3d"、"2d" - ゲーム制作はそれを知らない)。
    // 2 つ以上あるときは、シーンパネルの帯で選ぶ。空: 既定のもの。
    // インスペクターには出さない (選択肢はプラグインから来る)。そのプラグインがないときも、書かれた
    // まま保持する。
    RM_PROPERTY() std::string space;
    // どのスクリプトがどれより先に動くか (型ごと。同じ型は、つけた順)。
    RM_PROPERTY(Edit, Label="スクリプトの実行順", Tooltip="プレイヤーが動いてからカメラが追う、のように型の順番を決めます。小さいほど先") std::vector<ScriptOrderEntry> scriptOrder;
    RM_PROPERTY(Edit, Label="Play 中のビルド", Tooltip="Play 中にスクリプトのビルドが終わったとき。すぐ入れ替えると、欄の値は名前で引き継ぎ、BeginPlay は呼ばずに OnReloaded を呼びます (RM_TRANSIENT の欄は既定の値に戻ります)") ScriptReload scriptReload = ScriptReload::AtStop;
    // スクリプトが名前で読むもの (rm::Input::Down("Jump")): エディターの Play とゲームで同じ。
    RM_PROPERTY(Edit, Label="入力", Tooltip="スクリプトはキーではなくアクションの名前で入力を読みます (ゲームパッドもそのまま使えます)") std::vector<InputAction> input = DefaultInputActions();
    // 複数のプレイヤー (rm::Input::Axis("MoveX", player)): それぞれが自分のスロットのゲームパッドを
    // 持つ。キーボードとマウスはこのプレイヤーに行く (1 = 1P)。
    RM_PROPERTY(Edit, Label="キーボードとマウスのプレイヤー", Tooltip="何人かで遊ぶとき、キーボードとマウスを使う人 (1 = 1P)。ほかの人はゲームパッドだけ。プレイヤーを指定しない読み方 (rm::Input::Axis(\"MoveX\")) は今までどおり全部を読みます", Min=1, Max=4) int keyboardPlayer = 1;
    // ゲーム全体の値: Play のたびに (そして止まったときに) ここに戻る。rm::Save がこれを書く。
    RM_PROPERTY(Edit, Label="グローバル変数", Tooltip="ゲーム全体の値 (シーンを切り替えても残る)。スクリプトからは Global::名前。rm::Save::Write で保存できます") std::vector<GlobalVariable> globals;
    RM_PROPERTY(Edit, Label="音量", Tooltip="全体・BGM・効果音の大きさ。Play 中に変えるとすぐ聞こえ方が変わります") AudioVolumes volume;
};

}  // namespace rm::game_production
