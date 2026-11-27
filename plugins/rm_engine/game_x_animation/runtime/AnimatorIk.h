#pragma once
// ゲーム制作 × アニメーション - 部品「IK」を、アニメーターの姿勢にかける (docs/ANIMATION.md 7-2-1)。
// 計算は 3D アニメーションの Ik.h。ここは「どの骨を・どこへ・どれだけ」を、部品の欄・スクリプト・地面から決める。
// 地面は物理の光線 (口 rm_engine.game_x_animation.ground: GroundProbe.h。物理が無ければ地面の鎖は何もしない)。
// 純粋 (シーンもファイルもない。地面と目標は関数で渡す)。tests/TestGameAnimation.cpp でテストする。
//
// 地面の鎖 (足を坂に): 足首の上から下へ光線 → 当たった高さとエンティティの足元の高さの差だけ足首を動かす
// (平らなら動かない。段の上では上がる)。足首は「当たった高さ + 足の高さ」より下へ行かない。足の上 (+Y) を
// 面の法線へ 30 度まで傾ける。腰は低い方の足の分だけ下げる (腰を下げる上限まで)。高さの変わりは 0.1 秒でならす。

#include "rm_engine/animation3d/runtime/Pose.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"

#include <functional>
#include <optional>

namespace rm::game_x_animation {

struct GroundHit {
    Vec3 point{0, 0, 0};
    Vec3 normal{0, 1, 0};
};

struct IkWorld {
    space3d::Mat4 entityWorld;  // アニメーターのエンティティのワールド行列
    float dt = 0;               // 秒 (ならすのに使う)
    // `from` から `dir` へ `max` までの地面 (自分の体には当たらない)。null: 地面が分からない。
    std::function<std::optional<GroundHit>(Vec3 from, Vec3 dir, float max)> ground;
    // 目標のエンティティのワールドの位置 (ずれ `offset` をそのエンティティの向きで足したもの)。無い: nullopt。
    std::function<std::optional<Vec3>(const EntityRef& target, Vec3 offset)> entity;
};

// IK の鎖の今の重み (部品の重み x 重みのパラメーター。スクリプトの SetIkWeight / SetIkTarget の重みがあればそれ)。
float IkWeight(const AnimatorRun& run, const IkChain& chain);
// `pose` (体とレイヤーを重ねた後の姿勢) に IK をかける。かけた鎖があれば true。
bool ApplyAnimatorIk(AnimatorRun& run, const AnimatorIk& ik, const IkWorld& world, animation3d::Pose& pose);

// スクリプト (RMAnim.h の SetIkTarget / SetIkWeight / LookAt)。
void SetIkTarget(AnimatorRun& run, std::string_view chain, Vec3 worldTarget, float weight);
void SetIkWeight(AnimatorRun& run, std::string_view chain, float weight);
void SetLookAt(AnimatorRun& run, Vec3 worldTarget, float weight);

}  // namespace rm::game_x_animation
