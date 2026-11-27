#include "rm_engine/game_x_animation/runtime/AnimatorIk.h"

#include "rm_engine/animation3d/runtime/Ik.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>

namespace rm::game_x_animation {

namespace {

using space3d::Mat4;
using space3d::operator+;
using space3d::operator-;
using space3d::operator*;

constexpr float kDegrees = 0.017453292519943295f;
constexpr float kSmooth = 0.1f;          // 足と腰の高さをならす秒
constexpr float kFootTiltDegrees = 30;   // 足を面に合わせて傾けるのはここまで

AnimatorRun::IkScript* ScriptOf(AnimatorRun& run, std::string_view chain, bool create) {
    for (AnimatorRun::IkScript& s : run.ik.script) {
        if (s.chain == chain) return &s;
    }
    if (!create) return nullptr;
    run.ik.script.push_back({std::string(chain)});
    return &run.ik.script.back();
}

const AnimatorRun::IkScript* ScriptOf(const AnimatorRun& run, std::string_view chain) {
    for (const AnimatorRun::IkScript& s : run.ik.script) {
        if (s.chain == chain) return &s;
    }
    return nullptr;
}

// 鎖の先の骨と、2 本の骨ならその親と親の親もあるか。
bool ChainBones(const space3d::Skeleton& s, const IkChain& c, int& bone, int& top) {
    bone = s.Find(c.bone);
    top = bone;
    if (bone < 0) return false;
    if (c.kind == IkKind::LookAt) return true;
    const int mid = s.bones[static_cast<size_t>(bone)].parent;
    if (mid < 0) return false;
    top = s.bones[static_cast<size_t>(mid)].parent;
    return top >= 0;
}

float Smooth(float now, float want, float dt) { return now + (want - now) * std::clamp(dt / kSmooth, 0.f, 1.f); }

}  // namespace

float IkWeight(const AnimatorRun& run, const IkChain& chain) {
    float w = std::isfinite(chain.weight) ? std::clamp(chain.weight, 0.f, 1.f) : 0.f;
    if (!chain.weightParam.empty()) w *= std::clamp(RuleParam(run, chain.weightParam), 0.f, 1.f);
    if (const AnimatorRun::IkScript* s = ScriptOf(run, chain.name); s && s->weight >= 0) {
        w = s->weight;
    } else if (chain.kind == IkKind::LookAt && chain.goal == IkGoal::Script && run.ik.lookAt) {
        w = run.ik.lookAtWeight;
    }
    return w;
}

bool ApplyAnimatorIk(AnimatorRun& run, const AnimatorIk& ik, const IkWorld& world, animation3d::Pose& pose) {
    AnimatorRun::IkState& st = run.ik;
    st.active = false;
    if (!run.skin || ik.chains.empty()) return false;
    const space3d::Skeleton& skel = run.skin->skeleton;
    std::vector<Mat4>& model = st.model;
    const Mat4 toModel = space3d::Inverse(world.entityWorld).value_or(Mat4{});
    auto modelPoint = [&](Vec3 p) { return space3d::TransformPoint(toModel, p); };
    auto worldPoint = [&](Vec3 p) { return space3d::TransformPoint(world.entityWorld, p); };
    st.offset.resize(ik.chains.size(), 0.f);
    animation3d::ModelMatrices(skel, pose, model);
    bool any = false;

    // 地面の鎖: 足首の上から下へ光線。当たった高さと足元の高さの差を、ならして持つ。
    struct Foot {
        size_t chain = 0;
        int bone = -1;
        Vec3 ankle{0, 0, 0};  // ワールド (クリップの姿勢の足首)
        std::optional<GroundHit> hit;
    };
    std::vector<Foot> feet;
    const float originY = world.entityWorld.m[3][1];
    for (size_t i = 0; i < ik.chains.size(); ++i) {
        const IkChain& c = ik.chains[i];
        int bone = -1, top = -1;
        if (c.kind != IkKind::TwoBone || c.goal != IkGoal::Ground || !world.ground || !ChainBones(skel, c, bone, top)) continue;
        if (!(IkWeight(run, c) > 0)) continue;
        Foot f;
        f.chain = i;
        f.bone = bone;
        f.ankle = worldPoint(animation3d::BonePosition(model, bone));
        float leg = space3d::Length(worldPoint(animation3d::BonePosition(model, top)) - f.ankle);
        if (!(leg > 1e-3f)) leg = 0.5f;
        f.hit = world.ground(f.ankle + Vec3{0, leg, 0}, {0, -1, 0}, leg * 2.5f);
        const float want = f.hit ? std::clamp(f.hit->point.y - originY, -leg * 0.5f, leg * 0.5f) : 0.f;
        st.offset[i] = Smooth(st.offset[i], want, world.dt);
        feet.push_back(f);
    }
    // 腰: 低い方の足が届くように下げる。
    const int pelvis = ik.pelvis.empty() ? -1 : skel.Find(ik.pelvis);
    if (!feet.empty() && pelvis >= 0) {
        float lowest = 0;
        for (const Foot& f : feet) lowest = std::min(lowest, st.offset[f.chain]);
        st.pelvis = Smooth(st.pelvis, std::clamp(-lowest, 0.f, std::max(ik.pelvisDrop, 0.f)), world.dt);
        if (st.pelvis > 1e-5f) {
            animation3d::MoveBoneInModel(pose, skel, model, pelvis, space3d::TransformDirection(toModel, {0, -st.pelvis, 0}));
            animation3d::ModelMatrices(skel, pose, model);
        }
    }
    for (const Foot& f : feet) {
        const IkChain& c = ik.chains[f.chain];
        const float w = IkWeight(run, c);
        Vec3 want = f.ankle + Vec3{0, st.offset[f.chain], 0};
        if (f.hit) want.y = std::max(want.y, f.hit->point.y + std::max(c.footHeight, 0.f));
        any = animation3d::ApplyTwoBone(pose, skel, model, f.bone, modelPoint(want), c.pole, w) || any;
        if (f.hit) {
            const Vec3 n = space3d::Normalize(space3d::TransformDirection(toModel, f.hit->normal));
            animation3d::AlignToNormal(pose, skel, model, f.bone, n, std::min(kFootTiltDegrees, std::max(c.maxAngle, 0.f)) * kDegrees, w);
        }
    }

    // エンティティ・スクリプトの目標: 手を物に・顔を向ける。
    for (size_t i = 0; i < ik.chains.size(); ++i) {
        const IkChain& c = ik.chains[i];
        if (c.goal == IkGoal::Ground) continue;
        int bone = -1, top = -1;
        if (!ChainBones(skel, c, bone, top)) continue;
        const float w = IkWeight(run, c);
        if (!(w > 0)) continue;
        std::optional<Vec3> target;
        if (c.goal == IkGoal::Entity) {
            if (world.entity && !c.target.Empty()) target = world.entity(c.target, c.offset);
        } else if (const AnimatorRun::IkScript* s = ScriptOf(run, c.name); s && s->hasTarget) {
            target = s->target;
        } else if (c.kind == IkKind::LookAt && st.lookAt) {
            target = st.lookAtTarget;
        }
        if (!target) continue;
        if (c.kind == IkKind::TwoBone) {
            any = animation3d::ApplyTwoBone(pose, skel, model, bone, modelPoint(*target), c.pole, w) || any;
        } else {
            any = animation3d::ApplyLookAt(pose, skel, model, bone, animation3d::RestForward(skel, bone), modelPoint(*target),
                                           std::max(c.maxAngle, 0.f) * kDegrees, w) ||
                  any;
        }
    }
    st.active = any;
    return any;
}

void SetIkTarget(AnimatorRun& run, std::string_view chain, Vec3 worldTarget, float weight) {
    AnimatorRun::IkScript* s = ScriptOf(run, chain, true);
    s->hasTarget = true;
    s->target = worldTarget;
    s->weight = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 1.f;
}

void SetIkWeight(AnimatorRun& run, std::string_view chain, float weight) {
    AnimatorRun::IkScript* s = ScriptOf(run, chain, true);
    s->weight = std::isfinite(weight) && weight >= 0 ? std::min(weight, 1.f) : -1.f;
}

void SetLookAt(AnimatorRun& run, Vec3 worldTarget, float weight) {
    run.ik.lookAt = std::isfinite(weight) && weight > 0;
    run.ik.lookAtTarget = worldTarget;
    run.ik.lookAtWeight = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 0.f;
}

}  // namespace rm::game_x_animation
