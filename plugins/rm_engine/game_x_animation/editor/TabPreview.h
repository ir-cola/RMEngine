#pragma once
// ゲーム制作 × アニメーション - アニメーション タブの中のプレビュー (docs/ANIMATION.md 7-1。段階 3 で後にしたもの):
//
//   ┌ [その場で | 動かす]  [▶/‖] [■]                                      ┐
//   │            (床の格子の上で、選んだアニメーターのモデルが動く。                │
//   │             ドラッグで回す・右ドラッグで動かす・ホイールで近づく)            │
//   └ 体: Walk ・ 動き 1.40 m/s ・ 回る 0°/s                                 ┘
//
// 止まっているエディターでは、自分の再生 (AnimatorRun。ゲームと同じ StepAnimatorRules → StepAnimator) で
// 状態とルールを動かす。タブのパラメーターの「今」はこの再生の値を変える (Play しなくても試せる)。Play 中は、
// ゲームのアニメーター (システム「アニメーション」が進めたもの) の姿勢を見せる。
// ルートモーション: 「動かす」では根の動いた分だけモデルが回り、床が流れる (カメラがついて行く)。「その場で」は
// 動いた分を捨てる。どちらもゲームの部品の欄は変えない (見せ方だけ)。
// モデルは 3D 空間の MeshLoader で別のスレッドで読み、このプレビューの GPU に上げる (材質の色だけ。絵は貼らない)。

#include "rm_engine/animation/runtime/Rules.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"
#include "rm_engine/space3d/editor/Preview3D.h"
#include "rm_engine/space3d/runtime/MeshLoader.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rm::game_x_animation {

class TabPreview {
public:
    // `r` にプレビューを描く。entity / animator: 選んだアニメーター。rules: タブが解いた状態とルール (変わると
    // 始めからやり直す)。live: Play 中のゲームの再生 (止まっているときは null)。
    void Draw(ui::Context& c, const ui::Rect& r, game_production::SceneViewHost& host, int entity, const Animator& animator,
              const std::shared_ptr<const animation::CompiledController>& rules, AnimatorRun* live);
    // 止まっているときの再生 (状態とルールを読んで動いている物。まだ無い: null)。
    AnimatorRun* Run() { return run_.rules && run_.skin ? &run_ : nullptr; }
    // 見せている物の根の速さ (m/s。ならしたもの) と回る速さ (度/s)。
    float Speed() const { return speed_; }
    float TurnRate() const { return turnRate_; }

private:
    void Load(game_production::SceneViewHost& host, const AssetRef& model);
    void Rebind(const Animator& animator, const std::shared_ptr<const animation::CompiledController>& rules);
    void Restart();

    space3d::Preview3D view_;
    space3d::MeshLoader loader_;
    space3d::MeshLoader::Ticket ticket_ = 0;
    AssetRef model_;
    std::filesystem::path file_;
    std::filesystem::file_time_type time_{}, reading_{};
    std::chrono::steady_clock::time_point checked_{}, last_{};
    std::string error_;
    bool loaded_ = false;  // 1 度は読み終えた
    std::shared_ptr<const space3d::GpuMesh> gpu_;
    uint64_t generation_ = 0;
    std::vector<space3d::DrawMaterial> materials_;
    game_x_3d::SkinnedModel skinned_;

    int entity_ = 0;
    Animator anim_;  // プレビューの部品の写し (ルールが欄を書き換える)
    AnimatorRun run_;
    std::shared_ptr<const animation::CompiledController> rules_;
    std::vector<const space3d::ClipEvent*> fired_;
    bool paused_ = false;
    bool moving_ = false;  // 「動かす」
    Vec3 position_{0, 0, 0};
    float yaw_ = 0;
    float speed_ = 0, turnRate_ = 0;
};

}  // namespace rm::game_x_animation
