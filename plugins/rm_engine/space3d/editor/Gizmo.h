#pragma once
// 3D 空間 - the gizmo: the arrows, rings and boxes that move, turn and scale a thing when they are
// dragged in a 3D view. Only the math lives here: where the handles are (the same shapes are drawn
// and hit-tested - rule 3), which one is under the mouse, and what a drag does to a Transform.
// Drawing it is the view's. Used by every software that edits a 3D space (the game's scene view
// today, the 3D model software later). Pure - tested in tests/TestSpace3d.cpp.
//
//   auto f = gizmo::MakeFrame(world, tool, space, camera, viewHeight);     // where it is
//          (or MakeFrame(center, axes, ...): in the middle of several things, Pivot::Center)
//   gizmo::Handle h = gizmo::HitTest(tool, *f, screen, camera.Eye(), mouse);
//   auto drag = gizmo::BeginDrag(tool, h, *f, screen, camera.Eye(), mouse);  // on press
//   auto change = gizmo::Update(*drag, screen, mouse, snap);                // every move
//   transform = gizmo::Apply(start, parentWorld, startWorld, *change, space, transform.rotation, pivot);

#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rm::space3d::gizmo {

RM_ENUM()
enum class Tool { Move, Rotate, Scale };
// The directions of the move / rotate handles: the thing's own axes or the world's. (Scale is
// always along the thing's own axes - a Transform's scale is.)
RM_ENUM()
enum class Space { Local, World };
// Where the gizmo sits when several things are selected, and what they turn / scale about: each about
// its own place (the gizmo on the one selected last), or all about the middle of them (the gizmo there).
RM_ENUM()
enum class Pivot { Each, Center };

enum class Handle : uint8_t {
    None,
    X, Y, Z,       // an arrow / ring / scale axis
    YZ, ZX, XY,    // a square that moves in a plane (named by the two axes it moves along)
    Uniform,       // the box in the middle that scales evenly
    View,          // rotate: the outer ring facing the eye - turns about the line of sight
    Free,          // rotate: inside the ball (not on a ring) - turns freely like a trackball
};
// The axis of a handle: X / Y / Z -> 0 / 1 / 2, a plane -> the axis it does not move along, else -1.
int AxisOf(Handle h);

// The gizmo as it is placed: at `origin`, three unit axes (world), arrows `size` long (world units)
// which is `pixels` on the screen (it keeps one size on screen whatever its distance).
struct Frame {
    Vec3 origin{0, 0, 0};
    std::array<Vec3, 3> axes{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    float size = 1.f;
    float pixels = 90.f;
};
std::array<Vec3, 3> AxesOf(const Mat4& world, Tool tool, Space space);
// The gizmo of a thing placed by `world`; nullopt when the thing is behind the eye.
std::optional<Frame> MakeFrame(const Mat4& world, Tool tool, Space space, const OrbitView& view, float viewHeight,
                               float pixels = 90.f);
// The gizmo at `origin` with these axes (the middle of several things, the axes of one of them).
std::optional<Frame> MakeFrame(Vec3 origin, const std::array<Vec3, 3>& axes, const OrbitView& view, float viewHeight,
                               float pixels = 90.f);

// A view on the screen: the picture's rectangle (DIPs, y down) and the camera's matrix.
struct Screen {
    Mat4 viewProjection;
    Mat4 inverse;  // of viewProjection
    float x = 0, y = 0, w = 1, h = 1;

    std::optional<Vec2> ToScreen(Vec3 p) const;  // nullopt: behind the eye
    Ray RayAt(Vec2 p) const;
};
std::optional<Screen> MakeScreen(const Mat4& viewProjection, float x, float y, float w, float h);

// --- The shapes (drawn and hit-tested alike) ----------------------------------------------------
inline constexpr float kHeadStart = 0.8f;                 // the arrow's head: from 0.8 to 1 of size
inline constexpr float kPlaneNear = 0.2f, kPlaneFar = 0.42f;  // the plane squares: 0.2 .. 0.42 of size
Vec3 AxisEnd(const Frame& f, int axis);
std::array<Vec3, 4> PlaneQuad(const Frame& f, Handle plane);
// The ring of the rotate handle about `axis`, as polylines, cut where it turns away from the eye:
// front = the half facing the eye (drawn strong, grabbed first), else the half behind (drawn faint,
// so every ring reads as a whole circle; grabbed only where no front ring is nearer).
std::vector<std::vector<Vec3>> RingArcs(const Frame& f, int axis, Vec3 eye, bool front = true, int segments = 64);
inline constexpr float kBackPenalty = 5.f;  // pixels: a ring's back half is grabbed as if this much farther
// A circle facing the eye around the gizmo, `radius` x size (closed: its last point is its first).
// kViewRing = the outer ring (Handle::View); 1 = the ball's outline, inside which a drag turns freely.
inline constexpr float kViewRing = 1.18f;
std::vector<Vec3> FacingCircle(const Frame& f, Vec3 eye, float radius, int segments = 64);
// An axis that points almost straight at the eye is not drawn or grabbed (it would be a dot).
bool AxisShown(const Frame& f, const Screen& s, int axis);
// ... and a plane square seen almost edge-on is not either. The square's corners on the screen in `out`.
bool PlaneShown(const Frame& f, const Screen& s, Handle plane, std::array<Vec2, 4>* out = nullptr);

// The handle under the mouse (`tolerance` pixels around the lines), None when there is none.
Handle HitTest(Tool tool, const Frame& f, const Screen& s, Vec3 eye, Vec2 mouse, float tolerance = 7.f);

// --- Dragging ----------------------------------------------------------------------------------
struct Drag {
    Tool tool = Tool::Move;
    Handle handle = Handle::None;
    Frame frame;
    Vec2 startMouse;
    Vec3 startPoint;      // move / scale: where the axis or plane was grabbed
    float startT = 0;     // move / scale on an axis: how far along the axis
    Vec2 tangent;         // rotate: the screen direction that turns forward (unit)
    Vec3 axis{0, 0, 0};   // rotate: the axis turned about (world); Free: the line of sight (to the eye)
    Vec3 right{0, 0, 0}, up{0, 0, 0};  // Free: the screen's right and up, in the world
};
// nullopt when the handle cannot be grabbed from here (looking straight along its axis or plane).
std::optional<Drag> BeginDrag(Tool tool, Handle handle, const Frame& f, const Screen& s, Vec3 eye, Vec2 mouse);

// Ctrl held: steps instead of a smooth change.
struct Snap {
    bool on = false;
    float move = 0.25f;    // world units
    float degrees = 15.f;
    float scale = 0.1f;    // of the scale at the start
};

// What the drag does so far, from its start.
struct Change {
    Tool tool = Tool::Move;
    Vec3 move{0, 0, 0};  // move: how far (world)
    int axis = -1;       // rotate: about the frame's axis (-1: View / Free); scale: that component (-1: all three)
    Vec3 worldAxis{0, 0, 0};  // rotate: the axis turned about; scale: the axis scaled along (world, unit)
    float degrees = 0;   // rotate
    float factor = 1;    // scale
};
// nullopt when the mouse's ray misses the axis / plane (keep the last change).
std::optional<Change> Update(const Drag& d, const Screen& s, Vec2 mouse, const Snap& snap);

// A thing's Transform after the change. start / startWorld: its Transform and world matrix when the
// drag began; parentWorld: its parent's world matrix (several things can be dragged at once).
// `hint`: the rotation shown now (the angles stay continuous). No `pivot`: each thing turns / scales
// about its own place, a ring about its own axis (Local) or the world's; a `pivot` (Pivot::Center):
// all turn about the gizmo's axis through that point and scale away from it (their places move too).
Transform Apply(const Transform& start, const Mat4& parentWorld, const Mat4& startWorld, const Change& c, Space space,
                Vec3 hint, const std::optional<Vec3>& pivot = std::nullopt);

// Math the gizmo needs, tested on its own.
// Along `axis` (unit) from `origin`: the point nearest the ray. nullopt when they are parallel.
std::optional<float> ClosestOnLine(const Ray& ray, Vec3 origin, Vec3 axis);
std::optional<Vec3> IntersectPlane(const Ray& ray, Vec3 point, Vec3 normal);
float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b);

}  // namespace rm::space3d::gizmo
