#pragma once
// エディターの部品: 核の UI の土台 (ui::Context) + エディターのテーマ。
//
// ImGui のように使う - 1 回の呼び出しで 1 つのものを描き、ユーザーが何をしたかを返す:
//
//   if (w::Button(c, "save", r, Tr(L"保存"), w::ButtonKind::Primary)) Save();
//   if (w::Selectable(c, "row", r, selected).clicked) Select();
//   if (w::CollapsingHeader(c, "hdr", r, title, open)) ...;
//   if (w::IconButton(c, "more", r, w::Icon::Dots)) OpenMenu();
//
// 画面上の箱はすべて下の Surface のどれか、リストの行はすべて DrawRow、タブはすべて DrawTab、
// アイコンはすべて DrawIcon - それぞれ描き方は 1 本 (憲法 3)。画面は自分で四角を塗らない:
// 新しい見た目はここで 1 回作れば、どこにでも出る。色とグラデーションは Theme.h から来る。

#include "editor/widgets/Theme.h"
#include "ui/Context.h"

namespace rm::platform {
class Gfx;
}

#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {

enum class ButtonKind { Primary, Secondary, Ghost };

// --- 面 ------------------------------------------------------------------------------------
enum class Surface {
    Panel,          // パネル / カード / リスト: 穏やかなグラデーション、枠
    Raised,         // ボタン / 見出し: 盛り上がったグラデーション、上に明るい線、枠
    Hovered,        // マウスが乗った Raised
    Pressed,        // 押し込まれた Raised (グラデーションを逆にする)
    Sunken,         // テキストフィールド / チェックのない箱: 上のほうが暗い
    Accent,         // 主役のボタン / チェックのある箱
    AccentHovered,
    AccentPressed,
    Selected,       // 選んだ項目
    Popup,          // メニュー / ツールチップ / ダイアログ: やわらかい影、盛り上がったグラデーション、枠
};
void DrawSurface(ui::Context& c, const ui::Rect& r, Surface s, float radius = theme::Radius);
// 枠 (theme::EdgeOuter / EdgeInner) のあるパネル: 暗い線は r の 1px 外側に引くので、1px 離れた 2 つの
// パネルは 1 本の暗い継ぎ目を共有する。角ごとに別々の半径 - ほかに接する所は四角。
void DrawPanelFrame(ui::Context& c, const ui::Rect& r, ui::Corners radii);

// --- テーマの絵: いつも内容の後ろ ---------------------------------------------------------
// テーマパックのコードが描く (kOrnamentPoint で受け付ける。同じプラグインのテーマファイルが
// `name` で 1 つ指す - "ornament")。
struct OrnamentInfo {
    const char* name;
    // ウィンドウ全体の絵 (`floating`: 独立したパネルのウィンドウ - 明るめの絵)。ウィンドウの背景に描き、
    // その後、各パネルとバーの塗りの後にもう一度描く (それに切り取って)。
    void (*window)(ui::Context& c, const ui::Rect& window, bool floating);
    // パネル自身の地 (夏: 砂の粒)。パネルを塗った後、ウィンドウの絵より先に描く (絵が上になる)。null でもよい。
    void (*panel)(ui::Context& c, const ui::Rect& r, float corner);
};
inline constexpr char kOrnamentPoint[] = "core.theme_ornament";
// フレームの最初、ウィンドウをクリアした後: 背景にテーマの絵を描く。
void DrawBackdrop(ui::Context& c, bool floating);
// 塗ったばかりの面にもう一度絵を描く (DrawPanelFrame / DrawBar がやる)。`r` に切り取る。
// (`panelThings`) 先にパックのパネル用の地 (夏: 砂の粒) も描く - バーには描かない。
void DrawBackdropOn(ui::Context& c, const ui::Rect& r, float corner = 0, bool panelThings = true);

// ウィンドウの枠の帯 (メニューバー、ステータスバー、サイドバー): 穏やかなグラデーション、枠なし。
void DrawBar(ui::Context& c, const ui::Rect& r, ui::Color base);

// --- アイコン: 線で描くので、どの大きさでも箱の真ん中に収まる ---------------------------------
enum class Icon {
    Close, Dots, DotsVertical, ChevronDown, ChevronRight, ChevronUp, ChevronLeft, Check, Dash, Object, Cube, Square, Plus, Grip, Play, Pause, Step, Stop,
    // Menu: 横線 3 本 (ハンバーガー: 隠した一覧を出す - 細いヘルプの左上)
    Menu,
    // エンティティが何か (階層、シーンビューの印)
    // Point: 場所だけのもの (点のある輪 - シーンビューの空のエンティティの印);
    // Effect: きらめき (パーティクル); Scene: 層 (シーン - 階層の一番上の行)
    Camera, Light, Point, Effect, Scene,
    // ファイルが何か (アセットパネル、参照フィールド)
    Folder, File, Image, Audio, Font, Text,
    // ここから下は絵だけ (resources/icons/。どの絵かは IconAtlas.cpp の表)。
    // 道具 (シーンビューの左の列) と、インスペクターの項目の前 (RM_PROPERTY の Icon="...")
    Cursor, Move, Rotate, Scale, Grid, RotationLock,
    // 知らせ (色付きの絵: 渡した色の透明さだけを使う)
    Warning, Error,
    // 絵はあるが、まだどこにも出していない物
    Back, Undo, Redo, Lock, Unlock, Search, Trash, Color, Settings, Help, Human, Prefab,
    BoneMoveX, BoneMoveY, BoneMoveZ, BoneRotateX, BoneRotateY, BoneRotateZ,
};
// `size`: 絵のアイコンの一辺 (DIP)。0 = 四角に合わせる (短い辺の 8 割。16 まで)。線で描く印は四角に合わせる。
void DrawIcon(ui::Context& c, const ui::Rect& r, Icon icon, ui::Color color, float size = 0);
// 絵のアイコンを読む GPU (App が 1 回)。読むのは初めて描くとき。GPU を作り直したら読み直す。
void InitIcons(platform::Gfx& gfx);

// --- ボタン ----------------------------------------------------------------------------------
bool Button(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label,
            ButtonKind kind = ButtonKind::Secondary, bool enabled = true);
// `label` の Button がちょうど入る幅 (太字でも: Primary とほかを切り替えても幅が変わらない)。
float ButtonWidth(ui::Context& c, std::wstring_view label);
// アイコンのある小さな四角のボタン (Ghost の見た目: マウスの下のときだけ面が出る)。
bool IconButton(ui::Context& c, std::string_view id, const ui::Rect& r, Icon icon, bool enabled = true);
// `on` の間、押し込まれたままの (アクセントの) アイコンボタン - 動いている間のゲームの ▶。
// `tooltip` (省略可) はマウスの下に出る。
bool IconToggle(ui::Context& c, std::string_view id, const ui::Rect& r, Icon icon, bool on, bool enabled = true,
                std::wstring_view tooltip = {});
// 道具を選ぶ正方形のボタン (シーンビューの左の列: グリッド配置・選択・移動・回転・拡縮)。`on` の間は
// 押し込まれたまま (アクセント)。真ん中にアイコン (名前は `tooltip` で出す)。右クリックも返す
// (グリッド配置: 幅の箱を開く)。
ui::Context::ButtonResult ToolSquare(ui::Context& c, std::string_view id, const ui::Rect& r, Icon icon, bool on,
                                     std::wstring_view tooltip = {});
// 1 本の浮かんだ帯に横に並んだ、いくつかの選択肢の 1 つ (シーンビューの 移動 / 回転 / 拡大縮小):
// 選んだものがアクセントの色。クリックされた番号を返す。なければ -1。`tooltips` (省略可、
// ラベルごとに 1 つ) はマウスの下に出る。
int Segmented(ui::Context& c, std::string_view id, const ui::Rect& r, std::span<const std::wstring_view> labels, int selected,
              std::span<const std::wstring_view> tooltips = {});
// `labels` の Segmented がちょうど入る幅 (どの区切りも一番長いラベルの幅: 選んだ太字でも切れない)。
float SegmentedWidth(ui::Context& c, std::span<const std::wstring_view> labels);

// --- 行 (リスト、ツリー、メニュー、サイドバー) -----------------------------------------------
struct RowLook {
    bool selected = false;
    bool hovered = false;
    bool focused = true;  // リストがキーを受け取っている: 選んだ行にアクセントの棒が出る
    bool flat = false;    // 四角で、端から端まで: 線で区切ったリストの行 (Hub のページ、プロジェクトの一覧)
};
void DrawRow(ui::Context& c, const ui::Rect& r, RowLook look);
// 並びの 1 つのタイル (アセット パネルの右側): 選んだもの / マウスの下のものの面。行と同じ色で、
// 棒の代わりに、キーを受け取っている間は選んだタイルをアクセントで縁取る。
void DrawTile(ui::Context& c, const ui::Rect& r, bool selected, bool hovered, bool focused = true);
// ドラッグしているもの (ツリーの行、アセットのタイル) の札を、マウスの横に (フレームの終わりに、
// すべての上に)。`inside`: 自分の中に落とせる所の上 - 受け取る所が無くても縁取る。
void DragTag(ui::Context& c, std::wstring text, bool inside);
// クリックできる行 (ImGui の Selectable)。行を描く。行の上にあるものは呼び出し側が描く。
ui::Context::ButtonResult Selectable(ui::Context& c, std::string_view id, const ui::Rect& r, bool selected, bool flat = false);
// 選ぶテーマ 1 つ (テーマの画面): そのテーマ自身の色で描いた小さなウィンドウと、その下に名前。
// 選んだものはアクセントで縁取ってチェックを付ける。palette = nullptr: "PC の設定に合わせる"
// (半分が暗く、半分が明るい)。
ui::Context::ButtonResult ThemeCard(ui::Context& c, std::string_view id, const ui::Rect& r, const theme::Palette* palette,
                                    std::wstring_view name, bool selected);
// サイドバーのページへのリンク: 平らな行 (行の間の線は呼び出し側が描く)。クリックされたら true。
bool NavItem(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label, bool selected);

// --- タブと見出し ----------------------------------------------------------------------------
void DrawTab(ui::Context& c, const ui::Rect& r, bool selected, bool hovered);
// 折りたためる節 (ImGui の CollapsingHeader): クリックされると `open` を反転して true を返す。
// `reserveRight` は、呼び出し側がその上に描くボタンのために右に余白を残す。
bool CollapsingHeader(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view title, bool& open,
                      float reserveRight = 0.f);

// --- ドロップダウンリストの顔 (リスト自体は呼び出し側が開く MenuPopup) --------------------
// `icon`: 文字の前の絵 (参照フィールド: どの種類のファイルか)。
ui::Context::ButtonResult ComboBox(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view text, bool enabled = true,
                                   std::optional<Icon> icon = std::nullopt);

// どこまで進んだか: 沈んだ溝を、`fraction` (0..1) までアクセントで塗る。
void ProgressBar(ui::Context& c, const ui::Rect& r, float fraction);

// リストの事実 1 つ (デバッグパネル): 名前が左に薄く (`labelWidth`)、値が右に。
// `lit`: 値をアクセントで (押されているボタン、オンになっているもの)。
void InfoRow(ui::Context& c, const ui::Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth, bool lit = false);
// 同じで、値を危険の色にしたもの (目立たせたいエラー: 失敗したビルド)。
void InfoRowDanger(ui::Context& c, const ui::Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth);
// ファイルフォルダーの形のウィンドウ (ゲーム制作の デバッグ): シートの上に立つタブ。選んでいない
// タブを描き、次にシート、最後に選んだタブを描く - それはシートの色でシートにつながる (間に線がない)。
// FolderTab: クリックされたら true。squareTopLeft: 最初のタブを選んでいる (その下のシートの角は
// タブ側なので丸くしない)。
bool FolderTab(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label, bool selected, float radius);
void DrawFolderSheet(ui::Context& c, const ui::Rect& r, float radius, bool squareTopLeft);

// 時間に沿った値、新しいものが右 (デバッグパネルのフレーム時間): `a` はアクセントの線、
// `b` (空でもよい) は警告の色の線、下が 0 で上が `top` (それより高いものは切る);
// `guides`: その値の所に横切る薄い線。
void LineGraph(ui::Context& c, const ui::Rect& r, std::span<const float> a, std::span<const float> b, float top, std::span<const float> guides);

// --- 線とヒント ------------------------------------------------------------------------------
void Separator(ui::Context& c, float x, float y, float width);
// 色見本 (インスペクターの色の欄、気候の種類の行): `color` (ユーザーのデータの色) の角の丸い四角と縁。
void DrawSwatch(ui::Context& c, const ui::Rect& r, ui::Color color);

// --- 地図 (真上から見た絵の上に描くもの: 気候のタブ) ----------------------------------------
// 塗った所 (同じ色のマスの並び 1 つ): `color` (ユーザーのデータの色) を、下の絵が透けるように。
void DrawMapPaint(ui::Context& c, const ui::Rect& r, ui::Color color);
// マス目の線: `area` の中に、(x0, y0) を通って `step` DIP ごと。薄く。
void DrawMapGrid(ui::Context& c, const ui::Rect& area, float x0, float y0, float step);
// 地図の上の線 (気候エリアの縁など)。
void DrawMapLine(ui::Context& c, float x1, float y1, float x2, float y2, ui::Color color);
// 塗る筆の輪 (真ん中と半径 DIP)。`erase`: 消しゴム (危険の色)。
void DrawMapBrush(ui::Context& c, float cx, float cy, float radius, bool erase);
// 縦の区切り線 (並んだ 2 つの部分の間: アセット パネルの木と並び)。
void VSeparator(ui::Context& c, float x, float y, float height);
// マウスの横のヒント。フレームの最後に (何よりも上に) 描く。
void Tooltip(ui::Context& c, std::wstring_view text);

// 1 行のテキストフィールド (IME、選択、クリップボード)。値が変わったら true を返す。
// マウスのドラッグで選び、ダブルクリックで全部選ぶ。selectAllOnFocus: フィールドに入るクリックが
// 全部を選ぶ (上から打ち込む値 - インスペクターのフィールド); フィールドに入るドラッグは、
// 代わりに通った範囲を選ぶ。
bool TextField(ui::Context& c, std::string_view id, const ui::Rect& r, std::string& valueUtf8,
               std::wstring_view placeholder = {}, bool enabled = true, bool selectAllOnFocus = false);

// ラベル付きのチェックボックス。値が変わったら true を返す。
// mixed: まとめて編集しているオブジェクトが一致しない - チェックの代わりに横棒を出す。クリックするとオンになる。
bool Checkbox(ui::Context& c, std::string_view id, const ui::Rect& r, bool& value, std::wstring_view label, bool mixed = false);
// 数の付いたチェックボックス (コンソールの Info / Warn / Error): [箱] 印 ラベル 数 の並び。`count` は太字の
// `countColor` で。`label` は空でもよい (狭いとき: チェックと印と数だけ)。値が変わったら true。
// 要る幅: CheckboxCountWidth (`count` が空なら、ふつうの Checkbox の幅)。
// `mark` (省略可): ラベルのすぐ前に出す印 (警告 / エラーの絵)。`markAlpha` の濃さで (数が 0 のとき薄く)。
bool CheckboxCount(ui::Context& c, std::string_view id, const ui::Rect& r, bool& value, std::wstring_view label,
                   std::wstring_view count, ui::Color countColor, std::optional<Icon> mark = std::nullopt,
                   float markAlpha = 1.f);
float CheckboxCountWidth(ui::Context& c, std::wstring_view label, std::wstring_view count = {},
                         std::optional<Icon> mark = std::nullopt);
// テキストフィールドの数 (ビューの設定 - シーンビューの刻み): フォーカスがある間は打ったままの
// 文字、それ以外は値 (`text` がフレームの間それを持つ)。`value` が変わったら true: 数が
// 打たれた (min .. max に収める); 数でない文字は何も変えない。
bool NumberField(ui::Context& c, std::string_view id, const ui::Rect& r, float& value, float min, float max, std::string& text);
// 中のものを選ぶためにドラッグして作る四角 (シーンビュー): 薄いアクセントと、その縁。
void DrawSelectionBand(ui::Context& c, const ui::Rect& r);
// 携帯電話のようなスイッチ: 長い丸い溝と、オンのときに右へ滑る丸いつまみ (溝はアクセントの色に
// 変わる)。`offLabel` / `onLabel` (省略可) はその左 / 右に立つ。選んでいるほうが明るい。
// どこをクリックしても反転する。クリックされたら true を返す (`on` が変わった)。
// 必要な幅: SwitchWidth。
bool Switch(ui::Context& c, std::string_view id, float x, float y, bool& on, std::wstring_view offLabel = {},
            std::wstring_view onLabel = {});
float SwitchWidth(ui::Context& c, std::wstring_view offLabel = {}, std::wstring_view onLabel = {});
inline constexpr float kSwitchH = 22.f;

// ファイルの小さな絵 (アセットパネルのサムネイル): 薄い四角の上に、形を保って `r` の中に収める。
// `dimmed` のときは薄く。
void DrawThumbnail(ui::Context& c, const ui::Rect& r, ID2D1Bitmap* bitmap, bool dimmed = false);

// スライダー: 細い溝 (つまみまでがアクセント) と丸いつまみ。どこでも押してドラッグすると
// `value` が min と max の間で決まる (外に出ても、離すまでマウスに付いていく)。
// 値を変えている間 true を返す。
bool Slider(ui::Context& c, std::string_view id, const ui::Rect& r, float& value, float min, float max);

// --- 時間の帯 (モデルのクリップの帯: 0 から `duration` 秒の目盛りの上に、時刻に置いた印) -----------
// 帯は 3 段: 上が印の名前、真ん中が溝 (印の ▲ と縦の線)、下が目盛りの秒。高さは kTimeStripHeight。
inline constexpr float kTimeStripHeight = 58.f;
// 時刻 `t` の帯の上の x と、その逆 (帯の外は 0 と `duration` に収める)。
float TimeStripX(const ui::Rect& r, float duration, float t);
float TimeStripTime(const ui::Rect& r, float duration, float x);
// 溝と目盛り (詰まらない間: 0.1 / 0.2 / 0.5 / 1 / 2 / 5 … 秒) とその秒の数。`cursor` >= 0: その時刻に縦の線。
void DrawTimeStrip(ui::Context& c, const ui::Rect& r, float duration, float cursor = -1.f);
// 時刻の印 1 つ: 溝の中の ▲ と縦の線、上に名前 (`x` は TimeStripX)。押せる所は TimeMarkerRect。
ui::Rect TimeMarkerRect(const ui::Rect& strip, float x);
void DrawTimeMarker(ui::Context& c, const ui::Rect& strip, float x, std::wstring_view label, bool selected, bool hovered);

// タイトルバー付きの角の丸いパネル。内容の領域を返す。
ui::Rect Panel(ui::Context& c, const ui::Rect& r, std::wstring_view title);

// 縦のスクロール。内容を Begin と End の間に描き、返された値だけずらす。
float BeginScroll(ui::Context& c, std::string_view id, const ui::Rect& view, float contentHeight);
void EndScroll(ui::Context& c, std::string_view id, const ui::Rect& view, float contentHeight);
// 次の BeginScroll で末尾へ飛ぶ (新しい出力を追うコンソール用)。
void ScrollToEnd(ui::Context& c, std::string_view id);
// 今 (このフレームの EndScroll の後で) 末尾にいるか。中身が見える高さに収まるときも true
// (コンソール: 一番下にいる間だけ新しい出力を追う)。
bool ScrollAtEnd(ui::Context& c, std::string_view id, const ui::Rect& view, float contentHeight);

// --- ポップアップメニュー ---------------------------------------------------
struct MenuItem {
    std::wstring label;
    std::wstring shortcut;
    bool enabled = true;
    bool separator = false;
    bool checked = false;  // ラベルの前のチェックマーク
    bool submenu = false;  // 別のメニューを開く (右に ▸。ActionMenu のフォルダー)
};

struct Popup {
    bool open = false;
    bool fresh = false;  // このフレームの間に開いた: 開いたクリックは「外」のクリックではない
    float x = 0, y = 0;
    std::wstring sub;  // ActionMenu: 今メニューが開いているフォルダー ("追加/3D オブジェクト"; "" = なし)
    void OpenAt(float px, float py) {
        open = true;
        fresh = true;
        x = px;
        y = py;
        sub.clear();
    }
};

// 開いているポップアップを描く。クリックされた項目の番号か -1 を返す。
// 選ぶ・Escape・外のクリックで閉じる (そのクリックは受け止める)。
int MenuPopup(ui::Context& c, Popup& popup, const std::vector<MenuItem>& items, float width = 240.f);
// MenuPopup が使う四角 (ビューポートの中に収めた後)。
ui::Rect MenuRect(const ui::Context& c, const Popup& popup, const std::vector<MenuItem>& items, float width = 240.f);

// 何かをするメニューの行。ショートカットを付けてもよい。ソフトが埋めるメニュー
// (階層の右クリックメニュー、インスペクターのブロックのメニュー ...) が使う。
struct Action {
    std::wstring label;
    std::function<void()> run;  // 区切りなら空
    bool enabled = true;
    bool separator = false;
    ui::Key key = ui::Key::None;
    bool ctrl = false, shift = false;
    // 入っているフォルダー ("3D"、"UI"、"物理/2D"; 空 = 一番上)。ActionPicker: リストの中で開閉する
    // フォルダー。ActionMenu: フォルダーの名前の行が、中にあるもののメニューを開く (マウスが行の上に
    // あるとき、横に): 追加 ▸ 3D オブジェクト ▸ 箱。
    std::wstring folder;
};
std::wstring ShortcutText(const Action& a);  // "Ctrl+D"、"Delete"、"" (ショートカットなし)
// `popup` を (開いていれば) `actions` と一緒にフレームの最後に描き (パネルに切られない)、
// 選ばれた行を実行する。リストはメニューが閉じるまで呼び出し側が持っておく。フォルダーの
// アクション (Action::folder) はそれぞれ別のメニューに入る (Esc で最後に開いたものを閉じる。
// 全部の外をクリックすると全部閉じる)。
void ActionMenu(ui::Context& c, Popup& popup, const std::vector<Action>& actions);

// アクションを 1 つ選ぶパネル (インスペクターの 部品を足す): 上に `title` と ×、検索ボックス、
// その下にフォルダー (Action::folder) ごとのアクション。各フォルダーはその行で開閉する。
// ActionMenu のようにフレームの最後に描き、選ばれたアクションを実行して閉じる; × / Esc /
// 外のクリックでも閉じる。Enter は入力した文字で見つかった最初のものを実行する。上の部分
// (タイトルのある所) をドラッグすると別の所へ動かせる。どのフォルダーが開いているか、どこへ
// 動かしたかは呼び出し側が持つ (開くたびに引き継ぐ); 検索は開くたびに空から始まる。メニューより
// 広くて他のパネルを覆う: 開いている間は、その下の画面が入力を受けないようにする (EditorScreen がやる)。
struct PickerState {
    std::string search;
    std::set<std::wstring> open;  // 開いているフォルダーのパス (最初は全部閉じている)
    bool placed = false;          // 上の部分で動かした: x, y に開く (そうでなければ呼び出し側が開く所)
    float x = 0, y = 0;
    bool dragging = false;        // 上の部分をつかんでいる: その中のマウスの位置
    float grabX = 0, grabY = 0;
};
void ActionPicker(ui::Context& c, Popup& popup, PickerState& state, const std::vector<Action>& actions, std::wstring title,
                  float width = 320.f);

// 1 つのものを選ぶリスト。上に検索ボックスがある (参照フィールドの選択肢)。MenuPopup のように
// フレームの最後に呼ぶ。`search` は入力された文字 (呼び出し側が持ち、それに合う行だけを並べる)。
// クリックされた行 - または Enter なら最初の行 - を返して閉じる; なければ -1。
struct PickRow {
    std::wstring name;
    std::wstring detail;  // 薄く、右に (どこにあるか)
    bool hasIcon = false;
    Icon icon = Icon::File;
};
// `picture` (省略可): 行 i の小さな絵。見えている行についてだけ呼ばれる (null: その行のアイコン);
// `rowHeight` でそのための場所を作る。
int PickerPopup(ui::Context& c, Popup& popup, std::string_view id, std::string& search, std::span<const PickRow> rows,
                float width = 320.f, float rowHeight = 26.f, const std::function<ID2D1Bitmap*(size_t row, float size)>& picture = {});

// --- あるパネルから別のパネルへものをドラッグする (アセットや階層の項目を インスペクターの参照
// フィールドへ)。マウスは 1 つなので、プログラム全体で一度に 1 つのものだけ。元のパネルが、ドラッグが
// 続く間これを設定し、ボタンを離したときに終わらせる; 受け取る所は DropTarget に聞く。マウスを
// 捕まえているウィンドウがドラッグを見る; マウスがプログラムの別のウィンドウの上にあるときは、
// エディターがそのウィンドウに場所を伝え (SetDragElsewhere)、そこの受け取る所も光って
// ものを受け取る (独立したウィンドウに出したパネル)。
struct DragThing {
    std::string kind;                 // それが何か: "asset" (values: id) / "item" (values: 階層の項目の id)
    std::vector<std::string> values;  // 1 つ以上
};
void SetDragThing(DragThing thing);
// このフレームの最後にドラッグを終わらせる: 同じフレームで後から描く場所には、離されたことが見える。
void EndDragThing(ui::Context& c);
const DragThing* DraggedThing();
// ドロップできる場所。`accepts` が受け付けるものが `r` の上にある間、縁取る; そこでマウスが
// 離されたらそのものを返す (そうでなければ null)。
const DragThing* DropTarget(ui::Context& c, const ui::Rect& r, const std::function<bool(const DragThing&)>& accepts);
// マウスの下の DropTarget がこのフレームでものを受け取ったか (マウスの横のタグがそれを示す)。
// 読むと忘れる - 1 フレームに 1 回、最後に呼ぶ。
bool TakeDragAccepted();
// ドラッグしているものが別のウィンドウの上にある (`target`: その context; x, y: そこでのマウスの
// 位置、DIP); `released`: そこで離した。target が null なら: 私たちのどのウィンドウの上でもない。
void SetDragElsewhere(const ui::Context* target, float x, float y, bool released);
// 今のドラッグの場所。伝えられたウィンドウ用 (SetDragElsewhere): なければ null。
const ui::Context* DragElsewhereTarget();
// ウィンドウのフレームの最後: そこへ渡されたドロップは使い切る (受け取られたかどうかに関わらず)。
void EndDragFrame(ui::Context& c);

// --- モーダルダイアログ -------------------------------------------------------
// 画面を暗くして、ダイアログの箱とタイトルを描き、内容の領域を返す。
ui::Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title);

}  // namespace rm::editor::w
