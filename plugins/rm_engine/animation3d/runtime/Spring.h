#pragma once
// 3D アニメーション - 揺れる骨 (ばねの骨。docs/ANIMATION.md 7-2-3): 髪・スカート・しっぽ。骨の先を**ワールドの空間**で
// Verlet で動かし (キャラクターが走ると髪が後ろへ流れる)、骨の長さは保つ。式は VRM の揺れ (VRMC_springBone) と同じ:
//   次 = 今 + (今 - 前) x (1 - drag) + アニメーションの向き x stiffness x 刻み + 重力の向き x gravity x 刻み
// のあと、頭 (親) からの長さに戻し、当たりの球から押し出す。設定の値は VRM のものをそのまま使える。
// 刻みは dt に依らず 1/60 秒ずつ (1 フレームに多くて 4 回)。根が 1 フレームで大きく動いた (ワープ) ら始め直す。
// 純粋 (描画もファイルもない)。tests/TestAnimation3d.cpp でテストする。1 フレームの順: IK の後、FinishPose の前。

#include "rm_engine/animation3d/runtime/Pose.h"

#include <vector>

namespace rm::animation3d {

inline constexpr float kSpringStep = 1.f / 60.f;
inline constexpr int kMaxSpringSteps = 4;
inline constexpr float kSpringWarp = 2.f;  // 根がこれ (m) より 1 フレームで動いたら始め直す

// 揺れる骨 1 本 (その骨の頭の位置を動かす。親の骨の向きがそれを向く)。
struct SpringJoint {
    int bone = -1;
    int parent = -1;  // 親の骨 (動かない根か、ほかの揺れる骨)
    float stiffness = 1.f, drag = 0.4f, gravity = 0.f, radius = 0.02f;
    Vec3 gravityDir{0, -1, 0};
};
// 当たりの球 (骨の空間の offset、半径)。
struct SpringSphere {
    int bone = -1;
    Vec3 offset{0, 0, 0};
    float radius = 0.1f;
};

// 骨 `root` の下の骨すべてを、同じ設定で揺れる骨にする (`root` 自身は動かない: アニメーションのまま)。親が先に並ぶ。
void AddSpringChain(const space3d::Skeleton& skeleton, int root, const SpringJoint& settings, std::vector<SpringJoint>& out);

struct SpringState {
    std::vector<SpringJoint> joints;  // 親が先 (AddSpringChain の順)
    std::vector<SpringSphere> spheres;
    std::vector<Vec3> prev, cur;      // ワールドの骨の頭 (joints と同じ並び)
    float carry = 0;                  // まだ進めていない秒
    bool started = false;
    Vec3 anchor{0, 0, 0};             // 前のフレームの、エンティティの置き場所 (ワープを見る)
};

// `dt` 秒進めて、姿勢の揺れる骨の親を回す。`world` はエンティティのワールド行列、`model` は作業の骨の行列 (作り直す)。
// `reset`: 始め直す (スクリプトの ResetSprings・ワープの後)。動かした: true。
bool StepSprings(SpringState& state, const space3d::Skeleton& skeleton, Pose& pose, std::vector<space3d::Mat4>& model,
                 const space3d::Mat4& world, float dt, bool reset);

}  // namespace rm::animation3d
