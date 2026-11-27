#include "rm_engine/game_x_animation/runtime/AnimatorSprings.h"

#include "core/reflect/Data.h"

#include <algorithm>
#include <format>
#include <utility>

namespace rm::game_x_animation {

namespace {

// 鎖と当たりを作り直す鍵 (部品の中身とモデル)。
std::string KeyOf(const AnimatorRun& run, const SpringBones& springs) {
    return std::format("{}|{}", static_cast<const void*>(run.skin.get()), json::Write(reflect::Save(springs), false));
}

void Build(AnimatorRun& run, const SpringBones& springs) {
    animation3d::SpringState& st = run.springs;
    st = {};
    if (!run.skin) return;
    const space3d::Skeleton& skel = run.skin->skeleton;
    for (const SpringChain& c : springs.chains) {
        animation3d::SpringJoint j;
        j.stiffness = c.stiffness, j.drag = c.drag, j.gravity = c.gravity, j.radius = c.radius;
        animation3d::AddSpringChain(skel, skel.Find(c.bone), j, st.joints);
    }
    for (const SpringCollider& c : springs.colliders) {
        const int b = skel.Find(c.bone);
        if (b >= 0) st.spheres.push_back({b, c.offset, c.radius});
    }
    if (springs.useModel) {
        for (const space3d::SpringChainSetting& ch : run.skin->springs) {
            if (ch.joints.empty()) continue;
            auto settingOf = [](const space3d::SpringJointSetting& s) {
                animation3d::SpringJoint j;
                j.stiffness = s.stiffness, j.drag = s.drag, j.gravity = s.gravity, j.radius = s.radius, j.gravityDir = s.gravityDir;
                return j;
            };
            if (ch.below) {
                animation3d::AddSpringChain(skel, skel.Find(ch.joints[0].bone), settingOf(ch.joints[0]), st.joints);
                continue;
            }
            // 並びのとおり: 2 つ目から動き、前の骨の設定を使う (VRM 1.0 の決まり)。
            for (size_t k = 1; k < ch.joints.size(); ++k) {
                animation3d::SpringJoint j = settingOf(ch.joints[k - 1]);
                j.bone = skel.Find(ch.joints[k].bone);
                j.parent = skel.Find(ch.joints[k - 1].bone);
                if (j.bone < 0 || j.parent < 0) break;
                bool seen = false;
                for (const animation3d::SpringJoint& o : st.joints) seen = seen || o.bone == j.bone;
                if (!seen) st.joints.push_back(j);
            }
        }
        for (const space3d::SpringColliderSetting& c : run.skin->springColliders) {
            const int b = skel.Find(c.bone);
            if (b >= 0) st.spheres.push_back({b, c.offset, c.radius});
        }
    }
    // 親が先に (骨の番号の順: 骨の木は親が先)。
    std::stable_sort(st.joints.begin(), st.joints.end(), [](const animation3d::SpringJoint& a, const animation3d::SpringJoint& b) { return a.bone < b.bone; });
}

}  // namespace

bool ApplyAnimatorSprings(AnimatorRun& run, const SpringBones& springs, const space3d::Mat4& world, float dt, animation3d::Pose& pose) {
    if (!run.skin) return false;
    const std::string key = KeyOf(run, springs);
    if (key != run.springsKey) {
        run.springsKey = key;
        Build(run, springs);
    }
    const bool reset = std::exchange(run.springReset, false);
    return animation3d::StepSprings(run.springs, run.skin->skeleton, pose, run.springModel, world, dt, reset);
}

void ResetSprings(AnimatorRun& run) { run.springReset = true; }

}  // namespace rm::game_x_animation
