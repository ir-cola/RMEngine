#pragma once
// ゲーム UI - ゲームの UI の部品。UI のものはすべてエンティティ (設計: docs/ARCHITECTURE.md の
// このプラグインの README の部分): キャンバス のエンティティが UI のウィンドウ 1 つ (タイトル、HUD、
// ポーズ ...) で、その下の UI のエンティティがその中に配置される。だから 階層、インスペクター、保存、
// Undo、Prefab、スクリプトの Spawn / Destroy が、UI でもほかのものと同じように働く (憲法 1)。
//
//   キャンバス          UI のウィンドウ: その 基準の解像度 と画面への収まり方。見える = エンティティが有効
//   UI の配置          UI のものが親の中のどこにあるか: 角 / 真ん中に固定、または引き伸ばし (アンカー)、
//                     その場所、大きさ、回転、兄弟の中での順
//   文字 / 画像 / ボタン / バー   何を見せ、何をするか
//
// 単位: キャンバスの 基準の解像度 の DIP。画面の Y は下向き (画面と同じ。2D のゲームの平面は
// Y が上 - UI は平面の上にない)。純粋な値 - 配置は UiLayout.h、描画は UiRenderer.h。

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

// ゲームが動いているあいだ、UI のものが今どう動かされているか。UI の配置の上に重ねる (保存しない):
// 出る・消える動き、道に沿って動く、スクリプトの ScaleTo / FadeTo (UiService が毎フレーム書き、
// 配置が使う)。その子も、一緒に動き、回り、大きくなり、フェードする。
struct UiLive {
    Vec2 offset{0, 0};  // これだけ動かす (親の単位で)
    float scale = 1;    // その 中心 のまわりに
    float turn = 0;     // 度、時計回り
    float alpha = 1;    // 0: 見えない .. 1
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
    RM_TRANSIENT() UiLive live;  // 今動いているウィンドウ (その 出る・消える動き)
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
    RM_TRANSIENT() UiLive live;  // ゲームが動いているあいだ、今動いているもの
};

// --- 見た目の続き (UI-5): 背景・枠・影・光・ぼかし・立体 ---------------------------------------
// 種類ごとに 1 つの構造体 (種類ごとに別のものを意味する 1 組の数値は作らない: 仕様 §2-7)。
// どれも使うまでは何も描かない (それを持たないものは、ビットまで前と同じに見える)。
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

RM_STRUCT(Label="虹色")
struct UiRainbow {
    RM_PROPERTY(Edit, Label="使う", Tooltip="色が虹のように回ります") bool enabled = false;
    RM_PROPERTY(Edit, Label="回る速さ", Tooltip="1 秒に何周するか", Min=0, Max=20, Step=0.05, ShowIf="enabled") float speed = 0.25f;
    RM_PROPERTY(Edit, Label="鮮やかさ", Min=0, Max=1, ShowIf="enabled") float saturation = 0.8f;
    RM_PROPERTY(Edit, Label="明るさ", Min=0, Max=1, ShowIf="enabled") float brightness = 1;
    RM_PROPERTY(Edit, Label="ずらし", Tooltip="始めの色 (0〜1)。並んだ物を少しずつずらすと虹が流れて見えます", Min=0, Max=1, ShowIf="enabled") float phase = 0;
    RM_PROPERTY(Edit, Label="画像・バー", Tooltip="画像とバー (と動画) の色に掛けます", ShowIf="enabled") bool body = true;
    RM_PROPERTY(Edit, Label="背景", Tooltip="色を虹色に置き換えます (透明度はそのまま)", ShowIf="enabled") bool background = false;
    RM_PROPERTY(Edit, Label="文字", ShowIf="enabled") bool text = false;
    RM_PROPERTY(Edit, Label="枠", ShowIf="enabled") bool border = false;
    RM_PROPERTY(Edit, Label="発光", ShowIf="enabled") bool glow = false;
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
    RM_PROPERTY(Edit, Label="虹色") UiRainbow rainbow;
};

RM_STRUCT(Label="動画 (UI)")
struct UiVideo {
    RM_PROPERTY(Edit, Label="動画", Tooltip="Assets の動画 (.mp4 を勧めます)。箱いっぱいに出ます。音も出ます (ゲームの設定の音量の 全体 × 音楽 が掛かります)",
                AssetKind=".mp4,.m4v,.mov,.wmv,.avi") AssetRef video;
    RM_PROPERTY(Edit, Label="色", Tooltip="動画の色に掛けます (白でそのまま)", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="始めから流す", Tooltip="外すと最初の絵で止まっています") bool playOnStart = true;
    RM_PROPERTY(Edit, Label="くり返す") bool loop = true;
    RM_PROPERTY(Edit, Label="速さ", Min=0.1, Max=8, Step=0.05) float speed = 1;
    RM_PROPERTY(Edit, Label="音の大きさ", Min=0, Max=1) float volume = 1;
    RM_PROPERTY(Edit, Label="始める所 (秒)", Min=0, Max=100000) float start = 0;
    RM_PROPERTY(Edit, Label="終わる所 (秒)", Tooltip="0 なら最後まで", Min=0, Max=100000) float end = 0;
    RM_TRANSIENT() float gain = 1;  // 全体 × 音楽 の今の値 (UI サービスが入れる。ゲームの外では 1)
};

// --- 動き (UI-4) -------------------------------------------------------------------------------
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

// 現れ方 (出るとき: この値から今のものの状態へ) か、去り方 (消えるとき: 今の状態からこの値へ) の 1 つ。
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
    RM_TRANSIENT() int phase = 0;      // UiMotionPhase (UI サービス)
    RM_TRANSIENT() float time = 0;     // 始まってからの秒
    RM_TRANSIENT() bool seen = false;  // 前のフレームで見えていた
};
// UiMotion が今していること (UiMotion::phase)。
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
    RM_TRANSIENT() float travelled = 0;  // 道に沿った道のり (UI サービス)
    RM_TRANSIENT() bool playing = false;
    RM_TRANSIENT() bool backward = false;  // 往復: 戻る途中
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

// 文字の一部を別に飾るもの (仕様 §2-5 部分スタイル): `start` (0: 最初。改行も 1 文字に数える) から
// `length` 文字。重なる所では後のものが勝つ。
RM_ENUM(Label="部分の切り替え")
enum class UiRunSwitch {
    RM_ITEM(Label="そのまま") Keep,
    RM_ITEM(Label="オン") On,
    RM_ITEM(Label="オフ") Off,
};
RM_STRUCT(Label="部分のスタイル")
struct UiTextRun {
    RM_PROPERTY(Edit, Label="何文字目から", Tooltip="0 から数えます (改行も 1 文字)", Min=0, Max=1000000) int start = 0;
    RM_PROPERTY(Edit, Label="文字数", Min=0, Max=1000000) int length = 1;
    RM_PROPERTY(Edit, Label="太字") UiRunSwitch bold = UiRunSwitch::Keep;
    RM_PROPERTY(Edit, Label="斜体") UiRunSwitch italic = UiRunSwitch::Keep;
    RM_PROPERTY(Edit, Label="下線") UiRunSwitch underline = UiRunSwitch::Keep;
    RM_PROPERTY(Edit, Label="打ち消し線") UiRunSwitch strike = UiRunSwitch::Keep;
    RM_PROPERTY(Edit, Label="色を変える") bool setColor = false;
    RM_PROPERTY(Edit, Label="色", Min=0, Max=1, ShowIf="setColor") Color color{1, 0.85f, 0.3f, 1};
    RM_PROPERTY(Edit, Label="大きさを変える") bool setSize = false;
    RM_PROPERTY(Edit, Label="大きさ", Min=1, Max=1000, ShowIf="setSize") float size = 48;
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
    RM_PROPERTY(Edit, Label="斜体") bool italic = false;
    RM_PROPERTY(Edit, Label="下線") bool underline = false;
    RM_PROPERTY(Edit, Label="打ち消し線") bool strike = false;
    RM_PROPERTY(Edit, Label="字間", Tooltip="文字と文字の間に足す幅 (DIP。- で詰める)", Min=-100, Max=500) float letterSpacing = 0;
    RM_PROPERTY(Edit, Label="行の高さ", Tooltip="行と行の間 (DIP)。0 ならフォントのまま", Min=0, Max=2000) float lineHeight = 0;
    RM_PROPERTY(Edit, Label="縦書き", Tooltip="上から下へ、行は右から左へ") bool vertical = false;
    RM_PROPERTY(Edit, Label="はみ出したら縮める", Tooltip="枠に入りきらないとき、入るまで文字を小さくします") bool shrinkToFit = false;
    RM_PROPERTY(Edit, Label="文字だけずらす", Tooltip="見た目だけ文字をずらします (枠・折り返し・マウスの当たりは動かない)", Min=-1000, Max=1000) Vec2 offset{0, 0};
    RM_PROPERTY(Edit, Label="部分のスタイル", Tooltip="一部の文字だけ太字・色・大きさなどを変えます (後ろの物が勝つ)") std::vector<UiTextRun> runs;
};

RM_STRUCT(Label="画像 (UI)")
struct UiImage {
    RM_PROPERTY(Edit, Label="画像", Tooltip="画像のアセット。無いときは色の四角", AssetKind="image") AssetRef image;
    RM_PROPERTY(Edit, Label="色", Tooltip="画像の色に掛けます (白でそのまま)。透明度も効きます", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="9 スライス (左・上)", Tooltip="画像の左と上の縁の幅 (画像のピクセル)。縁は伸ばさず、真ん中だけを伸ばします (枠やボタンの絵)", Min=0, Max=16384)
    Vec2 sliceTopLeft{0, 0};
    RM_PROPERTY(Edit, Label="9 スライス (右・下)", Tooltip="画像の右と下の縁の幅 (画像のピクセル)", Min=0, Max=16384) Vec2 sliceBottomRight{0, 0};
    RM_PROPERTY(Edit, Label="縁の大きさ", Tooltip="9 スライスの縁を描く大きさ (1 で画像のピクセル = 1 DIP)", Min=0.01, Max=100) float sliceScale = 1;
    RM_PROPERTY(Edit, Label="切り出し (左上)", Tooltip="画像のこの点 (ピクセル) から切り出します", Min=0, Max=100000) Vec2 cropOffset{0, 0};
    RM_PROPERTY(Edit, Label="切り出し (大きさ)", Tooltip="切り出す幅と高さ (ピクセル)。0 なら右 / 下の端まで。9 スライスは切り出した中で数えます", Min=0, Max=100000)
    Vec2 cropSize{0, 0};
};

// このフレームでボタンがいる状態 (ゲームが動くあいだ UI サービスが書く。保存しない)。
enum class UiButtonState : int { Normal = 0, Hover = 1, Pressed = 2, Disabled = 3 };

RM_ENUM(Label="ボタンの色の変え方")
enum class UiButtonColorMode {
    RM_ITEM(Label="掛ける") Multiply,
    RM_ITEM(Label="置き換える") Replace,
};

// ボタンが今どう見えているか。状態の見た目へ向かう途中のもの (UiLayout: ButtonTarget / EaseLook)。
// 色は 2 通りで同時に動く (仕様 §6-4): `tint` を掛け、そのあと `replaceAmount` だけ `replace` へ寄せる -
// 置き換わるもの (絵、バーの背、その中身) が違うので、色 1 つでは足りない。輪郭はその状態のもので、
// `outlineAmount` で混ぜる (0: そのもの自身のもの - まだ無い): 輪郭の無い状態は、量が減るあいだ
// 最後の太さと色を保つので、細くならずに消える。
struct UiButtonLook {
    Color tint{1, 1, 1, 1};
    Color replace{1, 1, 1, 1};  // (r g b)
    float replaceAmount = 0;
    float scale = 1;
    float outline = 0;
    Color outlineColor{1, 1, 1, 1};
    float outlineAmount = 0;
};

// ボタンを押したとき、スクリプト無しでしてくれること (配置と配線; 仕様 §6-7)。順に、その音のあと、
// スクリプトの OnClick より前に - だから同じフレームの OnClick は、これがしたことを読む。
// ゲームが動くあいだだけ (エディターの Play、書き出したゲーム)。種類ごとに自分の欄だけを出す (ShowIf)。
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
    RM_TRANSIENT() int state = 0;         // UiButtonState、このフレームの (UI サービス)
    RM_TRANSIENT() bool focused = false;  // キーやパッドで選ばれた (Hover と同じに見える)
    RM_TRANSIENT() UiButtonLook now;      // 今の見た目 (UI サービスが、その状態の見た目へなめらかに寄せる)
    RM_TRANSIENT() bool ready = false;    // `now` が入っている (入っていなければ、その状態のとおりに見える: エディター、最初のフレーム)
    RM_TRANSIENT() bool hot = false;      // 前のフレームで 乗った か押された (その音は、そうなったとき 1 回だけ)
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

// --- 入力の部品 (入力欄・スライダー・トグル・スクロール) -----------------------------------------
// 入力欄・スライダー・トグルは、同じエンティティの「ボタン (UI)」で選ばれる (マウスの乗った / 押した
// 見た目・キーとパッドの選択・移る先・音・押せる)。だから選び方は 1 つの仕組みのまま (NextButton)。
// 「追加 ▸ UI」はボタンも一緒に付ける。値が変わるとスクリプトの OnEvent に "UIChanged" (数 = 値、
// 文字 = 入力欄の文字)、入力欄で Enter を押すと "UISubmitted" が届く (RMUI.h の rm::UI::kChanged)。

RM_ENUM(Label="入力できる文字")
enum class UiInputContent {
    RM_ITEM(Label="何でも") Any,
    RM_ITEM(Label="整数") Integer,
    RM_ITEM(Label="数 (小数も)") Decimal,
    RM_ITEM(Label="英字と数字") Alphanumeric,
};

RM_STRUCT(Label="入力欄 (UI)")
struct UiInputField {
    RM_PROPERTY(Edit, Label="入っている文字", Tooltip="ゲームの始めに入っている文字。打った文字はここに入ります (スクリプトの rm::UI::Text)") std::string text;
    RM_PROPERTY(Edit, Label="空のときの案内", Tooltip="何も入っていないときに薄く出す文字") std::string placeholder = "ここに入力";
    RM_PROPERTY(Edit, Label="案内の色", Min=0, Max=1) Color placeholderColor{1, 1, 1, 0.4f};
    RM_PROPERTY(Edit, Label="最大の文字数", Tooltip="0 なら決めない (見える 1 文字を 1 と数えます)", Min=0, Max=100000) int maxLength = 0;
    RM_PROPERTY(Edit, Label="入力できる文字", Tooltip="整数・数・英字と数字: ほかの文字は入りません (全角の数字と英字は半角にします)")
    UiInputContent content = UiInputContent::Any;
    RM_PROPERTY(Edit, Label="パスワード", Tooltip="打った文字を ● で隠します (コピーもできません)") bool password = false;
    RM_PROPERTY(Edit, Label="左右の余白", Tooltip="枠の左右と文字の間 (DIP)", Min=0, Max=1000) float padding = 16;
    RM_PROPERTY(Edit, Label="カーソルの色", Min=0, Max=1) Color caretColor{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="選んだ所の色", Min=0, Max=1) Color selectionColor{0.35f, 0.55f, 1, 0.45f};
    RM_TRANSIENT() bool typing = false;   // 今この欄に打っている (UI サービス)
    RM_TRANSIENT() int caret = 0;         // カーソル (text を UTF-16 にした位置)
    RM_TRANSIENT() int anchor = 0;        // 選び始めた所 (caret と違えば選んでいる)
    RM_TRANSIENT() std::string composing; // IME で変換中の文字 (UTF-8。まだ text に入っていない)
    RM_TRANSIENT() int composingCaret = 0;
};

RM_ENUM(Label="トグルの形")
enum class UiToggleLook {
    RM_ITEM(Label="チェックの箱") Check,
    RM_ITEM(Label="スイッチ") Switch,
};

RM_STRUCT(Label="トグル (UI)")
struct UiToggle {
    RM_PROPERTY(Edit, Label="オン", Tooltip="押すたびにオンとオフが入れ替わります (スクリプトの rm::UI::IsOn)") bool on = false;
    RM_PROPERTY(Edit, Label="形") UiToggleLook look = UiToggleLook::Check;
    RM_PROPERTY(Edit, Label="印の大きさ", Tooltip="箱 (スイッチ) の高さ (DIP)。0 なら枠の高さ。枠の左に描きます", Min=0, Max=1000) float markSize = 0;
    RM_PROPERTY(Edit, Label="地の色", Min=0, Max=1) Color back{0.12f, 0.12f, 0.15f, 0.9f};
    RM_PROPERTY(Edit, Label="オンの色", Tooltip="オンのときの箱 (スイッチ) の色", Min=0, Max=1) Color onColor{0.3f, 0.65f, 1, 1};
    RM_PROPERTY(Edit, Label="印の色", Tooltip="チェックの印・スイッチのつまみ", Min=0, Max=1) Color mark{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="グループ", Tooltip="同じウィンドウで同じ名前のトグルは 1 つだけオンになります (ラジオボタン。オンのものを押してもオンのまま)。空なら別々")
    std::string group;
    RM_TRANSIENT() float shown = 0;    // 見えているオンの量 0..1 (UI サービスがなめらかに寄せる)
    RM_TRANSIENT() bool ready = false;  // `shown` が入っている (無ければ on のとおり)
};

RM_STRUCT(Label="スライダー (UI)")
struct UiSlider {
    RM_PROPERTY(Edit, Label="値", Tooltip="最小から最大の間 (刻みに合わせます。スクリプトの rm::UI::Value)", Min=-1000000, Max=1000000) float value = 0.5f;
    RM_PROPERTY(Edit, Label="最小", Min=-1000000, Max=1000000) float min = 0;
    RM_PROPERTY(Edit, Label="最大", Min=-1000000, Max=1000000) float max = 1;
    RM_PROPERTY(Edit, Label="値の刻み", Tooltip="0 ならなめらか。1 で整数だけ", Min=0, Max=1000000) float step = 0;
    RM_PROPERTY(Edit, Label="伸びる向き", Tooltip="値が増える向き。左から右・右から左はキーの ← →、下から上・上から下は ↑ ↓ で動きます")
    UiBarDirection direction = UiBarDirection::LeftToRight;
    RM_PROPERTY(Edit, Label="キーで動く量", Tooltip="キー・パッドの 1 回で動く量。0 なら刻み (刻みも 0 なら幅の 20 分の 1)", Min=0, Max=1000000) float keyStep = 0;
    RM_PROPERTY(Edit, Label="溝の太さ", Tooltip="地と中身の帯の太さ (DIP)", Min=0, Max=1000) float trackWidth = 12;
    RM_PROPERTY(Edit, Label="つまみの大きさ", Tooltip="丸いつまみの直径 (DIP)。0 ならつまみなし", Min=0, Max=1000) float handleSize = 36;
    RM_PROPERTY(Edit, Label="地の色", Min=0, Max=1) Color back{0.12f, 0.12f, 0.15f, 0.85f};
    RM_PROPERTY(Edit, Label="中身の色", Min=0, Max=1) Color fill{0.3f, 0.65f, 1, 1};
    RM_PROPERTY(Edit, Label="つまみの色", Min=0, Max=1) Color handle{1, 1, 1, 1};
};

RM_STRUCT(Label="スクロール (UI)")
struct UiScroll {
    RM_PROPERTY(Edit, Label="縦に動く") bool vertical = true;
    RM_PROPERTY(Edit, Label="横に動く") bool horizontal = false;
    RM_PROPERTY(Edit, Label="ホイールの 1 目盛り", Tooltip="ホイールを 1 つ回すと動く長さ (DIP)", Min=0, Max=100000) float wheelStep = 80;
    RM_PROPERTY(Edit, Label="つかんで動かす", Tooltip="中身の空いた所をドラッグして動かせます (スマホのように)") bool drag = true;
    RM_PROPERTY(Edit, Label="選んだ物を見せる", Tooltip="キーやパッドで中のボタンを選ぶと、それが見えるまで動きます") bool followFocus = true;
    RM_PROPERTY(Edit, Label="スクロールバー", Tooltip="枠の右 (横は下) に、今どこを見ているかの帯を出します。つまみはドラッグできます") bool scrollbar = true;
    RM_PROPERTY(Edit, Label="バーの太さ", Min=1, Max=200, ShowIf="scrollbar") float barWidth = 10;
    RM_PROPERTY(Edit, Label="バーの地の色", Min=0, Max=1, ShowIf="scrollbar") Color barBack{1, 1, 1, 0.12f};
    RM_PROPERTY(Edit, Label="バーのつまみの色", Min=0, Max=1, ShowIf="scrollbar") Color barThumb{1, 1, 1, 0.55f};
    RM_TRANSIENT() Vec2 offset{0, 0};  // どれだけ動かしたか (中身が上 / 左へ動く。UI サービス)
    RM_TRANSIENT() Vec2 low{0, 0};   // 動かせる範囲 (UI サービスが毎フレーム書く: スクリプトの 0..1)
    RM_TRANSIENT() Vec2 high{0, 0};
};

}  // namespace rm::game_ui
