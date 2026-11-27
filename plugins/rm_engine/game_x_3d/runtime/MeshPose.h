#pragma once
// ゲーム制作 × 3D - 骨のあるモデルの姿勢を、ほかのプラグインに尋ねる口 (docs/ANIMATION.md 2 章)。
// ゲーム制作 × 3D はアニメーションを知らない: 骨のあるモデル (GpuMesh::skinned) で描くエンティティごとに、
// 寄せられた「姿勢を決めるもの」(MeshPoser) に、そのスキンの行列と姿勢の版を尋ねるだけ。誰も答えなければ
// 休みの姿勢 (バインドの形) で描く。
//
//   // コネクター (ゲーム制作 × アニメーション) の runtime/Register.cpp で:
//   static const game_x_3d::MeshPoseInfo kPose{&CreateAnimatorPoser};
//   registry.Contribute(game_x_3d::kMeshPosePoint, kPose);
//
// 描く画面ごと (シーンビュー・そのカメラのプレビュー・ゲームビュー・書き出したゲーム) に 1 つずつ作り、
// 1 フレームに 1 回 Begin → 骨のあるモデルの物ごとに Pose。描く道は 1 本 (ApplyPoses): シーンビューも
// 書き出したゲームも同じ関数で MeshDraw::bones / poseVersion を埋める (憲法 3)。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Skeleton.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rm::game_x_3d {

// RuntimeRegistry の口: 中身は MeshPoseInfo (static)。
inline constexpr char kMeshPosePoint[] = "rm_engine.game_x_3d.mesh_pose";

// 骨のあるモデルの CPU の側 (GPU の形 GpuMesh の隣に持つ)。読み直すと新しいものになる (ポインターが
// 変わる: 姿勢を決めるものは、それを見て骨の木とクリップをつなぎ直す)。
struct SkinnedModel {
    std::shared_ptr<const space3d::SkinData> skin;           // null: 骨が無い
    std::shared_ptr<const std::vector<space3d::Clip>> clips;  // ファイルのクリップ (空でもよい)
};
// 読んだモデルから骨とクリップを取り出す (mesh.skin と mesh.clips は空になる。GPU に上げた後で呼ぶ)。
// 骨が無いモデル: skin は null。
SkinnedModel TakeSkinned(space3d::MeshData& mesh);

// 1 フレームに 1 回、姿勢を尋ねる前に渡すもの。
struct MeshPoseFrame {
    const game_production::Scene& scene;
    float dt = 0;          // この画面の前のフレームからの秒数 (ゲームの時間の速さを掛けたもの)
    bool playing = false;  // ゲームが動いている (エディターの Play・書き出したゲーム): 全部が動く
    // 動いていないエディター: 選ばれているもの (プレビューしてよいもの)。
    std::span<const int> selection;
    double seconds = 0;              // この画面のゲームの時間
    const char* picture = nullptr;   // "scene" / "game" / "preview" (null: 書き出したゲーム)。WorldDrawFrame と同じ
    const RuntimeRegistry* registry = nullptr;
};

// 1 つの物の姿勢。
struct MeshPoseAnswer {
    std::span<const space3d::BoneMatrix> bones;  // スキンの行列 (モデルの空間)。空: 答えない (休みの姿勢)
    std::span<const float> morphWeights;          // ブレンドシェイプの重み (SkinData::morphs の並び。空: 形を動かさない)
    uint64_t poseVersion = 0;                     // MeshDraw::poseVersion (0: 毎フレーム変わるかもしれない)
    bool moving = false;                          // 姿勢が動いている (影の地図: MeshDraw::moving)
};

class MeshPoser {
public:
    virtual ~MeshPoser() = default;
    // 1 フレームに 1 回、Pose の前に。
    virtual void Begin(const MeshPoseFrame& f) = 0;
    // エンティティ `entity` を骨のあるモデル `model` (skin は null でない) で描く: その姿勢。bones は次の
    // Begin まで持つ (姿勢を決めるものの持ち物)。
    virtual MeshPoseAnswer Pose(int entity, const SkinnedModel& model) = 0;
    // 自分で動く姿勢がある: エディターは次のフレームにもビューを描き直す。
    virtual bool Animating() const { return false; }
    // 描いた後: エンティティ `entity` が画面の上でどれだけの高さだったか (割合。映らなかった: 0)。遠い物の姿勢を
    // 間引く (docs/ANIMATION.md 7-3)。画面がいくつもあれば、それぞれが知らせる。
    virtual void Seen(int entity, float screenFraction) {
        (void)entity;
        (void)screenFraction;
    }
};

struct MeshPoseInfo {
    std::unique_ptr<MeshPoser> (*create)() = nullptr;
};

// 寄せられたものを 1 つずつ作る (画面ごとに 1 回)。registry が null: 無し。
std::vector<std::unique_ptr<MeshPoser>> MakePosers(const RuntimeRegistry* registry);

// 描く物のうち、骨のあるモデルのものの持ち主 (MeshDraw と同じ番号の並び)。model が null: 骨が無い。
struct PoseOwner {
    int entity = 0;
    const SkinnedModel* model = nullptr;
};

// フレームの姿勢を埋める: 各 posers に Begin を 1 回、骨のあるモデルの物ごとに Pose を尋ね、最初に答えた
// ものの bones / poseVersion / moving を meshes[i] に入れる (誰も答えなければそのまま: 休みの姿勢)。
// 返すのは、どれかが Animating か。owners と meshes は同じ長さ (違えば短い方まで)。
bool ApplyPoses(std::span<const std::unique_ptr<MeshPoser>> posers, const MeshPoseFrame& f, std::span<const PoseOwner> owners,
                std::span<space3d::MeshDraw> meshes);
// 描いた後 (Renderer::MeshScreenSizes。owners と同じ並び): 骨のあるモデルの物ごとに MeshPoser::Seen を知らせる。
void ReportSeen(std::span<const std::unique_ptr<MeshPoser>> posers, std::span<const PoseOwner> owners, std::span<const float> screenSizes);

}  // namespace rm::game_x_3d
