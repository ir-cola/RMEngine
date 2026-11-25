#pragma once
// ゲーム制作 × アニメーション - ゲームが動いている間の 1 つのアニメーター (docs/ANIMATION.md 段階 2)。
// Play・書き出したゲームでは、姿勢はここで 1 フレームに 1 回だけ決まる (システム「アニメーション」:
// AnimationSystem.h)。描く画面 (AnimatorPoser) はその骨の行列を使うだけ、スクリプト (RMAnim.h) はここを
// 動かす。止まっているエディターのプレビューはここを使わない (AnimatorPoser のプレビューの時計)。
// 純粋 (描画もファイルもシーンもない)。tests/TestGameAnimation.cpp でテストする。
//
//   1 フレーム:  StepAnimator  時刻を進める → 通ったイベント → 骨の行列 (と、その版)
//   スクリプト:  PlayClip (クロスフェード)・Pause・SetSpeed / SetLoop (部品の欄に書く)・SetTime
//
// 部品の欄 (Animator) は Play 中も見えていて、変えればそのまま効く: クリップの名前を変えると (スクリプトの
// PlayClip も欄を書き換える) そのクリップを流し直す。

#include "rm_engine/animation3d/runtime/Player.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/space3d/runtime/Skeleton.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rm::game_x_animation {

struct AnimatorRun {
    int owner = 0;  // このアニメーターのエンティティ (部品が写されたら、写した先は新しく作る)
    // モデル (システムが読む)。skin が null: まだ読んでいない / 骨が無い。
    std::string model;  // そのアセット id
    bool failed = false;  // 読めなかった (同じモデルのうちは、もう読まない)
    std::shared_ptr<const space3d::SkinData> skin;
    std::shared_ptr<const std::vector<space3d::Clip>> clips;
    animation3d::Player player;

    bool started = false;      // 最初のクリップを流した
    std::string playing;       // 今流しているクリップの欄の値 (Animator::clip。変わったら流し直す)
    float askedFade = 0;       // スクリプトの PlayClip が頼んだ、次に流し直すときのフェード (秒)
    bool asked = false;        // スクリプトが流すことを頼んだ (始めから流す が外れていても流す)
    bool paused = false;       // スクリプトの Pause、または 始めから流す が外れている
    std::string missing;       // 見つからないと知らせたクリップの名前 (システムが知らせる)
    bool dirty = false;        // スクリプトが時刻を置いた: 次の StepAnimator で骨の行列を作り直す
    bool toldPlaying = false;  // 動かしたことを 1 度知らせた (システム)

    // 出すもの: スキンの行列 (GPU へ)、骨の行列 (モデルの空間。骨に付ける・BonePosition)、姿勢の版、動いたか。
    std::vector<space3d::BoneMatrix> bones;
    uint64_t version = 0;  // 0: まだ骨の行列が無い
    bool moving = false;
};

// 部品のゲームの間の再生 (無い・別のエンティティから写されたものなら新しく作る)。
AnimatorRun& RunOf(Animator& a, int entity);

// 読んだモデルをつなぐ (流していたものは止まり、次の StepAnimator で欄のクリップを始めから)。
void BindModel(AnimatorRun& run, std::shared_ptr<const space3d::SkinData> skin, std::shared_ptr<const std::vector<space3d::Clip>> clips);

// 1 フレーム (dt 秒。ゲームの時間の速さを掛けたもの)。通ったイベントを `fired` に足す (そのクリップの
// Clip::events の中のもの。次の BindModel まで持つ)。姿勢が変わったら新しい版を `next` から取る。
// 骨が無い (まだ読んでいない) ときは何もしない。
void StepAnimator(AnimatorRun& run, const Animator& a, float dt, std::vector<const space3d::ClipEvent*>& fired, uint64_t (*next)());

// スクリプトの PlayClip: 欄の clip を `clip` にして、`fade` 秒かけてそれへ移る (同じクリップでも始めから)。
// 一時停止も解く。モデルをまだ読んでいなければ、読んだときに流す。モデルにそのクリップが無いと false
// (読んでいないときは true: 分からない)。
bool PlayClip(AnimatorRun& run, Animator& a, std::string_view clip, float fade);
// 今のクリップの長さ (秒。無ければ 0) と、ループでないクリップが端まで流れたか。
float Duration(const AnimatorRun& run);
bool Ended(const AnimatorRun& run, const Animator& a);
// モデルの空間の骨の行列 (直前の StepAnimator のもの)。名前の骨が無い・まだ無い: null。
const space3d::Mat4* BoneModelMatrix(const AnimatorRun& run, std::string_view bone);

}  // namespace rm::game_x_animation
