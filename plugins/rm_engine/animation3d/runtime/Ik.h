#pragma once
// 3D アニメーション - IK (docs/ANIMATION.md 7-2-1): 2 本の骨 (足を坂に・手を物に) と、骨を目標へ向ける (顔を向ける)。
// どれも姿勢 (親の空間の位置・回転・大きさ) を直し、骨の行列 (モデルの空間。ModelMatrices) を作り直す。
// 目標・曲げる向き・面の法線はモデルの空間で渡す (ワールドからはゲーム制作 × アニメーション が直す)。
// 純粋 (描画もファイルもない)。tests/TestAnimation3d.cpp でテストする。
//
// 1 フレームの順: レイヤー → IK → 揺れ (Spring.h) → FinishPose。

#include "rm_engine/animation3d/runtime/Pose.h"

#include <vector>

namespace rm::animation3d {

// a の向きを b の向きへ回す一番短い回転 (長さは見ない。どちらかが 0: 回さない。反対向き: 直角な軸で半周)。
space3d::Quat FromTo(Vec3 a, Vec3 b);
// 回す角度を `maxRadians` までにする (軸はそのまま)。
space3d::Quat LimitAngle(space3d::Quat q, float maxRadians);
// 回さない所から q まで `w` (0..1)。
space3d::Quat Partial(space3d::Quat q, float w);

// 2 本の骨を解く (余弦定理): 上の骨の根 a・中 b・先 c (モデルの空間) を、先が目標 `target` に着くように曲げた
// 中と先の新しい位置。曲がる平面は a → target の線と `pole` (曲げる向き。0: 今の中の向き) で決める。
// 届かないときはまっすぐ伸ばし (長さは変えない)、近すぎるときは曲げられる所まで。
struct TwoBonePoints {
    Vec3 mid, end;
};
TwoBonePoints SolveTwoBone(Vec3 a, Vec3 b, Vec3 c, Vec3 target, Vec3 pole);

// 姿勢の骨 `bone` を、モデルの空間で `delta` だけ回す (親の骨の行列 `model` から親の空間へ直す)。
void RotateBoneInModel(Pose& pose, const space3d::Skeleton& skeleton, const std::vector<space3d::Mat4>& model, int bone,
                       space3d::Quat delta);
// 骨の行列の位置。
Vec3 BonePosition(const std::vector<space3d::Mat4>& model, int bone);

// 2 本の骨の IK: 骨 `end` (足首・手首) の親 (膝・肘) と親の親 (腿・二の腕) を回して、`end` を `target` へ
// (`weight` で今の所から混ぜる)。`model` は今の姿勢の骨の行列 (作り直す)。骨が足りない: false (何もしない)。
bool ApplyTwoBone(Pose& pose, const space3d::Skeleton& skeleton, std::vector<space3d::Mat4>& model, int end, Vec3 target, Vec3 pole,
                  float weight);
// 骨の前の向き (休みの姿勢で、モデルの +Z を向いていた向きを、その骨の空間で)。LookAt に渡す。
Vec3 RestForward(const space3d::Skeleton& skeleton, int bone);
// 骨 `bone` の前 (`localForward`: RestForward) を `target` へ向ける。回すのは `maxRadians` まで・`weight` だけ。
bool ApplyLookAt(Pose& pose, const space3d::Skeleton& skeleton, std::vector<space3d::Mat4>& model, int bone, Vec3 localForward,
                 Vec3 target, float maxRadians, float weight);
// 骨 `bone` を、モデルの上 (+Y) が `normal` を向くように回す (足を坂に。`maxRadians` まで・`weight` だけ)。
bool AlignToNormal(Pose& pose, const space3d::Skeleton& skeleton, std::vector<space3d::Mat4>& model, int bone, Vec3 normal,
                   float maxRadians, float weight);
// 骨 `bone` をモデルの空間で `move` だけずらす (腰を下げる)。
void MoveBoneInModel(Pose& pose, const space3d::Skeleton& skeleton, const std::vector<space3d::Mat4>& model, int bone, Vec3 move);

}  // namespace rm::animation3d
