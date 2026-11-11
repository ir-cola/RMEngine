#pragma once
// 2D 空間 - the gizmo of a 2D view: the arrows, the ring and the boxes that move, turn and scale a
// thing when they are dragged. Only the math lives here: where the handles are on the screen (the same
// shapes are drawn and hit-tested - rule 3), which one is under the mouse, and what a drag does to a
// Transform 2D. Drawing it is the view's. Used by every software that edits a 2D space (the game's 2D
// scene view today, the illustration software later). Pure - tested in tests/TestSpace2d.cpp.
//
//   gizmo2d::Frame f = gizmo2d::MakeFrame(world, tool, space);
//   gizmo2d::Handle h = gizmo2d::HitTest(tool, f, camera, rect, mouse);
//   gizmo2d::Drag drag = gizmo2d::BeginDrag(tool, h, f, camera, rect, mouse);   // on press
//   gizmo2d::Change ch = gizmo2d::Update(drag, camera, rect, mouse, snap);     // every move
//   transform = gizmo2d::Apply(start, parentWorld, startWorld, ch);          // (or with &f: about its middle)
//
// The gizmo keeps one size on the screen (DIPs) however near the camera is.

#include "rm_engine/space2d/runtime/View2D.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace rm::space2d::gizmo2d {

RM_ENUM()
enum class Tool { Move, Rotate, Scale };
// The directions of the arrows: the thing's own axes or the world's. (Scale is always along the thing's
// own axes - a Transform 2D's scale is.)
RM_ENUM()
enum class Space { Local, World };
// Where the gizmo sits when several things are selected, and what they turn / scale about: each about
// its own place (the gizmo on the one selected last), or all about the middle of them (the gizmo there).
RM_ENUM()
enum class Pivot { Each, Center };

enum class Handle : uint8_t {
    None,
    X, Y,   // an arrow / a scale axis
    Free,   // the square in the middle: moves freely / scales evenly
    Ring,   // rotate: the circle
};

inline constexpr float kArrowLength = 80.f;  // DIPs on the screen
inline constexpr float kRingRadius = 70.f;
inline constexpr float kCenterHalf = 9.f;    // half the middle square
inline constexpr float kHitSlop = 7.f;       // how near the mouse must be to a line
inline constexpr float kScalePerDip = 0.01f; // the middle square of 拡大縮小: 100 DIPs right / up = twice

// The gizmo as it is placed: at `origin` (in the plane), with two unit axes (in the plane).
struct Frame {
    Vec2 origin{0, 0};
    std::array<Vec2, 2> axes{Vec2{1, 0}, Vec2{0, 1}};
};
Frame MakeFrame(const Mat3& world, Tool tool, Space space);
// The middle of several places (中心), with the axes of `axesFrom` (the one selected last).
Frame CenterFrame(std::span<const Vec2> places, const Frame& axesFrom);

// On the screen: the gizmo's middle and the tip of each arrow.
struct ScreenShape {
    Vec2 origin;
    std::array<Vec2, 2> tips;
};
ScreenShape ShapeOf(const Frame& f, const Camera2D& cam, const ScreenRect& r);

// The handle under the mouse (screen DIPs).
Handle HitTest(Tool tool, const Frame& f, const Camera2D& cam, const ScreenRect& r, Vec2 mouse);

struct Drag {
    Tool tool = Tool::Move;
    Handle handle = Handle::None;
    Frame frame;
    Vec2 startWorld{0, 0};   // the mouse in the plane when the drag began
    Vec2 startScreen{0, 0};  // ... and on the screen
    float lastRaw = 0;       // rotate: the angle of the mouse last time (to count whole turns)
    float turned = 0;        // rotate: degrees so far (can go past 180)
};
// A drag of `handle` (not None), starting where the mouse is.
Drag BeginDrag(Tool tool, Handle handle, const Frame& f, const Camera2D& cam, const ScreenRect& r, Vec2 mouse);

// Steps (Ctrl): moves by `move` units along each axis, turns by `degrees`, scales by `scale` of the start.
struct Snap {
    bool on = false;
    float move = 0.25f;
    float degrees = 15.f;
    float scale = 0.1f;
};

// What the drag does so far (from its start).
struct Change {
    Vec2 move{0, 0};      // in the plane (world)
    float degrees = 0;    // counter-clockwise
    Vec2 scale{1, 1};     // times the start's, along the thing's own axes
};
Change Update(Drag& drag, const Camera2D& cam, const ScreenRect& r, Vec2 mouse, const Snap& snap);

// The Transform 2D after the change (from how it was when the drag began): a move is in the world and
// goes through the parent's inverse; a turn is turned over under a mirrored parent. `pivot` (中心): a
// turn also carries its place round the gizmo's middle, a scale moves it away from the middle along the
// gizmo's axes.
Transform2D Apply(const Transform2D& start, const Mat3& parentWorld, const Mat3& startWorld, const Change& change,
                  const Frame* pivot = nullptr);

}  // namespace rm::space2d::gizmo2d
