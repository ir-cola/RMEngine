#pragma once
// ゲーム UI - where every UI thing is on the screen, and what the mouse and the keys do to it. Pure (no
// drawing, no files): the editor's game view, the UI view and the exported game lay the UI out with
// this, so what is drawn is what is clicked (rule 3). Tested in tests/TestGameUi.cpp.
//
//   screen (DIPs, Y down) ─ safe area ─▶ a canvas's box ─ ÷ scale ─▶ the canvas's units (基準の解像度)
//   a UI thing: its box in its parent's units (アンカー + 位置 + 大きさ + 中心), turned about its 中心
//   (its children turn with it) - toScreen maps its units to the screen.

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

// A 2D affine map of the screen (Y down): p' = (a x + c y + tx, b x + d y + ty).
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};
Affine operator*(const Affine& m, const Affine& n);  // m after n
Vec2 Apply(const Affine& m, Vec2 p);
std::optional<Affine> Invert(const Affine& m);
Affine Translate(Vec2 t);
Affine Scaling(float sx, float sy);
Affine Rotation(float degrees);  // clockwise on the screen
// Turned by `degrees` and scaled by `scale` about `pivot`.
Affine About(Vec2 pivot, float degrees, float scale);

// The anchor's place in the parent, 0..1 from its top left: min = max for a point.
void AnchorRange(UiAnchor anchor, Vec2& min, Vec2& max);
// A UI thing's box in its parent's box (both in the parent's units).
UiBox PlaceBox(const UiRect& r, const UiBox& parent);
// The same box with another anchor: 位置 and 大きさ worked out again (the inspector's アンカー keeps the
// thing where it is on the screen, as the parent is now).
UiRect Reanchored(const UiRect& r, UiAnchor to, const UiBox& parent);

// --- The UI tab's handles (pure: what a drag does to UI の配置) --------------------------------
// The 8 handles of a box: its corners and the middles of its sides (clockwise from the top left).
enum class UiHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
// Which way a handle moves the box's sides: -1 the left / top side, 1 the right / bottom one, 0 none.
Vec2 HandleSides(UiHandle h);
// A handle dragged by `along` (in the thing's own units, along its own - turned - axes): the sides it
// holds move, the opposite ones stay where they are (its turn too). `keepAspect` (a corner): the width
// and height keep their ratio. The box never goes below `minSize`.
UiRect ResizeByHandle(const UiRect& r, const UiBox& parent, UiHandle h, Vec2 along, bool keepAspect, float minSize = 1.f);
// The turn after dragging the mouse from `from` to `to` around `center` (all in one space, Y down: a
// clockwise sweep is +). `step` > 0: rounded to steps of it (degrees).
float RotateByDrag(float startDegrees, Vec2 center, Vec2 from, Vec2 to, float step = 0);
// Its 中心 (pivot) moved to `pivot` (0..1 of its box), the thing staying where it is on the screen
// (its 位置 worked out again: the turn is about the new 中心).
UiRect MovePivot(const UiRect& r, const UiBox& parent, Vec2 pivot);
// Snapping along one axis: the shift that puts one of `edges` (a moving box's sides and middle) on the
// nearest of `targets` (other things' sides and middles) within `reach`; 0 when none is that near.
// `line`: the target it snapped to.
float SnapShift(std::span<const float> edges, std::span<const float> targets, float reach, std::optional<float>* line = nullptr);
// The shift that puts `value` on the nearest multiple of `step` (step <= 0: none).
float GridShift(float value, float step);
// The anchor picker (4 x 4, as Unity's): rows top / middle / bottom / stretched up-down, columns left /
// middle / right / stretched across.
UiAnchor AnchorAt(int row, int column);
void AnchorCell(UiAnchor anchor, int& row, int& column);
// The same box under another parent: 位置 and 大きさ (its anchor kept) so that its 中心 is at `pivotAt`
// (in the new parent's units), as big as it was, turned `rotation`.
UiRect Rehomed(const UiRect& r, const UiBox& oldParent, const UiBox& newParent, Vec2 pivotAt, float rotation);
// Lining several things up (their boxes in one space): how far each moves.
enum class UiAlign { Left, CenterX, Right, Top, CenterY, Bottom };
// To the outermost side of them all (Left: the leftmost ...), or to the middle of them all.
std::vector<Vec2> AlignShifts(std::span<const UiBox> boxes, UiAlign how);
// Equal gaps between them along x (`across`) or y, in their order along it; the first and the last stay.
std::vector<Vec2> DistributeShifts(std::span<const UiBox> boxes, bool across);
// Its 中心 in its parent's units (where it turns).
Vec2 PivotPoint(const UiRect& r, const UiBox& box);
// How many screen DIPs one canvas unit is, for a canvas of `reference` units in an area of `area` DIPs.
float CanvasScale(UiScaleMode mode, Vec2 reference, Vec2 area);
// The screen without the safe area's margins (never less than nothing).
UiBox SafeBox(Vec2 screen, Vec2 topLeft, Vec2 bottomRight);

// One UI thing laid out.
struct UiNode {
    int entity = 0;
    int canvas = 0;       // its canvas's entity
    int parent = -1;      // index in UiTree::nodes; -1 for a canvas
    UiBox box;            // in its parent's units (a canvas: its own whole box)
    Affine toScreen;      // the units its box is in -> screen DIPs (the canvas's scale, the parents' turns)
    Affine self;          // its own units (for its children: box with its turn) -> screen DIPs
    const UiCanvas* canvasPart = nullptr;  // a canvas
    const UiRect* place = nullptr;
    const UiText* text = nullptr;
    const UiImage* image = nullptr;
    const UiButton* button = nullptr;
    const UiBar* bar = nullptr;
    const UiStyle* style = nullptr;  // 見た目 (UI-5)
    const UiVideo* video = nullptr;  // 動画 (UI-5)
    float alpha = 1;       // faded (its own UiLive and its parents'): what it draws is multiplied by it
    bool closing = false;  // switched off and going (消えるとき) - or under one that is: drawn, never clicked
};

// Everything shown, back to front: canvases by 手前の順 (then the 階層's order), each canvas then its
// things (depth first; siblings by 手前の順, then the 階層's order).
struct UiTree {
    std::vector<UiNode> nodes;
    const UiNode* Find(int entity) const;
};

struct UiScreen {
    Vec2 size{1280, 720};  // the game's screen (DIPs)
    Vec2 safeTopLeft{0, 0}, safeBottomRight{0, 0};
};

// The UI of `scene` on `screen`: the canvases switched on (with their parents), and what is switched on
// under them. `alsoShow`: a canvas holding one of these entities is laid out even when it (or what holds
// it) is switched off - the UI view shows the window being made. A canvas under another canvas is one of
// its things (not a window of its own).
UiTree BuildUi(const game_production::Scene& scene, const UiScreen& screen, std::span<const int> alsoShow = {});
// Only one canvas of a tree (and what is in it): the UI tab shows the window being made alone.
UiTree OnlyCanvas(const UiTree& tree, int canvas);

// --- The mouse, the keys --------------------------------------------------------------------
// The point (screen DIPs) is on the node's box (turns included).
bool Hits(const UiNode& n, Vec2 screenPoint);
// The entity of the frontmost thing under `p` that takes the mouse (an image, a bar or a button - a text
// does not: a label never hides its button); 0: none.
int TopAt(const UiTree& tree, Vec2 p);
// The button that is `entity` or holds it (its nearest), 0: none.
int ButtonOf(const UiTree& tree, int entity);
// The button the mouse is on: TopAt's, when it can be pressed; 0: none (a button that cannot be pressed
// still hides what is under it).
int ButtonAt(const UiTree& tree, Vec2 p);

struct PointerState {
    int pressed = 0;  // the button the left button went down on (0: none)
};
struct PointerResult {
    int hovered = 0;  // under the mouse
    int held = 0;     // pressed and still under it (drawn pressed)
    int clicked = 0;  // let go on the button it went down on: OnClick
};
// One frame of the mouse: `down` held now, `wentDown` / `wentUp` this frame. Both in one frame (a click
// quicker than a frame: a tap) is a click on the button under the mouse.
PointerResult StepPointer(const UiTree& tree, std::optional<Vec2> p, bool down, bool wentDown, bool wentUp, PointerState& state);

// The canvas the keys work in: the frontmost with キーとパッドで選ぶ and a button that can be pressed; 0: none.
int NavigationCanvas(const UiTree& tree);
// The button to start from in `canvas`: `preferred` when it is one of its buttons that can be pressed,
// else the one at the top left (top first). 0: none.
int FirstFocus(const UiTree& tree, int canvas, int preferred);
// The next button of `canvas` from `current` in a direction (dx, dy: -1 / 0 / 1, screen Y down): the
// nearest one whose middle lies that way (going straight counts more than going aside). `current` itself
// when there is none.
int MoveFocus(const UiTree& tree, int canvas, int current, int dx, int dy);
// Where the keys' / a pad's step from `current` goes - what the UI service asks: the button's own link that
// way (上 / 下 / 左 / 右で移る先: 配置と配線), when it is a button of `canvas` that can be pressed; else
// MoveFocus (the nearest button that way on the screen, found by itself: nothing to set).
int NextButton(const UiTree& tree, int canvas, int current, int dx, int dy);

// --- How a button looks -----------------------------------------------------------------------
// The look of its state (focused looks as Hover; not pressable: 使えない). A state with no outline has
// outlineAmount 0 (its width and colour are the state's own - EaseLook keeps the last ones).
UiButtonLook ButtonTarget(const UiButton& b);
// How much of the way to the target one frame of `dt` goes, for a change taking `seconds` (exponential:
// the same after one frame of 0.1 s as after ten of 0.01 s). seconds <= 1e-4: all of it.
float EaseAmount(float dt, float seconds);
// One frame toward `target`: every value moved by `amount`, put on the target when nearer than 1e-4 (no
// hair-thin outline left over). A target with no outline (or no replacing colour) keeps the outline's
// (the colour's) width and colour as they are, its amount alone falling - a fade, never a thinner line.
UiButtonLook EaseLook(const UiButtonLook& now, const UiButtonLook& target, float amount);
// What is drawn now: the service's eased look while the game plays, else its state's own.
UiButtonLook ButtonNow(const UiButton& b);
float ButtonScale(const UiButton& b);  // ButtonNow's
// The picture of the button's own image for its state (ボタンの画像の差し替え; pressed without one: the hover's);
// `own` when the state has none.
const AssetRef& ButtonImage(const UiButton& b, const AssetRef& own);

// How a colour is painted by a button: multiplied, then moved toward the replacing colour (r g b).
struct UiPaint {
    Color tint{1, 1, 1, 1};
    Color replace{1, 1, 1, 1};
    float amount = 0;
};
UiPaint PaintOf(const UiButtonLook& look);
Color Paint(Color c, const UiPaint& p);
bool IsPlain(const UiPaint& p);  // changes nothing
// The outline drawn round a button: the state's mixed with the thing's own (`ownWidth` 0: none - then the
// state's width stays and its colour fades out).
struct UiStroke {
    float width = 0;
    Color color{0, 0, 0, 0};
};
UiStroke ButtonOutline(const UiButtonLook& look, UiStroke own = {});
Color Multiply(Color a, Color b);
// Characters `start`, `length` of them (as the user counts them: one outside the BMP - two UTF-16 units -
// is one) as UTF-16 units of `text`, kept inside it.
struct UiTextRange {
    uint32_t start = 0, length = 0;
};
UiTextRange TextRange(std::wstring_view text, int start, int length);
// The part of a picture of `imagePx` pixels that 切り出し takes (pixels; a size of 0: to its edge; kept
// inside the picture).
UiBox CropRect(Vec2 imagePx, Vec2 offset, Vec2 size);
// 虹色 at `seconds`: its hue going round `speed` times a second from `phase`; alpha 1.
Color RainbowColor(const UiRainbow& r, double seconds);
// The 立体 is drawn (one place decides it: spec §2-9) - and pressed in (へこませる, or 押すとへこむ on a
// button pressed now).
bool BevelShown(const UiStyle& s);
bool BevelInset(const UiStyle& s, const UiButton* button);
// The corner of a box as drawn (never more than half its side).
float Rounding(const UiStyle& s, const UiBox& box);
// The filled part of a bar inside its box (padding taken off).
UiBox BarFill(const UiBar& bar, const UiBox& box);

}  // namespace rm::game_ui
