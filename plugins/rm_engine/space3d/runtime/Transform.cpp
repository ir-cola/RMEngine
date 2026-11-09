#include "rm_engine/space3d/runtime/Transform.h"

#include "rm_engine/space3d/runtime/View.h"  // Inverse

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::space3d {

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k][row] * b.m[c][k];
            r.m[c][row] = s;
        }
    }
    return r;
}

Vec3 TransformPoint(const Mat4& m, Vec3 p) {
    return {m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
            m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
            m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2]};
}

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;

// R(row, col) of the stored m[col][row].
float At(const Mat4& m, int row, int col) { return m.m[col][row]; }

// `a` + some multiple of 360 that is nearest `ref`.
float Wrap(float a, float ref) { return a + 360.f * std::round((ref - a) / 360.f); }

}  // namespace

Mat4 RotationMatrix(Vec3 d) {
    const float cx = std::cos(d.x * kDeg), sx = std::sin(d.x * kDeg);
    const float cy = std::cos(d.y * kDeg), sy = std::sin(d.y * kDeg);
    const float cz = std::cos(d.z * kDeg), sz = std::sin(d.z * kDeg);
    Mat4 rx, ry, rz;
    rx.m[1][1] = cx, rx.m[1][2] = sx, rx.m[2][1] = -sx, rx.m[2][2] = cx;
    ry.m[0][0] = cy, ry.m[0][2] = -sy, ry.m[2][0] = sy, ry.m[2][2] = cy;
    rz.m[0][0] = cz, rz.m[0][1] = sz, rz.m[1][0] = -sz, rz.m[1][1] = cz;
    return rz * ry * rx;
}

Mat4 LocalMatrix(const Transform& t) {
    Mat4 s, tr;
    s.m[0][0] = t.scale.x, s.m[1][1] = t.scale.y, s.m[2][2] = t.scale.z;
    tr.m[3][0] = t.position.x, tr.m[3][1] = t.position.y, tr.m[3][2] = t.position.z;
    return tr * RotationMatrix(t.rotation) * s;
}

Mat4 AxisAngle(Vec3 axis, float degrees) {
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    Mat4 r;
    if (len <= 0) return r;
    const float x = axis.x / len, y = axis.y / len, z = axis.z / len;
    const float c = std::cos(degrees * kDeg), s = std::sin(degrees * kDeg), k = 1 - c;
    // Rodrigues: R(row, col), stored as m[col][row].
    const float R[3][3] = {{c + x * x * k, x * y * k - z * s, x * z * k + y * s},
                           {y * x * k + z * s, c + y * y * k, y * z * k - x * s},
                           {z * x * k - y * s, z * y * k + x * s, c + z * z * k}};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) r.m[col][row] = R[row][col];
    }
    return r;
}

Vec3 EulerFromRotation(const Mat4& r, Vec3 hint) {
    // R = Rz * Ry * Rx:  R(2,0) = -sin y,  R(2,1) / R(2,2) = tan x,  R(1,0) / R(0,0) = tan z.
    const float sy = std::clamp(-At(r, 2, 0), -1.f, 1.f);
    const float y = std::asin(sy) / kDeg;
    Vec3 a;
    if (std::fabs(sy) < 0.99999f) {
        a = {std::atan2(At(r, 2, 1), At(r, 2, 2)) / kDeg, y, std::atan2(At(r, 1, 0), At(r, 0, 0)) / kDeg};
    } else {
        // Straight up or down (gimbal lock): only x - z (y = 90) or x + z (y = -90) is fixed. Keep z.
        const float z = hint.z;
        const float xz = std::atan2(At(r, 0, 1), At(r, 1, 1)) / kDeg;      // y = +90: x - z
        const float xpz = std::atan2(-At(r, 0, 1), At(r, 1, 1)) / kDeg;    // y = -90: x + z
        a = {sy > 0 ? xz + z : xpz - z, sy > 0 ? 90.f : -90.f, z};
    }
    // The other set of angles for the same turn.
    const Vec3 b{a.x + 180.f, 180.f - a.y, a.z + 180.f};
    auto nearest = [&](Vec3 v) { return Vec3{Wrap(v.x, hint.x), Wrap(v.y, hint.y), Wrap(v.z, hint.z)}; };
    auto cost = [&](Vec3 v) { return std::fabs(v.x - hint.x) + std::fabs(v.y - hint.y) + std::fabs(v.z - hint.z); };
    const Vec3 na = nearest(a), nb = nearest(b);
    return cost(nb) + 1e-3f < cost(na) ? nb : na;
}

WorldPlacement Decompose(const Mat4& w, Vec3 hint) {
    WorldPlacement out;
    out.position = {w.m[3][0], w.m[3][1], w.m[3][2]};
    Vec3 col[3];
    float len[3];
    for (int c = 0; c < 3; ++c) {
        col[c] = {w.m[c][0], w.m[c][1], w.m[c][2]};
        len[c] = std::sqrt(col[c].x * col[c].x + col[c].y * col[c].y + col[c].z * col[c].z);
    }
    // Mirrored (determinant < 0): call it a negative X scale.
    const Vec3 c12{col[1].y * col[2].z - col[1].z * col[2].y, col[1].z * col[2].x - col[1].x * col[2].z,
                   col[1].x * col[2].y - col[1].y * col[2].x};
    if (col[0].x * c12.x + col[0].y * c12.y + col[0].z * c12.z < 0) len[0] = -len[0];
    out.scale = {len[0], len[1], len[2]};
    Mat4 r;
    for (int c = 0; c < 3; ++c) {
        if (len[c] == 0) continue;  // a scale of 0: no direction to read, that axis stays as it is
        for (int row = 0; row < 3; ++row) r.m[c][row] = (&col[c].x)[row] / len[c];
    }
    out.rotation = EulerFromRotation(r, hint);
    return out;
}

Transform FromWorld(const Transform& current, const Mat4& parentWorld, const WorldPlacement& want) {
    Transform t = current;
    const auto toParent = Inverse(parentWorld);
    if (!toParent) return t;  // a parent of scale 0: nothing can be put anywhere
    const WorldPlacement now = Decompose(parentWorld * LocalMatrix(current), current.rotation);
    if (!(want.position == now.position)) t.position = TransformPoint(*toParent, want.position);
    if (!(want.rotation == now.rotation)) {
        // The parent's turn alone (its scale taken out), undone: the turn left is the thing's own.
        const auto parentTurn = Inverse(RotationMatrix(Decompose(parentWorld).rotation));
        if (parentTurn) t.rotation = EulerFromRotation(*parentTurn * RotationMatrix(want.rotation), current.rotation);
    }
    if (!(want.scale == now.scale)) {
        // World scale of an axis = own scale x how much the parent stretches that (turned) axis.
        const Mat4 turned = parentWorld * RotationMatrix(t.rotation);
        const float w[3] = {want.scale.x, want.scale.y, want.scale.z}, n[3] = {now.scale.x, now.scale.y, now.scale.z};
        float* own[3] = {&t.scale.x, &t.scale.y, &t.scale.z};
        for (int i = 0; i < 3; ++i) {
            if (w[i] == n[i]) continue;
            const float c0 = turned.m[i][0], c1 = turned.m[i][1], c2 = turned.m[i][2];
            const float stretch = std::sqrt(c0 * c0 + c1 * c1 + c2 * c2);
            if (stretch > 0) *own[i] = w[i] / stretch;
        }
    }
    return t;
}

}  // namespace rm::space3d
