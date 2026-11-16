#pragma once
// ゲーム UI - the parts of a game's UI. Every UI thing is an entity (design: this plugin's README part in
// docs/ARCHITECTURE.md): a キャンバス entity is one UI window (タイトル, HUD, ポーズ ...), and the UI
// entities below it are laid out in it. So the 階層, the inspector, saving, undo, prefabs and the scripts'
// Spawn / Destroy work for the UI as for anything else (rule 1).
//
//   キャンバス          a UI window: its 基準の解像度 and how it fits the screen; shown = the entity active
//   UI の配置          where a UI thing is in its parent: fixed to a corner / the middle / stretched (アンカー),
//                     its place, size, turn and order among its siblings
//   文字 / 画像 / ボタン / バー   what it shows and does
//
// Units: DIPs of the canvas's 基準の解像度, the screen's Y going down (as the screen; a 2D game's plane has
// Y up - the UI is not on the plane). Pure values - laid out by UiLayout.h, drawn by UiRenderer.h.

#include "core/reflect/Reflect.h"

#include <string>
#include <vector>

namespace rm::game_ui {

RM_ENUM(Label="大きさの合わせ方")
enum class UiScaleMode {
    RM_ITEM(Label="小さい方に合わせる (全部見える)") Fit,
    RM_ITEM(Label="大きい方に合わせる (画面を埋める)") Fill,
    RM_ITEM(Label="幅に合わせる") Width,
    RM_ITEM(Label="高さに合わせる") Height,
    RM_ITEM(Label="合わせない (DIP のまま)") None,
};

// How a UI thing is moved now while the game plays, on top of its UI の配置 (not saved): 出る・消える動き,
// 道に沿って動く and the scripts' ScaleTo / FadeTo (UiService writes it each frame; the layout uses it).
// Its children move, turn, grow and fade with it.
struct UiLive {
    Vec2 offset{0, 0};  // moved by (its parent's units)
    float scale = 1;    // about its 中心
    float turn = 0;     // degrees, clockwise
    float alpha = 1;    // 0: unseen .. 1
    bool operator==(const UiLive&) const = default;
};

RM_STRUCT(Label="キャンバス (UI の画面)")
struct UiCanvas {
    RM_PROPERTY(Edit, Label="基準の解像度", Tooltip="この大きさの画面で UI を作ります。実際の画面の大きさには「大きさの合わせ方」で合わせます", Min=1, Max=16384)
    Vec2 reference{1920, 1080};
    RM_PROPERTY(Edit, Label="大きさの合わせ方") UiScaleMode scale = UiScaleMode::Fit;
    RM_PROPERTY(Edit, Label="手前の順", Tooltip="キャンバスどうしで、大きいほど手前に描き、ボタンも先に押されます", Min=-100000, Max=100000) int order = 0;
    RM_PROPERTY(Edit, Label="セーフエリアの中に", Tooltip="ゲームの設定の「画面」のセーフエリアを除いた所に UI を並べます") bool safeArea = true;
    RM_PROPERTY(Edit, Label="キーとパッドで選ぶ", Tooltip="十字キー・左スティック・矢印キーでボタンを選び、決定で押し、戻るでウィンドウを戻ります (表に出ているいちばん手前のキャンバスだけ。割り当てはゲームの設定の「入力」の UIUp / UIDown / UILeft / UIRight / UISubmit / UICancel)") bool navigation = true;
    RM_PROPERTY(Edit, Label="最初に選ぶボタン", Tooltip="このウィンドウが出たときに選んでいるボタン。無ければ左上のボタン") EntityRef firstButton;
    RM_PROPERTY(Edit, Label="戻るで押すボタン", Tooltip="「戻る」でこのボタンを押します。無ければ、このウィンドウを閉じて前のウィンドウに戻ります") EntityRef cancelButton;
    RM_TRANSIENT() UiLive live;  // the window moved now (its 出る・消える動き)
};

RM_ENUM(Label="固定する場所")
enum class UiAnchor {
    RM_ITEM(Label="左上") TopLeft,
    RM_ITEM(Label="上") Top,
    RM_ITEM(Label="右上") TopRight,
    RM_ITEM(Label="左") Left,
    RM_ITEM(Label="真ん中") Center,
    RM_ITEM(Label="右") Right,
    RM_ITEM(Label="左下") BottomLeft,
    RM_ITEM(Label="下") Bottom,
    RM_ITEM(Label="右下") BottomRight,
    RM_ITEM(Label="横に伸ばす (上)") StretchTop,
    RM_ITEM(Label="横に伸ばす (真ん中)") StretchMiddle,
    RM_ITEM(Label="横に伸ばす (下)") StretchBottom,
    RM_ITEM(Label="縦に伸ばす (左)") StretchLeft,
    RM_ITEM(Label="縦に伸ばす (真ん中)") StretchCenter,
    RM_ITEM(Label="縦に伸ばす (右)") StretchRight,
    RM_ITEM(Label="全体に伸ばす") Stretch,
};

RM_STRUCT(Label="UI の配置")
struct UiRect {
    RM_PROPERTY(Edit, Label="固定する場所 (アンカー)", Tooltip="親 (キャンバスか親の UI) のどこに付くか。伸ばすと、親の大きさが変わると一緒に伸びます")
    UiAnchor anchor = UiAnchor::Center;
    RM_PROPERTY(Edit, Label="位置", Tooltip="固定する場所からのずれ (基準の解像度の DIP)。右と下が +") Vec2 position{0, 0};
    RM_PROPERTY(Edit, Label="幅と高さ", Tooltip="幅と高さ。伸ばす向きでは、親の幅 (高さ) より大きい (+) / 小さい (-) 分") Vec2 size{240, 64};
    RM_PROPERTY(Edit, Label="回す中心", Tooltip="位置と回転の中心 (0〜1。左上が 0, 0、真ん中が 0.5, 0.5)", Min=0, Max=1) Vec2 pivot{0.5f, 0.5f};
    RM_PROPERTY(Edit, Label="回転 (度)", Tooltip="時計回りが +。子も一緒に回ります", Min=-100000, Max=100000) float rotation = 0;
    RM_PROPERTY(Edit, Label="手前の順", Tooltip="兄弟の中で大きいほど手前に描きます (同じなら階層で後ろの物が手前)", Min=-100000, Max=100000) int order = 0;
    RM_TRANSIENT() UiLive live;  // moved now while the game plays
};

// --- How it looks, more (UI-5): 背景・枠・影・光・ぼかし・立体 -----------------------------------------
// One struct for each kind (never one set of numbers meaning other things for each kind: spec §2-7).
// Each draws nothing until it is used (a thing without them looks as before, to the bit).
RM_STRUCT(Label="背景")
struct UiBackground {
    RM_PROPERTY(Edit, Label="使う", Tooltip="画像が無くても、この色の (角の丸い) 四角を描きます。ボタンや下敷きが画像なしで作れます") bool enabled = true;
    RM_PROPERTY(Edit, Label="色", Min=0, Max=1, ShowIf="enabled") Color color{0.16f, 0.17f, 0.20f, 1};
};
RM_STRUCT(Label="枠")
struct UiBorder {
    RM_PROPERTY(Edit, Label="使う") bool enabled = false;
    RM_PROPERTY(Edit, Label="太さ", Tooltip="箱の内側に描きます (DIP)。ボタンの「乗ったときの縁」はこの枠と混ざります", Min=0, Max=64, ShowIf="enabled") float width = 2;
    RM_PROPERTY(Edit, Label="色", Min=0, Max=1, ShowIf="enabled") Color color{1, 1, 1, 0.9f};
};
RM_STRUCT(Label="影")
struct UiShadow {
    RM_PROPERTY(Edit, Label="使う") bool enabled = false;
    RM_PROPERTY(Edit, Label="ずれ", Tooltip="右と下が + (DIP)", Min=-500, Max=500, ShowIf="enabled") Vec2 offset{4, 6};
    RM_PROPERTY(Edit, Label="ぼかし", Tooltip="0 でくっきりした影 (DIP)", Min=0, Max=200, ShowIf="enabled") float blur = 8;
    RM_PROPERTY(Edit, Label="色", Min=0, Max=1, ShowIf="enabled") Color color{0, 0, 0, 0.5f};
};
RM_STRUCT(Label="発光")
struct UiGlow {
    RM_PROPERTY(Edit, Label="使う") bool enabled = false;
    RM_PROPERTY(Edit, Label="広がり", Tooltip="形の周りに光がにじむ幅 (DIP)", Min=0, Max=200, ShowIf="enabled") float radius = 12;
    RM_PROPERTY(Edit, Label="色", Tooltip="明るさを足します (暗い色ほど弱い)", Min=0, Max=1, ShowIf="enabled") Color color{1, 0.85f, 0.35f, 0.9f};
};
RM_STRUCT(Label="ぼかし")
struct UiBlur {
    RM_PROPERTY(Edit, Label="使う") bool enabled = false;
    RM_PROPERTY(Edit, Label="強さ", Tooltip="中身 (背景・画像・バー・文字) をぼかす幅 (DIP)", Min=0, Max=200, ShowIf="enabled") float radius = 4;
};
RM_STRUCT(Label="立体")
struct UiBevel {
    RM_PROPERTY(Edit, Label="使う") bool enabled = false;
    RM_PROPERTY(Edit, Label="幅", Tooltip="縁の明るい側と暗い側の幅 (DIP)", Min=0, Max=64, ShowIf="enabled") float width = 3;
    RM_PROPERTY(Edit, Label="へこませる", Tooltip="明るい側と暗い側を入れ替えて、へこんで見せる", ShowIf="enabled") bool inset = false;
    RM_PROPERTY(Edit, Label="押すとへこむ", Tooltip="ボタンを押している間だけ、へこんで見せる", ShowIf="enabled") bool pressInset = true;
    RM_PROPERTY(Edit, Label="明るい側", Min=0, Max=1, ShowIf="enabled") Color highlight{1, 1, 1, 0.45f};
    RM_PROPERTY(Edit, Label="暗い側", Min=0, Max=1, ShowIf="enabled") Color shade{0, 0, 0, 0.35f};
};

RM_STRUCT(Label="見た目 (UI)")
struct UiStyle {
    RM_PROPERTY(Edit, Label="角の丸さ", Tooltip="背景・枠・立体の角 (DIP)。画像の形はそのまま", Min=0, Max=1000) float rounding = 8;
    RM_PROPERTY(Edit, Label="背景") UiBackground background;
    RM_PROPERTY(Edit, Label="枠") UiBorder border;
    RM_PROPERTY(Edit, Label="影", Tooltip="背景・画像・バー・文字の形の影") UiShadow shadow;
    RM_PROPERTY(Edit, Label="発光", Tooltip="背景・画像・バー・文字の形の周りに光") UiGlow glow;
    RM_PROPERTY(Edit, Label="ぼかし") UiBlur blur;
    RM_PROPERTY(Edit, Label="立体", Tooltip="縁に明るい側と暗い側を付けて、浮き出して (へこんで) 見せる") UiBevel bevel;
};

// --- Motion (UI-4) ------------------------------------------------------------------------------------
RM_ENUM(Label="速さの曲線")
enum class UiEase {
    RM_ITEM(Label="一定") Linear,
    RM_ITEM(Label="ゆっくり始まる") In,
    RM_ITEM(Label="ゆっくり終わる") Out,
    RM_ITEM(Label="ゆっくり始まって終わる") InOut,
    RM_ITEM(Label="はねる") Bounce,
    RM_ITEM(Label="行き過ぎて戻る") Back,
};

RM_ENUM(Label="すべる向き")
enum class UiSlide {
    RM_ITEM(Label="すべらない") None,
    RM_ITEM(Label="左") Left,
    RM_ITEM(Label="右") Right,
    RM_ITEM(Label="上") Up,
    RM_ITEM(Label="下") Down,
};

// One way of appearing (出るとき: from these values to the thing as it is) or of going (消えるとき: from it
// as it is to these values).
RM_STRUCT(Label="出る・消える動き")
struct UiTransition {
    RM_PROPERTY(Edit, Label="使う") bool enabled = true;
    RM_PROPERTY(Edit, Label="長さ (秒)", Min=0, Max=10, Step=0.05, ShowIf="enabled") float duration = 0.25f;
    RM_PROPERTY(Edit, Label="待ち (秒)", Tooltip="動き出すまで (並んだ物を少しずつずらして出すとき)", Min=0, Max=10, Step=0.05, ShowIf="enabled") float delay = 0;
    RM_PROPERTY(Edit, Label="速さの曲線", ShowIf="enabled") UiEase ease = UiEase::Out;
    RM_PROPERTY(Edit, Label="フェード", Tooltip="透明から出る / 透明になって消える", ShowIf="enabled") bool fade = true;
    RM_PROPERTY(Edit, Label="すべる向き", Tooltip="出るときはその向きから来て、消えるときはその向きへ去ります", ShowIf="enabled") UiSlide slide = UiSlide::None;
    RM_PROPERTY(Edit, Label="すべる長さ", Tooltip="親の単位 (基準の解像度の DIP)", Min=0, Max=100000, ShowIf="enabled&slide=Left|Right|Up|Down") float distance = 80;
    RM_PROPERTY(Edit, Label="大きさ", Tooltip="この大きさから出る / この大きさになって消える (1 でそのまま。0.8 で少し小さく、0 で点から)", Min=0, Max=10, Step=0.05, ShowIf="enabled")
    float scale = 1;
    RM_PROPERTY(Edit, Label="回る (度)", Tooltip="この角度から回って出る / 回りながら消える (時計回りが +)", Min=-3600, Max=3600, ShowIf="enabled") float turn = 0;
};

RM_STRUCT(Label="出る・消える動き (UI)")
struct UiMotion {
    RM_PROPERTY(Edit, Label="出るとき", Tooltip="有効になったとき (ボタンの動作・スクリプトの Show・SetActive)") UiTransition show;
    RM_PROPERTY(Edit, Label="消えるとき", Tooltip="有効を外されたとき。動き終わるまで見えています (ボタンは押せません)") UiTransition hide;
    RM_PROPERTY(Edit, Label="ゲームの始めにも", Tooltip="ゲーム (シーン) の始めから出ている物も、出るときの動きで出します") bool atStart = true;
    RM_TRANSIENT() int phase = 0;      // UiMotionPhase (the UI service)
    RM_TRANSIENT() float time = 0;     // seconds since it began
    RM_TRANSIENT() bool seen = false;  // shown last frame
};
// What a UiMotion is doing (UiMotion::phase).
enum class UiMotionPhase : int { Still = 0, Showing = 1, Hiding = 2 };

RM_ENUM(Label="道の走り方")
enum class UiPathLoop {
    RM_ITEM(Label="1 回") Once,
    RM_ITEM(Label="くり返す") Loop,
    RM_ITEM(Label="往復") PingPong,
};

RM_STRUCT(Label="道に沿って動く (UI)")
struct UiPath {
    RM_PROPERTY(Edit, Label="点", Tooltip="道の点。この物の位置からのずれ (親の単位)。UI タブでこの物を選ぶと点が出て、ドラッグで動かせます")
    std::vector<Vec2> points{{0, 0}, {200, 0}, {200, -120}};
    RM_PROPERTY(Edit, Label="なめらか", Tooltip="点を曲線でつなぐ (外すと直線)") bool smooth = true;
    RM_PROPERTY(Edit, Label="輪にする", Tooltip="最後の点から最初の点へもつなぐ") bool closed = false;
    RM_PROPERTY(Edit, Label="速さ", Tooltip="1 秒に進む長さ (親の単位)", Min=0, Max=100000) float speed = 120;
    RM_PROPERTY(Edit, Label="走り方") UiPathLoop loop = UiPathLoop::Loop;
    RM_PROPERTY(Edit, Label="速さの曲線", Tooltip="1 回・往復の片道ごとに") UiEase ease = UiEase::Linear;
    RM_PROPERTY(Edit, Label="向きを合わせる", Tooltip="進む向きに回します (右向きが 0 度)") bool orient = false;
    RM_PROPERTY(Edit, Label="始めから動く", Tooltip="外すとスクリプトの rm::UI::PlayPath で動き出します") bool playOnStart = true;
    RM_TRANSIENT() float travelled = 0;  // along the path (the UI service)
    RM_TRANSIENT() bool playing = false;
    RM_TRANSIENT() bool backward = false;  // 往復: on the way back
    RM_TRANSIENT() bool started = false;
};

RM_ENUM(Label="横の揃え")
enum class UiAlignX {
    RM_ITEM(Label="左") Left,
    RM_ITEM(Label="真ん中") Center,
    RM_ITEM(Label="右") Right,
};

RM_ENUM(Label="縦の揃え")
enum class UiAlignY {
    RM_ITEM(Label="上") Top,
    RM_ITEM(Label="真ん中") Middle,
    RM_ITEM(Label="下") Bottom,
};

RM_STRUCT(Label="文字 (UI)")
struct UiText {
    RM_PROPERTY(Edit, Label="文字", Tooltip="出す文字。改行もそのまま出ます") std::string text = "文字";
    RM_PROPERTY(Edit, Label="フォント", Tooltip="Windows に入っているフォントの名前 (Yu Gothic UI・Meiryo UI・Yu Mincho など)。下の「フォントのファイル」があればそちらを使います")
    std::string font = "Yu Gothic UI";
    RM_PROPERTY(Edit, Label="フォントのファイル", Tooltip="Assets の .ttf / .otf。書き出したゲームに入るので、Windows に無いフォントも出ます", AssetKind="font")
    AssetRef fontFile;
    RM_PROPERTY(Edit, Label="文字の大きさ", Tooltip="文字の高さ (基準の解像度の DIP)", Min=1, Max=1000) float size = 36;
    RM_PROPERTY(Edit, Label="太字") bool bold = false;
    RM_PROPERTY(Edit, Label="色", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="横の揃え") UiAlignX alignX = UiAlignX::Center;
    RM_PROPERTY(Edit, Label="縦の揃え") UiAlignY alignY = UiAlignY::Middle;
    RM_PROPERTY(Edit, Label="折り返す", Tooltip="枠の幅で次の行へ (外すと 1 行のまま、はみ出します)") bool wrap = true;
    RM_PROPERTY(Edit, Label="縁取りの太さ", Tooltip="0 なら縁取りなし (DIP)", Min=0, Max=32) float outline = 0;
    RM_PROPERTY(Edit, Label="縁取りの色", Min=0, Max=1) Color outlineColor{0, 0, 0, 1};
    RM_PROPERTY(Edit, Label="影のずれ", Tooltip="0, 0 なら影なし (右と下が +)", Min=-64, Max=64) Vec2 shadow{0, 0};
    RM_PROPERTY(Edit, Label="影の色", Min=0, Max=1) Color shadowColor{0, 0, 0, 0.5f};
};

RM_STRUCT(Label="画像 (UI)")
struct UiImage {
    RM_PROPERTY(Edit, Label="画像", Tooltip="画像のアセット。無いときは色の四角", AssetKind="image") AssetRef image;
    RM_PROPERTY(Edit, Label="色", Tooltip="画像の色に掛けます (白でそのまま)。透明度も効きます", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="9 スライス (左・上)", Tooltip="画像の左と上の縁の幅 (画像のピクセル)。縁は伸ばさず、真ん中だけを伸ばします (枠やボタンの絵)", Min=0, Max=16384)
    Vec2 sliceTopLeft{0, 0};
    RM_PROPERTY(Edit, Label="9 スライス (右・下)", Tooltip="画像の右と下の縁の幅 (画像のピクセル)", Min=0, Max=16384) Vec2 sliceBottomRight{0, 0};
    RM_PROPERTY(Edit, Label="縁の大きさ", Tooltip="9 スライスの縁を描く大きさ (1 で画像のピクセル = 1 DIP)", Min=0.01, Max=100) float sliceScale = 1;
};

// The state a button is in this frame (written by the UI service while the game plays; not saved).
enum class UiButtonState : int { Normal = 0, Hover = 1, Pressed = 2, Disabled = 3 };

RM_ENUM(Label="ボタンの色の変え方")
enum class UiButtonColorMode {
    RM_ITEM(Label="掛ける") Multiply,
    RM_ITEM(Label="置き換える") Replace,
};

// How a button looks now, on its way to its state's look (UiLayout: ButtonTarget / EaseLook). The colours
// go two ways at once (spec §6-4): multiplied by `tint`, then moved toward `replace` by `replaceAmount` -
// what is replaced differs (the picture, the bar's back, its fill), so one colour would not do. The
// outline is the state's, mixed in by `outlineAmount` (0: the thing's own - none yet): a state without
// one keeps the last width and colour while the amount falls, so it fades and never gets thinner.
struct UiButtonLook {
    Color tint{1, 1, 1, 1};
    Color replace{1, 1, 1, 1};  // (r g b)
    float replaceAmount = 0;
    float scale = 1;
    float outline = 0;
    Color outlineColor{1, 1, 1, 1};
    float outlineAmount = 0;
};

// What a button does when pressed, without a script (配置と配線; spec §6-7). In order, after its sound and
// before the scripts' OnClick - so an OnClick of the same frame reads what they did. Only while the game
// plays (the editor's Play, the exported game). Each kind shows only its own fields (ShowIf).
RM_ENUM(Label="押したときの動作の種類")
enum class UiActionKind {
    RM_ITEM(Label="ウィンドウを出す") ShowWindow,
    RM_ITEM(Label="ウィンドウを隠す") HideWindow,
    RM_ITEM(Label="ウィンドウを切り替える") SwitchWindow,
    RM_ITEM(Label="前のウィンドウに戻る") Back,
    RM_ITEM(Label="シーンを切り替える") LoadScene,
    RM_ITEM(Label="変数を変える") SetVariable,
    RM_ITEM(Label="音を鳴らす") PlaySound,
    RM_ITEM(Label="ゲームを終わる") Quit,
};

RM_ENUM(Label="変数の変え方")
enum class UiVariableChange {
    RM_ITEM(Label="ON にする") On,
    RM_ITEM(Label="OFF にする") Off,
    RM_ITEM(Label="ON / OFF を入れ替える") Toggle,
    RM_ITEM(Label="足す") Add,
    RM_ITEM(Label="入れる") Set,
};

RM_STRUCT(Label="押したときの動作")
struct UiAction {
    RM_PROPERTY(Edit, Label="動作") UiActionKind kind = UiActionKind::ShowWindow;
    RM_PROPERTY(Edit, Label="ウィンドウ", Tooltip="キャンバスのエンティティ。切り替える: ほかのウィンドウは全部隠れます", ShowIf="kind=ShowWindow|HideWindow|SwitchWindow")
    EntityRef window;
    RM_PROPERTY(Edit, Label="シーン", Tooltip="ゲームのシーンの一覧 (シーン パネル) にあるシーン。このフレームの終わりに切り替わります", AssetKind=".rmscene", ShowIf="kind=LoadScene")
    AssetRef scene;
    RM_PROPERTY(Edit, Label="変数", Tooltip="ゲームの設定の「グローバル変数」の名前", ShowIf="kind=SetVariable") std::string variable;
    RM_PROPERTY(Edit, Label="変え方", Tooltip="ON / OFF は ON / OFF の変数 (数の変数なら 1 / 0)。足す・入れるは数の変数", ShowIf="kind=SetVariable")
    UiVariableChange change = UiVariableChange::Toggle;
    RM_PROPERTY(Edit, Label="数", Tooltip="足す数 / 入れる数 (整数の変数は四捨五入)", ShowIf="kind=SetVariable&change=Add|Set") float amount = 1;
    RM_PROPERTY(Edit, Label="下限を使う", ShowIf="kind=SetVariable&change=Add|Set") bool useMin = false;
    RM_PROPERTY(Edit, Label="下限", Tooltip="これより小さくならない", ShowIf="kind=SetVariable&change=Add|Set&useMin") float min = 0;
    RM_PROPERTY(Edit, Label="上限を使う", ShowIf="kind=SetVariable&change=Add|Set") bool useMax = false;
    RM_PROPERTY(Edit, Label="上限", Tooltip="これより大きくならない", ShowIf="kind=SetVariable&change=Add|Set&useMax") float max = 10;
    RM_PROPERTY(Edit, Label="音", AssetKind="audio", ShowIf="kind=PlaySound") AssetRef sound;
    RM_PROPERTY(Edit, Label="音の大きさ", Min=0, Max=1, ShowIf="kind=PlaySound") float volume = 1;
};

RM_STRUCT(Label="ボタン (UI)")
struct UiButton {
    RM_PROPERTY(Edit, Label="押せる", Tooltip="外すと薄くなり、押せません") bool interactable = true;
    RM_PROPERTY(Edit, Label="ふつうの色", Tooltip="同じエンティティの画像とバーの色に掛けます", Min=0, Max=1) Color normal{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="乗ったときの色", Tooltip="マウスが乗ったとき・キーやパッドで選んでいるとき", Min=0, Max=1) Color hover{0.9f, 0.9f, 0.9f, 1};
    RM_PROPERTY(Edit, Label="押したときの色", Min=0, Max=1) Color pressed{0.72f, 0.72f, 0.72f, 1};
    RM_PROPERTY(Edit, Label="使えないときの色", Min=0, Max=1) Color disabled{0.6f, 0.6f, 0.6f, 0.5f};
    RM_PROPERTY(Edit, Label="乗ったときの大きさ", Tooltip="1 でそのまま (子も一緒に)", Min=0.1, Max=4) float hoverScale = 1.04f;
    RM_PROPERTY(Edit, Label="押したときの大きさ", Min=0.1, Max=4) float pressedScale = 0.96f;
    RM_PROPERTY(Edit, Label="色の変え方", Tooltip="掛ける: 画像とバーの色に掛けます (暗くするだけで、明るくはできません)。置き換える: 乗った・押した・使えないときは、その色そのものになります")
    UiButtonColorMode colorMode = UiButtonColorMode::Multiply;
    RM_PROPERTY(Edit, Label="乗ったときの縁", Tooltip="ボタンの周りの縁の太さ (DIP)。0 なら縁なし", Min=0, Max=32) float hoverOutline = 2;
    RM_PROPERTY(Edit, Label="乗ったときの縁の色", Min=0, Max=1) Color hoverOutlineColor{1, 1, 1, 0.55f};
    RM_PROPERTY(Edit, Label="押したときの縁", Tooltip="0 なら縁なし", Min=0, Max=32) float pressedOutline = 2;
    RM_PROPERTY(Edit, Label="押したときの縁の色", Min=0, Max=1) Color pressedOutlineColor{1, 1, 1, 0.35f};
    RM_PROPERTY(Edit, Label="乗ったときの画像", Tooltip="同じエンティティの画像をこれに差し替えます。空ならそのまま", AssetKind="image") AssetRef hoverImage;
    RM_PROPERTY(Edit, Label="押したときの画像", Tooltip="空なら乗ったときの画像", AssetKind="image") AssetRef pressedImage;
    RM_PROPERTY(Edit, Label="使えないときの画像", Tooltip="空ならそのまま", AssetKind="image") AssetRef disabledImage;
    RM_PROPERTY(Edit, Label="変わる速さ (秒)", Tooltip="乗った・押したとき、色・大きさ・縁がこの秒でなめらかに変わります (およそ 3 倍の時間で落ち着きます)。0 ですぐ", Min=0, Max=2, Step=0.01)
    float transition = 0.08f;
    RM_PROPERTY(Edit, Label="乗ったときの音", Tooltip="マウスが乗ったとき・キーやパッドで選んだときに 1 回鳴ります (音のプラグインがあるとき)", AssetKind="audio") AssetRef hoverSound;
    RM_PROPERTY(Edit, Label="乗ったときの音の大きさ", Min=0, Max=1) float hoverVolume = 0.8f;
    RM_PROPERTY(Edit, Label="押したときの音", Tooltip="押したときに鳴ります (スクリプトの OnClick より先)", AssetKind="audio") AssetRef clickSound;
    RM_PROPERTY(Edit, Label="押したときの音の大きさ", Min=0, Max=1) float clickVolume = 1;
    RM_PROPERTY(Edit, Label="押したときの動作", Tooltip="スクリプトを書かずに、押したときに上から順に行います (押したときの音の後、スクリプトの OnClick の前)。Play とゲームの中だけ")
    std::vector<UiAction> actions;
    RM_PROPERTY(Edit, Label="上で移る先", Tooltip="キーやパッドで上へ移るときのボタン (同じウィンドウの押せるもの)。空なら画面の上で一番近いボタン") EntityRef up;
    RM_PROPERTY(Edit, Label="下で移る先", Tooltip="空なら画面の下で一番近いボタン") EntityRef down;
    RM_PROPERTY(Edit, Label="左で移る先", Tooltip="空なら画面の左で一番近いボタン") EntityRef left;
    RM_PROPERTY(Edit, Label="右で移る先", Tooltip="空なら画面の右で一番近いボタン") EntityRef right;
    RM_TRANSIENT() int state = 0;         // UiButtonState, this frame (the UI service)
    RM_TRANSIENT() bool focused = false;  // chosen by the keys / a pad (looks as Hover)
    RM_TRANSIENT() UiButtonLook now;      // how it looks now (the UI service eases it toward its state's look)
    RM_TRANSIENT() bool ready = false;    // `now` is set (else it looks as its state: the editor, the first frame)
    RM_TRANSIENT() bool hot = false;      // 乗った or pressed last frame (its sound once, when it becomes so)
};

RM_ENUM(Label="伸びる向き")
enum class UiBarDirection {
    RM_ITEM(Label="左から右") LeftToRight,
    RM_ITEM(Label="右から左") RightToLeft,
    RM_ITEM(Label="下から上") BottomToTop,
    RM_ITEM(Label="上から下") TopToBottom,
};

RM_STRUCT(Label="バー (UI)")
struct UiBar {
    RM_PROPERTY(Edit, Label="値", Tooltip="0 で空、1 でいっぱい (HP・時間など)", Min=0, Max=1, Step=0.01) float value = 1;
    RM_PROPERTY(Edit, Label="伸びる向き") UiBarDirection direction = UiBarDirection::LeftToRight;
    RM_PROPERTY(Edit, Label="地の色", Min=0, Max=1) Color back{0.12f, 0.12f, 0.15f, 0.85f};
    RM_PROPERTY(Edit, Label="中身の色", Min=0, Max=1) Color fill{0.35f, 0.85f, 0.45f, 1};
    RM_PROPERTY(Edit, Label="内側の余白", Tooltip="地の縁と中身の間 (DIP)", Min=0, Max=1000) float padding = 4;
};

}  // namespace rm::game_ui
