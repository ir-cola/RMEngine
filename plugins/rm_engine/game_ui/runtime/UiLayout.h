#pragma once
// ゲーム UI - UI のあらゆるものが画面のどこにあるか、マウスとキーがそれに何をするか。純粋 (描画も
// ファイルも扱わない)。エディターのゲームビュー・UI ビュー・書き出したゲームはこれで UI を配置するので、
// 描かれた所がそのままクリックされる所になる (憲法 3)。tests/TestGameUi.cpp でテストする。
//
//   画面 (DIP、Y は下向き) ─ セーフエリア ─▶ キャンバスの箱 ─ ÷ 倍率 ─▶ キャンバスの単位 (基準の解像度)
//   UI のもの: 親の単位での箱 (アンカー + 位置 + 大きさ + 中心)。中心まわりに回る
//   (子も一緒に回る) - toScreen がその単位を画面に写す。

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rm::game_ui {

struct UiBox {
    float x = 0, y = 0, w = 0, h = 0;
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    bool Contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h; }
};

// 画面の 2D アフィン変換 (Y は下向き): p' = (a x + c y + tx, b x + d y + ty)。
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};
Affine operator*(const Affine& m, const Affine& n);  // n の後に m
Vec2 Apply(const Affine& m, Vec2 p);
std::optional<Affine> Invert(const Affine& m);
Affine Translate(Vec2 t);
Affine Scaling(float sx, float sy);
Affine Rotation(float degrees);  // 画面上で時計回り
// `pivot` を中心に `degrees` だけ回し、`scale` 倍にしたもの。
Affine About(Vec2 pivot, float degrees, float scale);

// 親の中のアンカーの位置。左上から 0..1。min = max なら点。
void AnchorRange(UiAnchor anchor, Vec2& min, Vec2& max);
// UI のものの、親の箱の中での箱 (どちらも親の単位)。
UiBox PlaceBox(const UiRect& r, const UiBox& parent);
// 別のアンカーでの同じ箱: 位置と大きさを計算し直す (インスペクターのアンカーは、今の親のまま
// 画面上でそれを同じ場所に保つ)。
UiRect Reanchored(const UiRect& r, UiAnchor to, const UiBox& parent);

// --- UI タブのハンドル (純粋: ドラッグが UI の配置に何をするか) ------------------------------
// 箱の 8 つのハンドル: 角と辺の真ん中 (左上から時計回り)。
enum class UiHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
// ハンドルが箱のどの辺を動かすか: -1 は左 / 上の辺、1 は右 / 下の辺、0 は動かさない。
Vec2 HandleSides(UiHandle h);
// `along` だけドラッグしたハンドル (そのものの単位で、回った軸に沿って): 持っている辺が動き、
// 反対側の辺はそのまま (回転も同じ)。`keepAspect` (角): 幅と高さの比を保つ。
// 箱は `minSize` を下回らない。
UiRect ResizeByHandle(const UiRect& r, const UiBox& parent, UiHandle h, Vec2 along, bool keepAspect, float minSize = 1.f);
// マウスを `center` のまわりで `from` から `to` へドラッグした後の回転 (すべて同じ空間、Y は下向き:
// 時計回りが +)。`step` > 0: その刻み (度) に丸める。
float RotateByDrag(float startDegrees, Vec2 center, Vec2 from, Vec2 to, float step = 0);
// その 中心 (ピボット) を `pivot` (箱の 0..1) へ動かしたもの。そのものは画面上で同じ場所に
// とどまる (位置を計算し直す: 回転は新しい 中心 のまわり)。
UiRect MovePivot(const UiRect& r, const UiBox& parent, Vec2 pivot);
// 1 つの軸でのスナップ: `edges` (動く箱の辺と真ん中) の 1 つを、`reach` 以内で一番近い `targets`
// (ほかのものの辺と真ん中) に合わせるずれ。近いものがなければ 0。
// `line`: 吸着した先。
float SnapShift(std::span<const float> edges, std::span<const float> targets, float reach, std::optional<float>* line = nullptr);
// `value` を `step` の一番近い倍数に合わせるずれ (step <= 0: なし)。
float GridShift(float value, float step);
// アンカーの選択表 (4 x 4、Unity と同じ): 行は上 / 中 / 下 / 上下に引き伸ばし、列は左 / 中 / 右 /
// 左右に引き伸ばし。
UiAnchor AnchorAt(int row, int column);
void AnchorCell(UiAnchor anchor, int& row, int& column);
// 別の親の下の同じ箱: 位置と大きさ (アンカーはそのまま)。中心 が (新しい親の単位で) `pivotAt` に
// 来るようにし、大きさは元のまま、`rotation` だけ回す。
UiRect Rehomed(const UiRect& r, const UiBox& oldParent, const UiBox& newParent, Vec2 pivotAt, float rotation);
// いくつかのものを揃える (箱は 1 つの空間で): それぞれがどれだけ動くか。
enum class UiAlign { Left, CenterX, Right, Top, CenterY, Bottom };
// 全体の一番外側の辺へ (Left: 一番左 ...)、または全体の真ん中へ。
std::vector<Vec2> AlignShifts(std::span<const UiBox> boxes, UiAlign how);
// x 方向 (`across`) または y 方向に、並んでいる順で間を等しくする。最初と最後は動かない。
std::vector<Vec2> DistributeShifts(std::span<const UiBox> boxes, bool across);
// 親の単位での 中心 (回る所)。
Vec2 PivotPoint(const UiRect& r, const UiBox& box);
// `area` DIP の領域の中の `reference` 単位のキャンバスで、キャンバスの 1 単位が画面の何 DIP か。
float CanvasScale(UiScaleMode mode, Vec2 reference, Vec2 area);
// セーフエリアの余白を除いた画面 (0 より小さくはならない)。
UiBox SafeBox(Vec2 screen, Vec2 topLeft, Vec2 bottomRight);

// 配置された UI のもの 1 つ。
struct UiNode {
    int entity = 0;
    int canvas = 0;       // そのキャンバスのエンティティ
    int parent = -1;      // UiTree::nodes の番号。キャンバスは -1
    UiBox box;            // 親の単位で (キャンバス: それ自身の全体の箱)
    Affine toScreen;      // 箱の単位 -> 画面 DIP (キャンバスの倍率と親の回転)
    Affine self;          // それ自身の単位 (子には: 回転込みの箱) -> 画面 DIP
    const UiCanvas* canvasPart = nullptr;  // キャンバス
    const UiRect* place = nullptr;
    const UiText* text = nullptr;
    const UiImage* image = nullptr;
    const UiButton* button = nullptr;
    const UiBar* bar = nullptr;
    const UiStyle* style = nullptr;  // 見た目 (UI-5)
    const UiVideo* video = nullptr;  // 動画 (UI-5)
    float alpha = 1;       // 薄くなった割合 (自分の UiLive と親の): 描くものにかかる
    bool closing = false;  // オフになって消えていく (消えるとき)、またはそうなっているものの下: 描くがクリックはされない
};

// 表示されているもの全部を奥から手前へ: キャンバスは 手前の順 (次に 階層の順)、各キャンバスの中は
// そのものたち (深さ優先。兄弟は 手前の順、次に 階層の順)。
struct UiTree {
    std::vector<UiNode> nodes;
    const UiNode* Find(int entity) const;
};

struct UiScreen {
    Vec2 size{1280, 720};  // ゲームの画面 (DIP)
    Vec2 safeTopLeft{0, 0}, safeBottomRight{0, 0};
};

// `screen` 上の `scene` の UI: オンのキャンバス (その親も) と、その下でオンのもの。`alsoShow`:
// これらのエンティティを持つキャンバスは、それ (や持ち主) がオフでも配置する - UI ビューは作っている
// ウィンドウを見せるため。キャンバスの下のキャンバスは、そのもののひとつ (独立したウィンドウではない)。
UiTree BuildUi(const game_production::Scene& scene, const UiScreen& screen, std::span<const int> alsoShow = {});
// ツリーのキャンバス 1 つだけ (とその中身): UI タブは作っているウィンドウだけを見せる。
UiTree OnlyCanvas(const UiTree& tree, int canvas);

// --- マウスとキー --------------------------------------------------------------------------
// 点 (画面 DIP) がノードの箱の上にある (回転込み)。
bool Hits(const UiNode& n, Vec2 screenPoint);
// `p` の下にある、マウスを受けるいちばん手前のものの エンティティ (画像・バー・ボタン。テキストは
// 受けない: ラベルがボタンを隠すことはない)。0: なし。
int TopAt(const UiTree& tree, Vec2 p);
// `entity` 自身か、それを持つ (一番近い) ボタン。0: なし。
int ButtonOf(const UiTree& tree, int entity);
// マウスが乗っているボタン: TopAt のもので、押せるとき。0: なし (押せないボタンも下のものを隠す)。
int ButtonAt(const UiTree& tree, Vec2 p);

struct PointerState {
    int pressed = 0;  // 左ボタンが押されたボタン (0: なし)
};
struct PointerResult {
    int hovered = 0;  // マウスの下
    int held = 0;     // 押されて、まだその上にある (押された見た目で描く)
    int clicked = 0;  // 押されたボタンの上で離された: OnClick
};
// マウスの 1 フレーム: 今 `down` で押している、このフレームに `wentDown` / `wentUp`。1 フレームに
// 両方 (フレームより速いクリック: タップ) なら、マウスの下のボタンのクリックになる。
PointerResult StepPointer(const UiTree& tree, std::optional<Vec2> p, bool down, bool wentDown, bool wentUp, PointerState& state);

// キーが効くキャンバス: キーとパッドで選ぶ を持ち、押せるボタンがある一番手前のもの。0: なし。
int NavigationCanvas(const UiTree& tree);
// `canvas` で最初に選ぶボタン: `preferred` がそのボタンのうち押せるものならそれ、なければ
// 左上のもの (上が先)。0: なし。
int FirstFocus(const UiTree& tree, int canvas, int preferred);
// `canvas` の `current` から、ある向き (dx, dy: -1 / 0 / 1、画面 Y は下向き) の次のボタン: その向きに
// 真ん中があるもののうち一番近いもの (まっすぐ行くほうが、脇へ行くより優先)。なければ `current`
// そのもの。
int MoveFocus(const UiTree& tree, int canvas, int current, int dx, int dy);
// `current` からキー / パッドの 1 歩が行く先 - UI サービスが尋ねるもの: そのボタン自身のリンクの
// その向き (上 / 下 / 左 / 右で移る先: 配置と配線)。`canvas` の押せるボタンならそれ。そうでなければ
// MoveFocus (画面でその向きの一番近いボタン。自分で見つける: 設定は要らない)。
int NextButton(const UiTree& tree, int canvas, int current, int dx, int dy);

// --- ボタンの見た目 -----------------------------------------------------------------------
// 状態ごとの見た目 (フォーカスは Hover と同じ。押せない: 使えない)。縁取りのない状態は
// outlineAmount 0 (幅と色はその状態自身のもの - EaseLook は最後のものを保つ)。
UiButtonLook ButtonTarget(const UiButton& b);
// `seconds` かけて変わるとき、`dt` の 1 フレームで目標までの道のりのどれだけ進むか (指数的:
// 0.1 秒の 1 フレームの後と 0.01 秒の 10 フレームの後で同じ)。seconds <= 1e-4: 全部。
float EaseAmount(float dt, float seconds);
// `target` へ向かう 1 フレーム: すべての値を `amount` だけ動かし、1e-4 より近ければ目標に置く
// (髪の毛ほどの縁取りが残らないように)。縁取りのない (または置き換える色のない) 目標では、縁取りの
// (色の) 幅と色はそのままで、量だけが下がる - フェードであって、細い線にはならない。
UiButtonLook EaseLook(const UiButtonLook& now, const UiButtonLook& target, float amount);
// 今描くもの: ゲームの Play 中はサービスのなめらかにした見た目、そうでなければ状態そのもの。
UiButtonLook ButtonNow(const UiButton& b);
float ButtonScale(const UiButton& b);  // ButtonNow の
// ボタン自身の画像の、状態ごとの絵 (ボタンの画像の差し替え。画像なしの押した状態: Hover のもの)。
// その状態に画像がなければ `own`。
const AssetRef& ButtonImage(const UiButton& b, const AssetRef& own);

// ボタンが色をどう塗るか: 掛け合わせ、次に置き換える色 (r g b) へ寄せる。
struct UiPaint {
    Color tint{1, 1, 1, 1};
    Color replace{1, 1, 1, 1};
    float amount = 0;
};
UiPaint PaintOf(const UiButtonLook& look);
Color Paint(Color c, const UiPaint& p);
bool IsPlain(const UiPaint& p);  // 何も変えない
// ボタンのまわりに描く縁取り: 状態のものとそのもの自身のものを混ぜる (`ownWidth` 0: なし - その
// とき状態の幅は残り、色は薄くなって消える)。
struct UiStroke {
    float width = 0;
    Color color{0, 0, 0, 0};
};
UiStroke ButtonOutline(const UiButtonLook& look, UiStroke own = {});
Color Multiply(Color a, Color b);
// 文字 `start` から `length` 個 (ユーザーが数えるとおりに: BMP の外の 1 文字は UTF-16 の 2 単位だが
// 1 文字) を `text` の UTF-16 単位にしたもの。`text` の内側に収める。
struct UiTextRange {
    uint32_t start = 0, length = 0;
};
UiTextRange TextRange(std::wstring_view text, int start, int length);
// `imagePx` ピクセルの絵のうち 切り出し が取る部分 (ピクセル。大きさ 0: 端まで。絵の内側に収める)。
UiBox CropRect(Vec2 imagePx, Vec2 offset, Vec2 size);
// `seconds` での 虹色: 色相が `phase` から 1 秒に `speed` 回まわる。alpha は 1。
Color RainbowColor(const UiRainbow& r, double seconds);
// 立体 を描く (決めるのはここだけ: 仕様 §2-9) - そして押し込む (へこませる、またはいま押されている
// ボタンの 押すとへこむ)。
bool BevelShown(const UiStyle& s);
bool BevelInset(const UiStyle& s, const UiButton* button);
// 描かれる箱の角 (辺の半分を超えない)。
float Rounding(const UiStyle& s, const UiBox& box);
// 箱の中のバーの塗られた部分 (余白を除く)。
UiBox BarFill(const UiBar& bar, const UiBox& box);

}  // namespace rm::game_ui
