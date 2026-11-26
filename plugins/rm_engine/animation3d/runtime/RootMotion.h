#pragma once
// 3D アニメーション - ルートモーション (docs/ANIMATION.md 3-5・7-1): クリップの中で根の骨が動いた分を取り出し、
// 姿勢からは消して (StripRoot)、エンティティを動かす分 (RootDelta) にする。足が滑らない・攻撃で踏み込む。
// 純粋 (描画もファイルもない)。tests/TestAnimation3d.cpp でテストする。設定の型はクリップの ClipRootMotion
// (3D 空間の Skeleton.h。モデルの .meta の "clips" に置く)。
//
// 決まり:
//   - 根の骨は**親の無い骨だけ** (その姿勢がそのままモデルの空間)。名前が空なら自動 (RootBone)
//   - 向きは「クリップの始めのコマからどれだけ Y のまわりに回ったか」(yaw)。四元数を Y のまわりのひねりと
//     残り (傾き) に分け、ひねりだけを取る (ひねり = normalize(0, q.y, 0, q.w))。傾きは姿勢に残す
//   - 動いた分 move は**その区間の始めの向き**から見たもの (= エンティティの今の向きから見たもの。エンティティは
//     前のフレームまでに回った分だけもう回っている)。turn はその間に回った角度 (ラジアン。+Y のまわり)
//   - ループの継ぎ目は区切って足す: prev → 終わり、(またいだ数 - 1) 回の 1 周、始め → now。区切りごとに、
//     それまでに回った分で回してから足す (ループのクリップは最後と最初で根の位置が違ってよい)
//   - 逆再生はそのまま負の向きに動く。ループでないクリップは端で止まる (AdvanceTime の時刻のまま)

#include "rm_engine/animation3d/runtime/ClipTime.h"
#include "rm_engine/animation3d/runtime/Pose.h"

#include <span>
#include <string_view>

namespace rm::animation3d {

using space3d::ClipRootMotion;

// ある間に根が動いた分。move: モデルの空間の向きと大きさ (エンティティの行列で回してワールドへ)。turn: ラジアン。
struct RootDelta {
    Vec3 move{0, 0, 0};
    float turn = 0;
};

// 根の骨の番号。`name` が空: 親の無い骨のうち「(骨の外)」でない最初のもの。名前の骨が無い・親がある: -1。
int RootBone(const space3d::Skeleton& skeleton, std::string_view name);

// 四元数の Y のまわりのひねりの角度 (ラジアン。[-π, π])。
float YawOf(space3d::Quat q);
// Y のまわりに `radians` 回すもの。
space3d::Quat YawQuat(float radians);
// v を Y のまわりに `radians` 回したもの (YawQuat と同じ向き)。
Vec3 RotateY(Vec3 v, float radians);

// 根の骨が 1 つのクリップの中で時刻 a から b まで (継ぎ目をまたがない。b < a なら逆向き) に動いた分。
// settings の「横」「縦」に無い軸の動きは 0、「回転」が無ければ turn は 0 で move はモデルの空間のまま。
// root が -1 か使わない設定: 0。
RootDelta RootSegment(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int root,
                      const ClipRootMotion& settings, float a, float b);

// `first` の後に `then` が続いたもの (then は first の終わりの向きから見たもの)。
RootDelta AppendDelta(const RootDelta& first, const RootDelta& then);

// 時刻 prevTime から AdvanceTime の結果 `step` までに根が動いた分 (区切り方はこのファイルの頭の決まり)。
// speed は向き (負: 逆再生) にだけ使う。またいだ数は kMaxEventLoops で止める。
RootDelta RootDeltaBetween(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int root,
                           const ClipRootMotion& settings, float prevTime, const TimeStep& step, float speed, bool loop);

// 重みで混ぜる (合計で割る。重みが 0 以下のものは入れない。合計が 0: 0)。
RootDelta MixRootDelta(std::span<const RootDelta> deltas, std::span<const float> weights);
// a から b へ w (0 = a、1 = b)。
RootDelta LerpDelta(const RootDelta& a, const RootDelta& b, float w);

// 姿勢から取り出した軸を消す: 根の骨の位置の XZ (横) / Y (縦) を `start` (クリップの始めのコマ) の値に、
// 回転の Y のまわりのひねりを始めのコマのものに。root が範囲の外・使わない設定: そのまま。
void StripRoot(Pose& pose, int root, const ClipRootMotion& settings, const space3d::BoneTRS& start);

// 1 回 (ループなら 1 周) で動く分 (インスペクターに出す・速さを合わせる)。
RootDelta RootTotals(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int root,
                     const ClipRootMotion& settings);

}  // namespace rm::animation3d
