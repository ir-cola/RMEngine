#include "rm_engine/animation3d/runtime/Spring.h"

#include "rm_engine/animation3d/runtime/Ik.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>

namespace rm::animation3d {

using space3d::Mat4;
using space3d::operator+;
using space3d::operator-;
using space3d::operator*;
using space3d::Length;
using space3d::Normalize;

void AddSpringChain(const space3d::Skeleton& skeleton, int root, const SpringJoint& settings, std::vector<SpringJoint>& out) {
    if (root < 0 || static_cast<size_t>(root) >= skeleton.bones.size()) return;
    std::vector<bool> inside(skeleton.bones.size(), false);
    inside[static_cast<size_t>(root)] = true;
    for (size_t b = static_cast<size_t>(root) + 1; b < skeleton.bones.size(); ++b) {
        const int p = skeleton.bones[b].parent;
        if (p < 0 || !inside[static_cast<size_t>(p)]) continue;
        inside[b] = true;
        // (同じ骨が 2 つの鎖に入ったら、最初のもの)
        if (std::any_of(out.begin(), out.end(), [&](const SpringJoint& j) { return j.bone == static_cast<int>(b); })) continue;
        SpringJoint j = settings;
        j.bone = static_cast<int>(b);
        j.parent = p;
        out.push_back(j);
    }
}

namespace {

Vec3 WorldOf(const Mat4& world, const std::vector<Mat4>& model, int bone) { return space3d::TransformPoint(world, BonePosition(model, bone)); }

}  // namespace

bool StepSprings(SpringState& s, const space3d::Skeleton& skeleton, Pose& pose, std::vector<Mat4>& model, const Mat4& world, float dt, bool reset) {
    if (s.joints.empty()) return false;
    ModelMatrices(skeleton, pose, model);
    const size_t n = s.joints.size();
    std::vector<Vec3> animated(n);
    for (size_t i = 0; i < n; ++i) animated[i] = WorldOf(world, model, s.joints[i].bone);
    const Vec3 anchor{world.m[3][0], world.m[3][1], world.m[3][2]};
    if (!s.started || reset || s.prev.size() != n || Length(anchor - s.anchor) > kSpringWarp) {
        s.prev = s.cur = animated;
        s.carry = 0;
        s.started = true;
    }
    s.anchor = anchor;
    s.carry = std::min(s.carry + (std::isfinite(dt) && dt > 0 ? dt : 0.f), kSpringStep * kMaxSpringSteps);
    // 揺れる骨の番号 (骨 -> joints の番号)。親が揺れる骨なら、その今の位置を頭にする。
    std::vector<int> jointOf(skeleton.bones.size(), -1);
    for (size_t i = 0; i < n; ++i) {
        if (s.joints[i].bone >= 0 && static_cast<size_t>(s.joints[i].bone) < jointOf.size()) jointOf[static_cast<size_t>(s.joints[i].bone)] = static_cast<int>(i);
    }
    std::vector<Vec3> centers(s.spheres.size());
    std::vector<float> radii(s.spheres.size());
    for (size_t k = 0; k < s.spheres.size(); ++k) {
        const SpringSphere& sp = s.spheres[k];
        const bool ok = sp.bone >= 0 && static_cast<size_t>(sp.bone) < model.size();
        const Mat4 m = ok ? world * model[static_cast<size_t>(sp.bone)] : world;
        centers[k] = space3d::TransformPoint(m, sp.offset);
        radii[k] = sp.radius * Length(space3d::TransformDirection(m, {1, 0, 0}));
    }
    while (s.carry >= kSpringStep - 1e-6f) {  // (1/30 秒 = 2 刻み: 丸めで 1 刻み足りなくならないように)
        s.carry = std::max(0.f, s.carry - kSpringStep);
        const float h = kSpringStep;
        for (size_t i = 0; i < n; ++i) {
            const SpringJoint& j = s.joints[i];
            const int pj = j.parent >= 0 && static_cast<size_t>(j.parent) < jointOf.size() ? jointOf[static_cast<size_t>(j.parent)] : -1;
            const Vec3 head = pj >= 0 ? s.cur[static_cast<size_t>(pj)] : WorldOf(world, model, j.parent);
            const Vec3 animHead = WorldOf(world, model, j.parent);
            const Vec3 rest = animated[i] - animHead;
            const float len = Length(rest);
            if (len < 1e-6f) {
                s.prev[i] = s.cur[i] = head;
                continue;
            }
            Vec3 next = s.cur[i] + (s.cur[i] - s.prev[i]) * (1.f - std::clamp(j.drag, 0.f, 1.f)) + Normalize(rest) * (j.stiffness * h) +
                        Normalize(j.gravityDir) * (j.gravity * h);
            next = head + Normalize(next - head) * len;
            for (size_t k = 0; k < centers.size(); ++k) {
                const float r = radii[k] + j.radius;
                const Vec3 d = next - centers[k];
                const float dl = Length(d);
                if (dl < r && dl > 1e-6f) next = head + Normalize(centers[k] + d * (r / dl) - head) * len;
            }
            s.prev[i] = s.cur[i];
            s.cur[i] = next;
        }
    }
    // 姿勢へ: 親の骨を、子 (揺れる骨) が今の位置を向くように回す (親から順に)。
    const auto toModel = space3d::Inverse(world).value_or(Mat4{});
    bool moved = false;
    for (size_t i = 0; i < n; ++i) {
        const SpringJoint& j = s.joints[i];
        if (j.parent < 0) continue;
        const Vec3 from = BonePosition(model, j.bone) - BonePosition(model, j.parent);
        const Vec3 to = space3d::TransformPoint(toModel, s.cur[i]) - BonePosition(model, j.parent);
        if (Length(from) < 1e-6f || Length(to) < 1e-6f) continue;
        RotateBoneInModel(pose, skeleton, model, j.parent, FromTo(from, to));
        ModelMatrices(skeleton, pose, model);
        moved = true;
    }
    return moved;
}

}  // namespace rm::animation3d
