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

namespace rm::game_ui {

RM_ENUM(Label="大きさの合わせ方")
enum class UiScaleMode {
    RM_ITEM(Label="小さい方に合わせる (全部見える)") Fit,
    RM_ITEM(Label="大きい方に合わせる (画面を埋める)") Fill,
    RM_ITEM(Label="幅に合わせる") Width,
    RM_ITEM(Label="高さに合わせる") Height,
    RM_ITEM(Label="合わせない (DIP のまま)") None,
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

RM_STRUCT(Label="ボタン (UI)")
struct UiButton {
    RM_PROPERTY(Edit, Label="押せる", Tooltip="外すと薄くなり、押せません") bool interactable = true;
    RM_PROPERTY(Edit, Label="ふつうの色", Tooltip="同じエンティティの画像とバーの色に掛けます", Min=0, Max=1) Color normal{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="乗ったときの色", Tooltip="マウスが乗ったとき・キーやパッドで選んでいるとき", Min=0, Max=1) Color hover{0.9f, 0.9f, 0.9f, 1};
    RM_PROPERTY(Edit, Label="押したときの色", Min=0, Max=1) Color pressed{0.72f, 0.72f, 0.72f, 1};
    RM_PROPERTY(Edit, Label="使えないときの色", Min=0, Max=1) Color disabled{0.6f, 0.6f, 0.6f, 0.5f};
    RM_PROPERTY(Edit, Label="乗ったときの大きさ", Tooltip="1 でそのまま (子も一緒に)", Min=0.1, Max=4) float hoverScale = 1.04f;
    RM_PROPERTY(Edit, Label="押したときの大きさ", Min=0.1, Max=4) float pressedScale = 0.96f;
    RM_TRANSIENT() int state = 0;         // UiButtonState, this frame (the UI service)
    RM_TRANSIENT() bool focused = false;  // chosen by the keys / a pad (looks as Hover)
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
