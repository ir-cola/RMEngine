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
//
// ルートモーション (段階 5。RootMotion.h): クリップの設定 (Clip::root) で使うクリップは、読んだ姿勢から根の動きを
// 消し (stripRoot)、Tick の間に根が動いた分を RootStep に出す。混ぜているときは、混ぜるクリップごとにその時刻
// (主の割合 x 長さ) で動いた分を出して同じ重みで混ぜる。フェードの間は、Play した瞬間の 1 秒あたりの動き
// (止め絵の速さ) を (1 - 重み) だけ足す (止め絵は動かないので、フェードの間に遅くならない)。

#include "rm_engine/animation3d/runtime/ClipTime.h"
#include "rm_engine/animation3d/runtime/Pose.h"
#include "rm_engine/animation3d/runtime/RootMotion.h"

#include <span>
#include <vector>

namespace rm::animation3d {

class Player {
public:
    float speed = 1.f;  // 負: 逆再生
    bool loop = true;
    // ルートモーションを使うクリップの姿勢から、根の動きを消す (外すと、根はクリップどおりに動いて見える)。
    bool stripRoot = true;

    // 混ぜるクリップ 1 つ: クリップの番号と重み (重みは合計 1 でなくてよい: 合計で割る)。
    struct Motion {
        int clip = -1;
        float weight = 0;
    };
    // 混ぜるクリップ (毎フレーム重みを変えてよい)。空: Play したクリップだけ。Play しても残る (外すときは空を渡す)。
    void SetBlend(std::span<const Motion> motions);

    // ブレンドシェイプ (段階 6): 形の名前と初めの重み (SkinData::morphs / morphDefaults) をつなぐ (Bind の後に。
    // 無いモデルは呼ばなくてよい)。重みは EvaluatePose が姿勢と一緒に決める (混ぜる・フェードも同じ重みで)。
    void BindMorphs(std::span<const std::string> names, std::span<const float> defaults);
    // 今の形の重み (BindMorphs の並び)。レイヤーはこれに重ねる。
    const std::vector<float>& MorphWeights() const { return morph_; }
    std::vector<float>& MutableMorphWeights() { return morph_; }
    // 今のクリップ (混ぜているときはどれか) が形 `k` の曲線を持つ (上書きのレイヤーは、持つ形だけ重ねる)。
    bool MorphAnimated(size_t k) const;
    const std::vector<float>& MorphDefaults() const { return morphDefaults_; }

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
    // 今のクリップの最初のコマの姿勢と形の重み (足すレイヤーの基準「クリップの最初のコマ」)。クリップが無い: 休みの姿勢。
    void FirstFrame(const space3d::Skeleton& skeleton, Pose& out, std::vector<float>& morphs) const;
    // 混ぜたクリップの長さ (重みで混ぜたもの。混ぜていなければ今のクリップの長さ。無ければ 0)。
    float BlendDuration() const;

    // 直前の Tick の間に根が動いた分 (RootMotion.h の RootDelta。モデルの空間)。SetTime の後: 0。
    const RootDelta& RootStep() const { return root_; }
    // 直前の Tick で、ルートモーションを使うクリップ (かフェードの止め絵の速さ) に重みがあった。
    bool RootActive() const { return rootActive_; }
    // クリップ `clip` の根の骨の番号 (使わない・無い: -1) と、1 回で動く分 (RootTotals)。
    int RootBoneOf(int clip) const;
    RootDelta RootTotalsOf(int clip) const;

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
    // クリップ `clip` の prev から step までに根が動いた分と、読んだ姿勢の根を消すこと。
    RootDelta ClipRoot(int clip, float prev, const TimeStep& step) const;
    void Strip(int clip, Pose& pose) const;

    std::span<const space3d::Clip> clips_;
    std::vector<ClipBinding> bindings_;
    const space3d::Skeleton* skeleton_ = nullptr;
    std::vector<int> rootBone_;                // クリップごとの根の骨 (-1: 使わない)
    std::vector<space3d::BoneTRS> rootStart_;  // クリップごとの、始めのコマの根
    RootDelta root_;
    bool rootActive_ = false;
    RootDelta frozenVelocity_;  // フェードの止め絵の 1 秒あたりの動き (Play した瞬間のもの)
    RootDelta lastVelocity_;    // 直前の Tick の 1 秒あたりの動き
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
    // ブレンドシェイプ
    std::vector<std::string> morphNames_;
    std::vector<float> morphDefaults_;
    std::vector<std::vector<int>> morphTrack_;  // クリップごと: 形ごとの Clip::morphs の番号 (-1: 無い)
    std::vector<float> morph_, fromMorph_, sampleMorph_, partMorph_;
    void MorphsAt(int clip, float time, std::vector<float>& out) const;
};

}  // namespace rm::animation3d
