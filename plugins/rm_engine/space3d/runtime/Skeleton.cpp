#include "rm_engine/space3d/runtime/Skeleton.h"

#include <algorithm>
#include <cmath>

namespace rm::space3d {

namespace {

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Column(const Mat4& m, int c) { return {m.m[c][0], m.m[c][1], m.m[c][2]}; }

}  // namespace

Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat QuatNormalize(Quat q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(len > 1e-12f)) return {};
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

Quat QuatConjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

float QuatDot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

Vec3 QuatRotate(Quat q, Vec3 v) {
    // v + 2w (u x v) + 2 u x (u x v)
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 c = Cross(u, v);
    const Vec3 cc = Cross(u, c);
    return {v.x + 2 * (q.w * c.x + cc.x), v.y + 2 * (q.w * c.y + cc.y), v.z + 2 * (q.w * c.z + cc.z)};
}

Quat QuatAxisAngle(Vec3 axis, float radians) {
    const float len = Len(axis);
    if (!(len > 1e-12f)) return {};
    const float s = std::sin(radians * 0.5f) / len;
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}

Mat4 QuatMatrix(Quat q) {
    q = QuatNormalize(q);
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 r;
    r.m[0][0] = 1 - 2 * (y * y + z * z), r.m[0][1] = 2 * (x * y + z * w), r.m[0][2] = 2 * (x * z - y * w);
    r.m[1][0] = 2 * (x * y - z * w), r.m[1][1] = 1 - 2 * (x * x + z * z), r.m[1][2] = 2 * (y * z + x * w);
    r.m[2][0] = 2 * (x * z + y * w), r.m[2][1] = 2 * (y * z - x * w), r.m[2][2] = 1 - 2 * (x * x + y * y);
    return r;
}

Quat QuatFromMatrix(const Mat4& m) {
    // R(行, 列) = m[列][行]
    auto R = [&](int row, int col) { return m.m[col][row]; };
    const float trace = R(0, 0) + R(1, 1) + R(2, 2);
    Quat q;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.f) * 2.f;  // 4w
        q = {(R(2, 1) - R(1, 2)) / s, (R(0, 2) - R(2, 0)) / s, (R(1, 0) - R(0, 1)) / s, 0.25f * s};
    } else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
        const float s = std::sqrt(1.f + R(0, 0) - R(1, 1) - R(2, 2)) * 2.f;  // 4x
        q = {0.25f * s, (R(0, 1) + R(1, 0)) / s, (R(0, 2) + R(2, 0)) / s, (R(2, 1) - R(1, 2)) / s};
    } else if (R(1, 1) > R(2, 2)) {
        const float s = std::sqrt(1.f + R(1, 1) - R(0, 0) - R(2, 2)) * 2.f;  // 4y
        q = {(R(0, 1) + R(1, 0)) / s, 0.25f * s, (R(1, 2) + R(2, 1)) / s, (R(0, 2) - R(2, 0)) / s};
    } else {
        const float s = std::sqrt(1.f + R(2, 2) - R(0, 0) - R(1, 1)) * 2.f;  // 4z
        q = {(R(0, 2) + R(2, 0)) / s, (R(1, 2) + R(2, 1)) / s, 0.25f * s, (R(1, 0) - R(0, 1)) / s};
    }
    q = QuatNormalize(q);
    if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
    return q;
}

Mat4 TRSMatrix(const BoneTRS& p) {
    Mat4 r = QuatMatrix(p.r);
    const float s[3] = {p.s.x, p.s.y, p.s.z};
    for (int c = 0; c < 3; ++c) {
        for (int row = 0; row < 3; ++row) r.m[c][row] *= s[c];
    }
    r.m[3][0] = p.t.x, r.m[3][1] = p.t.y, r.m[3][2] = p.t.z;
    return r;
}

BoneTRS DecomposeTRS(const Mat4& m) {
    BoneTRS p;
    p.t = {m.m[3][0], m.m[3][1], m.m[3][2]};
    Vec3 c[3] = {Column(m, 0), Column(m, 1), Column(m, 2)};
    float s[3] = {Len(c[0]), Len(c[1]), Len(c[2])};
    // 鏡写し: X を負に (回転の部分は行列式 +1 に)。
    const Vec3 x = Cross(c[1], c[2]);
    if (x.x * c[0].x + x.y * c[0].y + x.z * c[0].z < 0) s[0] = -s[0];
    Mat4 r;
    for (int i = 0; i < 3; ++i) {
        const float inv = std::abs(s[i]) > 1e-12f ? 1.f / s[i] : 0.f;
        r.m[i][0] = c[i].x * inv, r.m[i][1] = c[i].y * inv, r.m[i][2] = c[i].z * inv;
    }
    // 大きさ 0 の軸: 残りの軸から作り直す (回転を壊さない)。
    if (std::abs(s[0]) <= 1e-12f || std::abs(s[1]) <= 1e-12f || std::abs(s[2]) <= 1e-12f) {
        p.r = {};
    } else {
        p.r = QuatFromMatrix(r);
    }
    p.s = {s[0], s[1], s[2]};
    return p;
}

BoneMatrix ToBoneMatrix(const Mat4& m) {
    BoneMatrix b;
    for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < 4; ++c) b.row[row][c] = m.m[c][row];
    }
    return b;
}

int Skeleton::Find(std::string_view name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

bool ParentsFirst(const Skeleton& s) {
    for (size_t i = 0; i < s.bones.size(); ++i) {
        const int p = s.bones[i].parent;
        if (p < -1 || p >= static_cast<int>(i)) return false;
    }
    return true;
}

SkinVertex PackInfluences(std::vector<std::pair<uint32_t, float>> influences, uint32_t fallback, bool* dropped) {
    std::erase_if(influences, [](const auto& i) { return !(i.second > 0.f); });
    // 同じ骨が 2 回あれば足す。
    std::sort(influences.begin(), influences.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::pair<uint32_t, float>> merged;
    for (const auto& i : influences) {
        if (!merged.empty() && merged.back().first == i.first) {
            merged.back().second += i.second;
        } else {
            merged.push_back(i);
        }
    }
    std::stable_sort(merged.begin(), merged.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (dropped) *dropped = merged.size() > 4;
    if (merged.size() > 4) merged.resize(4);
    SkinVertex v;
    if (merged.empty()) {
        v.bone[0] = static_cast<uint16_t>(fallback);
        v.weight[0] = 65535;
        return v;
    }
    float sum = 0;
    for (const auto& i : merged) sum += i.second;
    int total = 0;
    for (size_t k = 0; k < merged.size(); ++k) {
        v.bone[k] = static_cast<uint16_t>(merged[k].first);
        const int w = static_cast<int>(std::lround(merged[k].second / sum * 65535.f));
        v.weight[k] = static_cast<uint16_t>(std::clamp(w, 0, 65535));
        total += v.weight[k];
    }
    v.weight[0] = static_cast<uint16_t>(std::clamp(static_cast<int>(v.weight[0]) + (65535 - total), 0, 65535));
    return v;
}

void ComputeBoneBounds(SkinData& skin, const std::vector<Vec3>& positions) {
    const size_t n = skin.skeleton.bones.size();
    skin.boundsMin.assign(n, Vec3{1e30f, 1e30f, 1e30f});
    skin.boundsMax.assign(n, Vec3{-1e30f, -1e30f, -1e30f});
    const size_t count = std::min(positions.size(), skin.vertices.size());
    for (size_t i = 0; i < count; ++i) {
        const SkinVertex& sv = skin.vertices[i];
        for (int k = 0; k < 4; ++k) {
            if (sv.weight[k] == 0 || sv.bone[k] >= n) continue;
            const size_t b = sv.bone[k];
            const Vec3 p = b < skin.inverseBind.size() ? TransformPoint(skin.inverseBind[b], positions[i]) : positions[i];
            Vec3& lo = skin.boundsMin[b];
            Vec3& hi = skin.boundsMax[b];
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
    }
}

uint32_t ClipFrames(float duration, float rate) {
    if (!(duration > 0.f) || !(rate > 0.f)) return 1;
    return static_cast<uint32_t>(std::lround(duration * rate)) + 1;
}

}  // namespace rm::space3d
