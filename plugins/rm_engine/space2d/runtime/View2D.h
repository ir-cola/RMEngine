#pragma once
// 2D 空間: looking at the plane - the camera (where it looks and how near) and what a point of the
// screen touches. Pure - tested in tests/TestSpace2d.cpp.
//
// The screen is a rectangle of DIPs with Y going down; the plane has Y up (Transform2D.h). The camera
// puts `center` in the middle of the rectangle, `zoom` DIPs for one unit of the plane.

#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::space2d {

// A rectangle of the screen (DIPs). Its own type so the runtime does not need the UI's.
struct ScreenRect {
    float x = 0, y = 0, w = 0, h = 0;
};

RM_STRUCT()
struct Camera2D {
    RM_PROPERTY() Vec2 center{0, 0};
    RM_PROPERTY(Min=0.5, Max=20000) float zoom = 100.f;  // DIPs per unit (100: a sprite of 100 pixels per unit shows 1:1)
};

inline constexpr float kMinZoom = 0.5f;
inline constexpr float kMaxZoom = 20000.f;

Vec2 ToScreen(const Camera2D& cam, const ScreenRect& r, Vec2 world);
Vec2 ToWorld(const Camera2D& cam, const ScreenRect& r, Vec2 screen);
// world -> screen as a matrix (for drawing things turned and scaled).
Mat3 ScreenMatrix(const Camera2D& cam, const ScreenRect& r);

// The picture follows the mouse: it moved (dx, dy) DIPs on the screen.
void Pan(Camera2D& cam, float dx, float dy);
// Nearer (notches > 0) or farther, keeping the point of the plane under `screen` where it is.
void ZoomAt(Camera2D& cam, const ScreenRect& r, Vec2 screen, float notches);
// Shows a box of the plane (its middle, half its size) filling about 2/3 of the view.
void Frame(Camera2D& cam, const ScreenRect& r, Vec2 center, Vec2 halfSize);

// The grid's step in plane units: 1, 2 or 5 times a power of ten, the smallest whose lines are at
// least `minDips` apart on the screen. `major`: every this many lines one is darker (always 5 or 10).
struct GridStep {
    float step = 1;
    int major = 10;
};
GridStep GridFor(float zoom, float minDips = 12.f);

// Whether a point of the plane is inside a box (min .. max of the thing's own space) placed by `world`
// (moved, turned, scaled, mirrored).
bool InsidePlacedBox(Vec2 point, const Mat3& world, Vec2 min, Vec2 max);
// The 4 corners of that box in the world, counter-clockwise from (min.x, min.y).
void PlacedBoxCorners(const Mat3& world, Vec2 min, Vec2 max, Vec2 out[4]);

}  // namespace rm::space2d
