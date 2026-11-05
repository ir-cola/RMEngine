#include "rm_engine/space3d/runtime/Transform.h"

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

Mat4 LocalMatrix(const Transform& t) {
    constexpr float kDeg = std::numbers::pi_v<float> / 180.f;
    const float cx = std::cos(t.rotation.x * kDeg), sx = std::sin(t.rotation.x * kDeg);
    const float cy = std::cos(t.rotation.y * kDeg), sy = std::sin(t.rotation.y * kDeg);
    const float cz = std::cos(t.rotation.z * kDeg), sz = std::sin(t.rotation.z * kDeg);
    Mat4 rx, ry, rz, s, tr;
    rx.m[1][1] = cx, rx.m[1][2] = sx, rx.m[2][1] = -sx, rx.m[2][2] = cx;
    ry.m[0][0] = cy, ry.m[0][2] = -sy, ry.m[2][0] = sy, ry.m[2][2] = cy;
    rz.m[0][0] = cz, rz.m[0][1] = sz, rz.m[1][0] = -sz, rz.m[1][1] = cz;
    s.m[0][0] = t.scale.x, s.m[1][1] = t.scale.y, s.m[2][2] = t.scale.z;
    tr.m[3][0] = t.position.x, tr.m[3][1] = t.position.y, tr.m[3][2] = t.position.z;
    return tr * rz * ry * rx * s;
}

}  // namespace rm::space3d
