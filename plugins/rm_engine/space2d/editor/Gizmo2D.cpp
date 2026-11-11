#include "rm_engine/space2d/editor/Gizmo2D.h"

#include <algorithm>
#include <cmath>

namespace rm::space2d::gizmo2d {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// The plane's direction on the screen (Y turned over), unit length.
Vec2 OnScreen(Vec2 axis) { return Normalize({axis.x, -axis.y}); }

float DistanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float len2 = Dot(ab, ab);
    const float t = len2 > 1e-12f ? std::clamp(Dot(p - a, ab) / len2, 0.f, 1.f) : 0.f;
    return Length(p - (a + ab * t));
}

float Step(float v, float step) { return step > 0 ? std::round(v / step) * step : v; }

// The angle of the mouse about the gizmo's middle (degrees, counter-clockwise in the plane).
float AngleOf(Vec2 v) { return std::atan2(v.y, v.x) * 180.f / kPi; }

}  // namespace

Frame MakeFrame(const Mat3& world, Tool tool, Space space) {
    Frame f;
    f.origin = TransformPoint(world, {0, 0});
    if (space == Space::Local || tool == Tool::Scale) {
        const Vec2 x = Normalize({world.a, world.b});
        Vec2 y = Normalize({world.c, world.d});
        if (Length(x) == 0 && Length(y) == 0) return f;  // scaled to nothing: the world's axes
        if (Length(x) == 0) return f;
        if (Length(y) == 0) y = {-x.y, x.x};
        f.axes = {x, y};
    }
    return f;
}

ScreenShape ShapeOf(const Frame& f, const Camera2D& cam, const ScreenRect& r) {
    ScreenShape s;
    s.origin = ToScreen(cam, r, f.origin);
    for (size_t i = 0; i < 2; ++i) s.tips[i] = s.origin + OnScreen(f.axes[i]) * kArrowLength;
    return s;
}

Handle HitTest(Tool tool, const Frame& f, const Camera2D& cam, const ScreenRect& r, Vec2 mouse) {
    const ScreenShape s = ShapeOf(f, cam, r);
    if (tool == Tool::Rotate) {
        return std::abs(Length(mouse - s.origin) - kRingRadius) <= kHitSlop ? Handle::Ring : Handle::None;
    }
    if (std::abs(mouse.x - s.origin.x) <= kCenterHalf && std::abs(mouse.y - s.origin.y) <= kCenterHalf) return Handle::Free;
    // The nearer arrow wins where they come close.
    const float dx = DistanceToSegment(mouse, s.origin, s.tips[0]);
    const float dy = DistanceToSegment(mouse, s.origin, s.tips[1]);
    if (std::min(dx, dy) > kHitSlop) return Handle::None;
    return dx <= dy ? Handle::X : Handle::Y;
}

Drag BeginDrag(Tool tool, Handle handle, const Frame& f, const Camera2D& cam, const ScreenRect& r, Vec2 mouse) {
    Drag d;
    d.tool = tool;
    d.handle = handle;
    d.frame = f;
    d.startScreen = mouse;
    d.startWorld = ToWorld(cam, r, mouse);
    d.lastRaw = AngleOf(d.startWorld - f.origin);
    return d;
}

Change Update(Drag& drag, const Camera2D& cam, const ScreenRect& r, Vec2 mouse, const Snap& snap) {
    Change ch;
    const Vec2 now = ToWorld(cam, r, mouse);
    const Vec2 d = now - drag.startWorld;
    const auto& axes = drag.frame.axes;
    switch (drag.tool) {
        case Tool::Move: {
            if (drag.handle == Handle::X || drag.handle == Handle::Y) {
                const Vec2 a = axes[drag.handle == Handle::X ? 0 : 1];
                float along = Dot(d, a);
                if (snap.on) along = Step(along, snap.move);
                ch.move = a * along;
            } else {
                ch.move = snap.on ? Vec2{Step(d.x, snap.move), Step(d.y, snap.move)} : d;
            }
            break;
        }
        case Tool::Rotate: {
            const float raw = AngleOf(now - drag.frame.origin);
            float delta = raw - drag.lastRaw;  // the shorter way round (the mouse does not jump half a turn)
            while (delta > 180.f) delta -= 360.f;
            while (delta < -180.f) delta += 360.f;
            drag.lastRaw = raw;
            drag.turned += delta;
            ch.degrees = snap.on ? Step(drag.turned, snap.degrees) : drag.turned;
            break;
        }
        case Tool::Scale: {
            auto stepped = [&](float f) { return snap.on ? 1.f + Step(f - 1.f, snap.scale) : f; };
            if (drag.handle == Handle::X || drag.handle == Handle::Y) {
                const int i = drag.handle == Handle::X ? 0 : 1;
                const float from = Dot(drag.startWorld - drag.frame.origin, axes[i]);
                const float to = Dot(now - drag.frame.origin, axes[i]);
                const float f = std::abs(from) > 1e-6f ? to / from : 1.f;
                (i == 0 ? ch.scale.x : ch.scale.y) = stepped(f);
            } else {
                // Right and up grow, left and down shrink (never below 1/100).
                const float moved = (mouse.x - drag.startScreen.x) - (mouse.y - drag.startScreen.y);
                const float f = stepped(std::max(0.01f, 1.f + moved * kScalePerDip));
                ch.scale = {f, f};
            }
            break;
        }
    }
    return ch;
}

Transform2D Apply(const Transform2D& start, const Mat3& parentWorld, const Mat3& startWorld, const Change& change) {
    Transform2D t = start;
    if (change.move.x != 0 || change.move.y != 0) {
        const Vec2 world = TransformPoint(startWorld, {0, 0}) + change.move;
        if (const auto inv = Inverse(parentWorld)) t.position = TransformPoint(*inv, world);
    }
    if (change.degrees != 0) t.rotation = start.rotation + change.degrees * (Determinant(parentWorld) < 0 ? -1.f : 1.f);
    t.scale = {start.scale.x * change.scale.x, start.scale.y * change.scale.y};
    return t;
}

}  // namespace rm::space2d::gizmo2d
