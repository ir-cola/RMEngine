#pragma once
// 3D アニメーション - 姿勢 (ポーズ) の計算 (docs/ANIMATION.md 3-3): クリップを読む → 混ぜる → 骨の行列 →
// スキンの行列。データの型 (Skeleton・SkinData・Clip) は 3D 空間の Skeleton.h にある。ここはその計算だけ。
// 純粋 (描画もファイルもない)。tests/TestAnimation3d.cpp でテストする。
//
// 1 体の 1 フレーム:
//   BindClip (クリップを読み込んだときに 1 回) → SampleClip → (Blend / PoseAccumulator)
//   → ModelMatrices (親から順に) → SkinMatrices (GPU へ: space3d::BoneMatrix)

#include "rm_engine/space3d/runtime/Skeleton.h"

#include <span>
#include <string>
#include <vector>

namespace rm::animation3d {

// 骨ごとの位置・回転・大きさ (親の空間で)。数と順番は Skeleton::bones と同じ。計算の途中だけにある。
using Pose = std::vector<space3d::BoneTRS>;

// 休みの姿勢 (骨ごとの Bone::rest)。
Pose RestPose(const space3d::Skeleton& skeleton);

// a から b へ w (0 = a、1 = b)。
Vec3 Lerp(Vec3 a, Vec3 b, float w);
// 回転の nlerp: 4 つの数を直線で混ぜてから長さを 1 に直す。b が a の反対側 (QuatDot < 0) のときは
// b を -b にしてから混ぜる (q と -q は同じ回転。そろえないと遠回りの 360 度近く回る)。
space3d::Quat QuatNlerp(space3d::Quat a, space3d::Quat b, float w);
// 位置・大きさは直線、回転は QuatNlerp。
space3d::BoneTRS LerpTRS(const space3d::BoneTRS& a, const space3d::BoneTRS& b, float w);

// クリップと骨の木の組の、骨ごとのトラックの番号。クリップの骨は**名前**で指すので、名前から番号へは
// ここで 1 回だけ引き、毎フレーム使い回す。
//   同じ名前の骨が何本もあるとき: その名前の k 本目の骨は、その名前の k 本目のトラック
//   (docs/ANIMATION.md 3-2「名前 + 何番目で引く」)。k 本目のトラックが無ければ最初のトラック。
struct ClipBinding {
    std::vector<int> track;  // 骨ごと: Clip::tracks の番号 (-1: トラックが無い = 休みの姿勢のまま)
    int unmatched = 0;       // どの骨にも当たらなかったトラックの数 (別のモデルのクリップ・名前の違い)
};
ClipBinding BindClip(const space3d::Clip& clip, const space3d::Skeleton& skeleton);

// クリップの `time` 秒の姿勢を `out` に (out は骨の数にそろえる)。
//   刻みの位置 = time / duration x (frames - 1) (= time x rate。最後のサンプルがちょうど duration)。
//   前後 2 つのサンプルを混ぜる: 位置・大きさは直線、回転は QuatNlerp。
//   loop: time を [0, duration) に回す。最後のサンプル (time = duration) の次は最初のサンプル
//         (ループするクリップは最後と最初を同じ値で作るので、継ぎ目で飛ばない)。
//   loop でない: [0, duration] に止める。
// トラックが無い骨・長さ 0 の列は休みの姿勢、長さ 1 の列はずっと同じ値。列が frames より短いときは
// 最後のサンプルで止める (壊れたファイルでも配列の外を読まない)。
void SampleClip(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, float time, bool loop,
                Pose& out);
// クリップの形の重みの曲線 `track` (Clip::morphs の番号) の `time` 秒の値 (SampleClip と同じ刻みの読み方)。
float SampleMorph(const space3d::Clip& clip, int track, float time, bool loop);
// 骨 `bone` 1 本だけの SampleClip (同じ式。ルートモーションが根を何度も読む)。範囲の外の番号: 単位の値。
space3d::BoneTRS SampleBone(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int bone, float time,
                            bool loop);

// クロスフェード: 骨ごとに LerpTRS(a, b, w)。`out` は a と b のどちらと同じでもよい。
// 数が違うときは、短い方に無い骨は長い方の値のまま。
void Blend(const Pose& a, const Pose& b, float w, Pose& out);

// 重みの付いた姿勢をいくつも足し合わせる (ブレンド)。重みの合計で割って使う (合計が 1 でなくてよい)。
//   回転は、最初に足した姿勢の回転と向きをそろえて (QuatDot < 0 なら -q) 足し、最後に長さを 1 に直す。
//   重みが 0 以下の姿勢は足さない。合計が 0 なら休みの姿勢 (骨ごと: 足した姿勢が短くて届かなかった骨も)。
// 使い回せる (Begin でまた空にする。配列は持ったまま)。
class PoseAccumulator {
public:
    void Begin(size_t boneCount);
    void Add(const Pose& pose, float weight);
    void Finish(const space3d::Skeleton& skeleton, Pose& out) const;
    float TotalWeight() const { return total_; }

private:
    std::vector<Vec3> t_, s_;
    std::vector<space3d::Quat> r_;
    std::vector<float> w_;  // 骨ごとの重みの合計
    float total_ = 0;
};

// --- 重ねる (レイヤー。docs/ANIMATION.md 段階 4) ---
// マスク: `roots` の名前の骨と、その下の骨すべてが 1、ほかは 0 (骨の数と同じ並び)。`roots` が空: 全部 1。
// 無い名前は無視する (全部が無い名前なら全部 0)。
std::vector<float> BoneMask(const space3d::Skeleton& skeleton, std::span<const std::string> roots);
// 上書きのレイヤー: 骨ごとに base = LerpTRS(base, layer, weight x mask[骨])。mask が空: 全部 1。
void OverlayPose(Pose& base, const Pose& layer, float weight, std::span<const float> mask);
// 足すレイヤー: 休みの姿勢 `rest` からの差を足す。骨ごとに (w = weight x mask[骨])
//   位置 += (layer.t - rest.t) x w、回転 = base.r x nlerp(1, rest.r⁻¹ x layer.r, w)、大きさ x= lerp(1, layer.s / rest.s, w)。
void AddPose(Pose& base, const Pose& layer, const Pose& rest, float weight, std::span<const float> mask);

// モデルの空間の骨の行列: 親の行列 x TRSMatrix(姿勢)。親が先に並んでいるので 1 回のループ。
// 姿勢が骨の数より短いときは、足りない骨は休みの姿勢。親の番号が自分より後 (壊れた木) なら根として扱う。
void ModelMatrices(const space3d::Skeleton& skeleton, const Pose& pose, std::vector<space3d::Mat4>& out);
// スキンの行列 = 骨の行列 x 逆バインド行列 (GPU に送る 3x4)。逆バインド行列が足りない骨は骨の行列のまま。
void SkinMatrices(std::span<const space3d::Mat4> model, std::span<const space3d::Mat4> inverseBind,
                  std::vector<space3d::BoneMatrix>& out);

}  // namespace rm::animation3d
