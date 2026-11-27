#include "rm_engine/animation3d/runtime/Ik.h"

#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>

namespace rm::animation3d {

using space3d::Mat4;
using space3d::Quat;
using space3d::operator+;
using space3d::operator-;
using space3d::operator*;
using space3d::Cross;
using space3d::Dot;
using space3d::Length;
using space3d::Normalize;

namespace {

constexpr float kEps = 1e-6f;

bool Valid(const space3d::Skeleton& s, int bone) { return bone >= 0 && static_cast<size_t>(bone) < s.bones.size(); }

// 骨の行列の回転 (大きさを外したもの)。
Quat RotationOf(const Mat4& m) { return space3d::DecomposeTRS(m).r; }

// v に直角な向き (長さ 1)。
Vec3 Perpendicular(Vec3 v) {
    const Vec3 axis = std::abs(v.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    return Normalize(Cross(v, axis));
}

}  // namespace

Quat FromTo(Vec3 a, Vec3 b) {
    a = Normalize(a);
    b = Normalize(b);
    if (Length(a) < kEps || Length(b) < kEps) return {};
    const float d = Dot(a, b);
    if (d > 1.f - 1e-7f) return {};
    if (d < -1.f + 1e-6f) {
        const Vec3 axis = Perpendicular(a);
        return {axis.x, axis.y, axis.z, 0.f};  // (半周)
    }
    const Vec3 c = Cross(a, b);
    return space3d::QuatNormalize({c.x, c.y, c.z, 1.f + d});
}

Quat LimitAngle(Quat q, float maxRadians) {
    q = space3d::QuatNormalize(q);
    if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
    const float angle = 2.f * std::acos(std::clamp(q.w, -1.f, 1.f));
    if (!(maxRadians >= 0) || angle <= maxRadians) return q;
    const float s = std::sqrt(std::max(0.f, 1.f - q.w * q.w));
    if (s < kEps) return {};
    return space3d::QuatAxisAngle({q.x / s, q.y / s, q.z / s}, maxRadians);
}

Quat Partial(Quat q, float w) {
    if (!(w > 0)) return {};
    if (w >= 1.f) return space3d::QuatNormalize(q);
    return QuatNlerp({}, q, w);
}

TwoBonePoints SolveTwoBone(Vec3 a, Vec3 b, Vec3 c, Vec3 target, Vec3 pole) {
    const float l1 = Length(b - a), l2 = Length(c - b);
    if (l1 < kEps || l2 < kEps) return {b, c};
    const Vec3 toT = target - a;
    const float d = Length(toT);
    Vec3 dir = d > kEps ? toT * (1.f / d) : Normalize(c - a);
    if (Length(dir) < kEps) dir = Normalize(b - a);
    const float reach = std::clamp(d, std::abs(l1 - l2) + 1e-4f, l1 + l2 - 1e-4f);
    const float cosA = std::clamp((l1 * l1 + reach * reach - l2 * l2) / (2.f * l1 * reach), -1.f, 1.f);
    const float sinA = std::sqrt(std::max(0.f, 1.f - cosA * cosA));
    // 曲げる向き: pole (無ければ今の中の向き) の、a → 目標の線に直角な成分。
    Vec3 bend = Length(pole) > kEps ? pole : b - a;
    bend = bend - dir * Dot(bend, dir);
    if (Length(bend) < kEps) {
        bend = (b - a) - dir * Dot(b - a, dir);
        if (Length(bend) < kEps) bend = Perpendicular(dir);
    }
    bend = Normalize(bend);
    return {a + dir * (l1 * cosA) + bend * (l1 * sinA), a + dir * reach};
}

void RotateBoneInModel(Pose& pose, const space3d::Skeleton& skeleton, const std::vector<Mat4>& model, int bone, Quat delta) {
    if (!Valid(skeleton, bone) || static_cast<size_t>(bone) >= pose.size()) return;
    const int parent = skeleton.bones[static_cast<size_t>(bone)].parent;
    const Quat pr = parent >= 0 && static_cast<size_t>(parent) < model.size() ? RotationOf(model[static_cast<size_t>(parent)]) : Quat{};
    space3d::BoneTRS& p = pose[static_cast<size_t>(bone)];
    p.r = space3d::QuatNormalize(space3d::QuatConjugate(pr) * delta * pr * p.r);
}

void MoveBoneInModel(Pose& pose, const space3d::Skeleton& skeleton, const std::vector<Mat4>& model, int bone, Vec3 move) {
    if (!Valid(skeleton, bone) || static_cast<size_t>(bone) >= pose.size()) return;
    const int parent = skeleton.bones[static_cast<size_t>(bone)].parent;
    Vec3 local = move;
    if (parent >= 0 && static_cast<size_t>(parent) < model.size()) {
        const space3d::BoneTRS pt = space3d::DecomposeTRS(model[static_cast<size_t>(parent)]);
        local = space3d::QuatRotate(space3d::QuatConjugate(pt.r), move);
        local = {local.x / (pt.s.x != 0 ? pt.s.x : 1.f), local.y / (pt.s.y != 0 ? pt.s.y : 1.f), local.z / (pt.s.z != 0 ? pt.s.z : 1.f)};
    }
    space3d::BoneTRS& p = pose[static_cast<size_t>(bone)];
    p.t = p.t + local;
}

Vec3 BonePosition(const std::vector<Mat4>& model, int bone) {
    if (bone < 0 || static_cast<size_t>(bone) >= model.size()) return {};
    const Mat4& m = model[static_cast<size_t>(bone)];
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

bool ApplyTwoBone(Pose& pose, const space3d::Skeleton& skeleton, std::vector<Mat4>& model, int end, Vec3 target, Vec3 pole, float weight) {
    if (!Valid(skeleton, end) || !(weight > 0)) return false;
    const int mid = skeleton.bones[static_cast<size_t>(end)].parent;
    if (!Valid(skeleton, mid)) return false;
    const int up = skeleton.bones[static_cast<size_t>(mid)].parent;
    if (!Valid(skeleton, up)) return false;
    ModelMatrices(skeleton, pose, model);
    const Vec3 a = BonePosition(model, up), b = BonePosition(model, mid), c = BonePosition(model, end);
    const Vec3 goal = Lerp(c, target, std::min(weight, 1.f));
    const TwoBonePoints want = SolveTwoBone(a, b, c, goal, pole);
    RotateBoneInModel(pose, skeleton, model, up, FromTo(b - a, want.mid - a));
    ModelMatrices(skeleton, pose, model);
    const Vec3 b1 = BonePosition(model, mid), c1 = BonePosition(model, end);
    RotateBoneInModel(pose, skeleton, model, mid, FromTo(c1 - b1, want.end - b1));
    ModelMatrices(skeleton, pose, model);
    return true;
}

Vec3 RestForward(const space3d::Skeleton& skeleton, int bone) {
    if (!Valid(skeleton, bone)) return {0, 0, 1};
    std::vector<Mat4> rest;
    ModelMatrices(skeleton, RestPose(skeleton), rest);
    return Normalize(space3d::QuatRotate(space3d::QuatConjugate(RotationOf(rest[static_cast<size_t>(bone)])), {0, 0, 1}));
}

bool ApplyLookAt(Pose& pose, const space3d::Skeleton& skeleton, std::vector<Mat4>& model, int bone, Vec3 localForward, Vec3 target,
                 float maxRadians, float weight) {
    if (!Valid(skeleton, bone) || !(weight > 0)) return false;
    ModelMatrices(skeleton, pose, model);
    const Vec3 at = BonePosition(model, bone);
    const Vec3 forward = space3d::QuatRotate(RotationOf(model[static_cast<size_t>(bone)]), localForward);
    const Quat delta = Partial(LimitAngle(FromTo(forward, target - at), maxRadians), weight);
    RotateBoneInModel(pose, skeleton, model, bone, delta);
    ModelMatrices(skeleton, pose, model);
    return true;
}

bool AlignToNormal(Pose& pose, const space3d::Skeleton& skeleton, std::vector<Mat4>& model, int bone, Vec3 normal, float maxRadians,
                   float weight) {
    if (!Valid(skeleton, bone) || !(weight > 0) || Length(normal) < kEps) return false;
    ModelMatrices(skeleton, pose, model);
    RotateBoneInModel(pose, skeleton, model, bone, Partial(LimitAngle(FromTo({0, 1, 0}, normal), maxRadians), weight));
    ModelMatrices(skeleton, pose, model);
    return true;
}

}  // namespace rm::animation3d
