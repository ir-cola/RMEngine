#pragma once
// 3D アニメーション - 1 体のクリップの再生 (段階 1: docs/ANIMATION.md 5 章)。1 度に流すクリップは 1 つで、
// 切り替えるときだけ、前の姿勢からクロスフェードする。後で作るつなぎ (ゲーム制作 × アニメーション) は
// エンティティごとに Player を 1 つ持ち、毎フレーム Tick → Evaluate を呼ぶ。
// 純粋 (描画もファイルもない)。tests/TestAnimation3d.cpp でテストする。
//
//   Player p;
//   p.Bind(skin.skeleton, clips);     // 骨の木とクリップの一覧が変わったときだけ
//   p.Play(0);                        // クリップ 0 を始めから (Play(1, 0.25f): 0.25 秒かけて 1 へ)
//   毎フレーム: p.Tick(dt);  p.PassedEvents(events, fired);  p.Evaluate(skin, boneMatrices);
//
// クロスフェード: Play した瞬間の姿勢 (前のフェードの途中でも、見えていたもの) を固めて、そこから新しい
// クリップへ fade 秒かけて移る。前のクリップは進めない。
//
// 混ぜる (段階 4): SetBlend でいくつものクリップを重みで混ぜる (歩き ↔ 走り)。足をそろえる: どのクリップも
// 同じ割合の時刻 (主のクリップ = Play したものの 時刻 / 長さ) で読み、その割合は「重みで混ぜた長さ」で進む。
// 重ねる (段階 4): EvaluatePose で姿勢だけ作り、MutablePose にレイヤーを重ねて (Pose.h の OverlayPose /
// AddPose)、FinishPose で行列にする。

#include "rm_engine/animation3d/runtime/ClipTime.h"
#include "rm_engine/animation3d/runtime/Pose.h"

#include <span>
#include <vector>

namespace rm::animation3d {

class Player {
public:
    float speed = 1.f;  // 負: 逆再生
    bool loop = true;

    // 混ぜるクリップ 1 つ: クリップの番号と重み (重みは合計 1 でなくてよい: 合計で割る)。
    struct Motion {
        int clip = -1;
        float weight = 0;
    };
    // 混ぜるクリップ (毎フレーム重みを変えてよい)。空: Play したクリップだけ。Play しても残る (外すときは空を渡す)。
    void SetBlend(std::span<const Motion> motions);

    // 骨の木とクリップの一覧をつなぐ (名前から番号を引く)。`clips` は持たないので、Player より長く置いておく。
    // Evaluate に渡すスキンの骨の木は、ここで渡したものと同じにする。流していたクリップは止める。
    void Bind(const space3d::Skeleton& skeleton, std::span<const space3d::Clip> clips);

    // `clipIndex` を始めから流す (同じクリップでも始めから。ループしない逆再生 (speed < 0) なら終わりから)。
    // -1 か範囲の外: 止めて休みの姿勢へ。`fade` 秒かけて今の姿勢から移る (0 以下: すぐ)。
    void Play(int clipIndex, float fade = 0);

    // 時刻を dt x speed 進める (フェードは dt で進む: 速さに依らない)。止まっているときは何もしない。
    TimeStep Tick(float dt);
    // 直前の Tick の間に通ったイベントの番号を `out` に足す (ClipTime.h の PassedEvents。Play の後の最初の
    // Tick は、始めの時刻ちょうどのイベントも出す)。`eventTimes` は今のクリップのイベントの時刻 (秒)。
    void PassedEvents(std::span<const float> eventTimes, std::vector<int>& out) const;

    // 今の姿勢 → 骨の行列 → スキンの行列 (GPU へ)。= EvaluatePose → FinishPose。
    void Evaluate(const space3d::SkinData& skin, std::vector<space3d::BoneMatrix>& out);
    // 今の姿勢 (混ぜたもの・フェードの途中のもの) だけ。レイヤーはこれに重ねる。
    const Pose& EvaluatePose(const space3d::Skeleton& skeleton);
    Pose& MutablePose() { return pose_; }
    // MutablePose の姿勢 → 骨の行列 → スキンの行列。
    void FinishPose(const space3d::SkinData& skin, std::vector<space3d::BoneMatrix>& out);
    // 混ぜたクリップの長さ (重みで混ぜたもの。混ぜていなければ今のクリップの長さ。無ければ 0)。
    float BlendDuration() const;

    int ClipIndex() const { return clip_; }  // -1: 止まっている
    float Time() const { return time_; }
    // 時刻を直に置く (時間のつまみ。ループなら [0, duration) に回す)。イベントは出さない。
    void SetTime(float time);
    // フェードの進み (0: Play した瞬間 .. 1: 新しいクリップだけ)。フェードしていなければ 1。
    float FadeWeight() const;
    // 直前の Evaluate の姿勢と骨の行列 (モデルの空間。「骨に付ける」が使う)。
    const Pose& CurrentPose() const { return pose_; }
    const std::vector<space3d::Mat4>& ModelMatrices() const { return model_; }

private:
    const space3d::Clip* Current() const;

    std::span<const space3d::Clip> clips_;
    std::vector<ClipBinding> bindings_;
    int clip_ = -1;
    float time_ = 0;
    float prevTime_ = 0;  // 直前の Tick の前の時刻
    TimeStep last_;       // 直前の Tick
    bool fresh_ = false;      // Play の後、まだ Tick していない
    bool lastFresh_ = false;  // 直前の Tick が Play の後の最初のもの
    float fadeLength_ = 0, fadeLeft_ = 0;
    std::vector<Motion> blend_;
    PoseAccumulator mix_;
    Pose part_;
    Pose from_, sample_, pose_;
    std::vector<space3d::Mat4> model_;
};

}  // namespace rm::animation3d
