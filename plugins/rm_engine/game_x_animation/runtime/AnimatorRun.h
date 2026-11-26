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

#include "rm_engine/animation/runtime/Rules.h"
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

    // 状態とルール (.rmanim。Animator::rules): 読んだもの (null: 無い・読めない)、そのアセット id、読めなかったか。
    std::string rulesId;
    bool rulesFailed = false;
    std::shared_ptr<const animation::CompiledController> rules;
    animation::ParamValues params;  // パラメーターの今の値 (スクリプトが入れる)
    int state = -1;                 // 今の状態 (rules->states の番号。-1: 無し)
    float stateClock = 0;           // 今の状態に入ってからの秒
    float lastTime = 0;             // 前のフレームのクリップの時刻 (ループが 1 周したかを見る)
    bool looped = false;            // 今の状態のループのクリップが 1 周した
    int rule = -1;                  // 最後に当たった行 (-1: 無し)
    std::vector<animation::RuleCheck> checks;  // 最後に調べた行ごとの結果 (「アニメーション」タブが光らせる)
    // スクリプトがルールを読む前に入れた値 (読んだときに入れる)。
    std::vector<std::pair<std::string, float>> pendingParams;
    // 混ぜる状態の、今のクリップごとの重み (状態の motions の並び。「アニメーション」タブが出す)。
    std::vector<float> blend;
    // 重ねるレイヤー (段階 4。rules->layers と同じ並び): それぞれ自分の再生・状態・ルール。
    struct Layer {
        animation3d::Player player;
        int state = -1;
        float lastTime = 0;
        bool looped = false;
        int rule = -1;
        std::vector<animation::RuleCheck> checks;
        std::vector<float> blend;
        std::vector<float> mask;  // 骨ごと (モデルを読んだときに作る)
        float weight = 0;         // 今の重み
    };
    std::vector<Layer> layers;
    animation3d::Pose rest;  // 休みの姿勢 (足すレイヤーの基準)

    // 出すもの: スキンの行列 (GPU へ)、骨の行列 (モデルの空間。骨に付ける・BonePosition)、姿勢の版、動いたか。
    std::vector<space3d::BoneMatrix> bones;
    uint64_t version = 0;  // 0: まだ骨の行列が無い
    bool moving = false;

    // ルートモーション (段階 5。docs/ANIMATION.md 7-1)。体だけが動かす (レイヤーは根を消すだけ)。
    animation3d::RootDelta root;  // 直前の StepAnimator で根が動いた分 (モデルの空間。止まっている: 0)
    bool rootActive = false;      // ... 動かすクリップに重みがあった
    Vec3 rootWorld{0, 0, 0};      // システムがワールドにしたもの (スクリプトの RootDelta・タブの「今」)
    float rootDt = 0;             // ... その秒数
    bool rootMoved = false;       // 前のフレームに動かした (止まったら 1 度だけ 0 を渡す)
    float rootTravel = 0;         // 動かした横の距離の合計 (システム。1 度だけ知らせる)
    bool toldRoot = false;
    // スクリプトの SetLayerWeight (レイヤーの名前 → 重み。ルールの重みより強い。消すと元に戻る)。
    std::vector<std::pair<std::string, float>> layerWeights;
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

// --- 状態とルール (docs/ANIMATION.md 段階 3) ---
// 状態とルールをつなぐ (null: 外す)。パラメーターは初めの値 (スクリプトが先に入れた値はその上に)、1 つ目の状態
// から始める (その状態のクリップ・ループ・速さを欄に書いて流す)。
void BindRules(AnimatorRun& run, Animator& a, std::shared_ptr<const animation::CompiledController> rules);
// 1 フレーム (StepAnimator の前): 今の状態の t と ended を出してルールを調べ、移るなら先の状態へ (フェードつき)。
void StepAnimatorRules(AnimatorRun& run, Animator& a, float dt);
// 状態へ直に移る (スクリプトの Play: ルールを飛ばす。次のフレームからルールがまた働く)。無い名前: false。
bool PlayState(AnimatorRun& run, Animator& a, std::string_view state, float fade);
// 今の状態の中の割合 (0..1。クリップの長さに対する時刻) と、今の状態の名前 (無し: 空)。
float StateFraction(const AnimatorRun& run);
std::string_view StateName(const AnimatorRun& run);
// パラメーター (ルールを読む前なら、読んだときに入れる)。無い名前: false (読む前は true)。
bool SetRuleParam(AnimatorRun& run, std::string_view name, float value, bool trigger);
float RuleParam(const AnimatorRun& run, std::string_view name);
// レイヤーの重みをスクリプトから (数でない・負の数: 外してルールの重みに戻す)。無い名前でも覚えておく (読んだら効く)。
void SetLayerWeight(AnimatorRun& run, std::string_view layer, float weight);
// 今のレイヤーの重み (無い名前: 0)。
float LayerWeightOf(const AnimatorRun& run, std::string_view layer);

}  // namespace rm::game_x_animation
