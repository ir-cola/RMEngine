#include "rm_engine/space2d/runtime/Transform2D.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace rm::space2d {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRad = kPi / 180.f;

// The same angle as `deg` (+ 360s) nearest `hint`.
float NearAngle(float deg, float hint) { return deg + 360.f * std::round((hint - deg) / 360.f); }

}  // namespace

Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
float Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }
Vec2 Normalize(Vec2 v) {
    const float l = Length(v);
    return l > 1e-12f ? v * (1.f / l) : Vec2{0, 0};
}
Vec2 Rotate(Vec2 v, float degrees) {
    const float s = std::sin(degrees * kRad), c = std::cos(degrees * kRad);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

Mat3 operator*(const Mat3& m, const Mat3& n) {
    Mat3 r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.tx = m.a * n.tx + m.c * n.ty + m.tx;
    r.ty = m.b * n.tx + m.d * n.ty + m.ty;
    return r;
}

Vec2 TransformPoint(const Mat3& m, Vec2 p) { return {m.a * p.x + m.c * p.y + m.tx, m.b * p.x + m.d * p.y + m.ty}; }
Vec2 TransformDirection(const Mat3& m, Vec2 v) { return {m.a * v.x + m.c * v.y, m.b * v.x + m.d * v.y}; }
float Determinant(const Mat3& m) { return m.a * m.d - m.b * m.c; }

std::optional<Mat3> Inverse(const Mat3& m) {
    const float det = Determinant(m);
    if (std::abs(det) < 1e-12f || !std::isfinite(det)) return std::nullopt;
    const float k = 1.f / det;
    Mat3 r;
    r.a = m.d * k;
    r.b = -m.b * k;
    r.c = -m.c * k;
    r.d = m.a * k;
    r.tx = -(r.a * m.tx + r.c * m.ty);
    r.ty = -(r.b * m.tx + r.d * m.ty);
    return r;
}

Mat3 Translation(Vec2 t) { return {1, 0, 0, 1, t.x, t.y}; }
Mat3 Rotation(float degrees) {
    const float s = std::sin(degrees * kRad), c = std::cos(degrees * kRad);
    return {c, s, -s, c, 0, 0};
}
Mat3 Scaling(Vec2 s) { return {s.x, 0, 0, s.y, 0, 0}; }

Mat3 LocalMatrix(const Transform2D& t) { return Translation(t.position) * Rotation(t.rotation) * Scaling(t.scale); }

WorldPlacement2D Decompose(const Mat3& world, float hintDegrees) {
    WorldPlacement2D p;
    p.position = {world.tx, world.ty};
    const Vec2 x{world.a, world.b}, y{world.c, world.d};
    const float sx = Length(x);
    // The X axis gives the turn; Y is what is left (negative when mirrored; not at right angles when skewed).
    p.scale.x = sx;
    p.scale.y = sx > 1e-12f ? Determinant(world) / sx : Length(y);
    const float deg = sx > 1e-12f ? std::atan2(x.y, x.x) / kRad : std::atan2(-y.x, y.y) / kRad;
    p.rotation = NearAngle(deg, hintDegrees);
    return p;
}

Transform2D FromWorld(const Transform2D& current, const Mat3& parentWorld, int parentOrder, const WorldPlacement2D& want) {
    Transform2D t = current;
    const WorldPlacement2D now = Decompose(parentWorld * LocalMatrix(current), current.rotation);
    if (want.position != now.position) {
        if (const auto inv = Inverse(parentWorld)) t.position = TransformPoint(*inv, want.position);
    }
    if (want.rotation != now.rotation) {
        // A mirrored parent turns the other way.
        const float sign = Determinant(parentWorld) < 0 ? -1.f : 1.f;
        t.rotation = current.rotation + (want.rotation - now.rotation) * sign;
    }
    auto ratio = [](float want, float now, float own) {
        if (std::abs(now) < 1e-12f) return want;  // nothing to scale from: take it as it is
        return own * (want / now);
    };
    if (want.scale.x != now.scale.x) t.scale.x = ratio(want.scale.x, now.scale.x, current.scale.x);
    if (want.scale.y != now.scale.y) t.scale.y = ratio(want.scale.y, now.scale.y, current.scale.y);
    if (want.order != parentOrder + current.order) t.order = want.order - parentOrder;
    return t;
}

std::vector<size_t> DrawOrder(std::span<const DrawKey> keys) {
    std::vector<size_t> out(keys.size());
    std::iota(out.begin(), out.end(), size_t{0});
    std::stable_sort(out.begin(), out.end(), [&](size_t i, size_t j) {
        const DrawKey& a = keys[i];
        const DrawKey& b = keys[j];
        if (a.order != b.order) return a.order < b.order;
        if (a.subOrder != b.subOrder) return a.subOrder < b.subOrder;
        return a.tree < b.tree;
    });
    return out;
}

}  // namespace rm::space2d
