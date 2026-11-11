#include "rm_engine/space2d/runtime/View2D.h"

#include <algorithm>
#include <cmath>

namespace rm::space2d {

namespace {

constexpr float kZoomPerNotch = 1.2f;

}  // namespace

Vec2 ToScreen(const Camera2D& cam, const ScreenRect& r, Vec2 world) {
    const Vec2 d = cam.rotation != 0 ? Rotate(world - cam.center, -cam.rotation) : world - cam.center;
    return {r.x + r.w * 0.5f + d.x * cam.zoom, r.y + r.h * 0.5f - d.y * cam.zoom};
}

Vec2 ToWorld(const Camera2D& cam, const ScreenRect& r, Vec2 screen) {
    const float z = std::max(cam.zoom, 1e-6f);
    const Vec2 d{(screen.x - (r.x + r.w * 0.5f)) / z, -(screen.y - (r.y + r.h * 0.5f)) / z};
    return cam.center + (cam.rotation != 0 ? Rotate(d, cam.rotation) : d);
}

Mat3 ScreenMatrix(const Camera2D& cam, const ScreenRect& r) {
    // The middle of the screen, Y turned over and zoomed, the camera's turn taken back, the centre to 0.
    const Mat3 screen{cam.zoom, 0, 0, -cam.zoom, r.x + r.w * 0.5f, r.y + r.h * 0.5f};
    return screen * Rotation(-cam.rotation) * Translation(Vec2{0, 0} - cam.center);
}

void Pan(Camera2D& cam, float dx, float dy) {
    const float z = std::max(cam.zoom, 1e-6f);
    cam.center = cam.center + Rotate({-dx / z, dy / z}, cam.rotation);
}

void ZoomAt(Camera2D& cam, const ScreenRect& r, Vec2 screen, float notches) {
    const Vec2 before = ToWorld(cam, r, screen);
    cam.zoom = std::clamp(cam.zoom * std::pow(kZoomPerNotch, notches), kMinZoom, kMaxZoom);
    const Vec2 after = ToWorld(cam, r, screen);
    cam.center = cam.center + (before - after);  // the same point under the mouse again
}

void Frame(Camera2D& cam, const ScreenRect& r, Vec2 center, Vec2 halfSize) {
    cam.center = center;
    const float hw = std::max(std::abs(halfSize.x), 0.05f), hh = std::max(std::abs(halfSize.y), 0.05f);
    const float zoom = std::min(r.w / (hw * 3.f), r.h / (hh * 3.f));
    cam.zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
}

GridStep GridFor(float zoom, float minDips) {
    GridStep g;
    const float want = minDips / std::max(zoom, 1e-6f);  // the smallest step that is far enough apart
    float base = std::pow(10.f, std::floor(std::log10(want)));
    for (int i = 0; i < 4; ++i, base *= 10.f) {
        for (const float k : {1.f, 2.f, 5.f}) {
            if (base * k >= want * 0.999f) {
                g.step = base * k;
                g.major = k == 2.f ? 5 : 10;  // 2, 4, 6, 8, (10): the 10s stand out
                return g;
            }
        }
    }
    g.step = base;
    return g;
}

bool InsidePlacedBox(Vec2 point, const Mat3& world, Vec2 min, Vec2 max) {
    const auto inv = Inverse(world);
    if (!inv) return false;
    const Vec2 p = TransformPoint(*inv, point);
    return p.x >= std::min(min.x, max.x) && p.x <= std::max(min.x, max.x) && p.y >= std::min(min.y, max.y) &&
           p.y <= std::max(min.y, max.y);
}

void PlacedBoxCorners(const Mat3& world, Vec2 min, Vec2 max, Vec2 out[4]) {
    out[0] = TransformPoint(world, {min.x, min.y});
    out[1] = TransformPoint(world, {max.x, min.y});
    out[2] = TransformPoint(world, {max.x, max.y});
    out[3] = TransformPoint(world, {min.x, max.y});
}

}  // namespace rm::space2d
