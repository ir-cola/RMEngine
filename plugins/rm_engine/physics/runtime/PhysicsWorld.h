#pragma once
// 物理 - 再生しているシーンの物理ワールド: 薄い壁の向こうの Jolt Physics (このプラグインの .cpp
// ファイルの外は、Jolt の型を見ない - 設計 NewEngineBluePrint 9-f)。再生中のシーン 1 つにつき 1 つで、
// シーン自身が持つ (Scene::PlayState: ■ で再生するコピーはそのワールドと一緒に捨てる; 切り替えた先の
// シーンは新しいものを持つ)。
//
//   固定ステップごと (システム 物理、SystemPhase::Fixed - スクリプトの Tick のあと):
//     1 部品の通りに物体を作る (作る、変わったら作り直す、エンティティ / 部品 / 有効 が無くなれば消す);
//       スクリプトが Transform を置いたなら、物体もそこへ行く。ほかのプラグインの動かない形 (口
//       static_shapes: タイルマップの区画) も、版の変わった物だけ作り直す (StaticShapes.h)
//     2 ゲーム設定の 物理の刻み で、ゲーム設定の 重力 を使って Jolt が進む
//     3 物体が動いた先を Transform に書き戻す (動く 物体、キャラクター)
//     4 触れた / 離れたもの: スクリプトへ HookCall (OnHit / OnTriggerEnter / OnTriggerExit)
//
// 2D の物体 (Transform 2D についた コライダー (2D)) は、平面に留まる Jolt の物体: X と Y に動き、
// Z まわりにだけ回る (Jolt の AllowedDOFs) - 2D と 3D は 1 つのワールドを共有する。

#include "base/Math.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace rm::physics {

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // `scene` に対する `dt` 秒の固定ステップ 1 回。`hooks` / `assets` / `registry`: SystemContext と同じ (null でもよい。
    // `registry` が無いと、ほかのプラグインの動かない形 (口 static_shapes) は作らない)。
    void Step(game_production::Scene& scene, float dt, Vec3 gravity, std::vector<game_production::HookCall>* hooks,
              const game_production::AssetAccess* assets, const RuntimeRegistry* registry = nullptr);

    // --- スクリプト用 (RMPhysics.h)。まだ物体が無いエンティティには、ここで作る (Tick で作った弾に
    // すぐ速さを与えられる)。false / nullopt: 物体が無い (コライダーが無い、力に対するトリガー ...)。
    // 力と衝撃は 動く 物体にだけ働く。
    bool AddForce(game_production::Scene& scene, int entity, Vec3 force, bool impulse);
    std::optional<Vec3> Velocity(game_production::Scene& scene, int entity);
    bool SetVelocity(game_production::Scene& scene, int entity, Vec3 velocity);
    struct RayHit {
        int entity = 0;
        Vec3 point{0, 0, 0}, normal{0, 0, 0};
        float distance = 0;
    };
    // `origin` から `direction` の向きへ、最大 `maxDistance` の距離までのレイが最初に当たるもの
    // (トリガーは除く)。キャラクターにも当たる (その物体に)。`ignoreEntity` (0 以外) のエンティティの物体には当たらない
    // (足の IK が自分のキャラクターの体に当たらないように)。
    std::optional<RayHit> Raycast(Vec3 origin, Vec3 direction, float maxDistance, int ignoreEntity = 0) const;

    // キャラクター (キャラクター): `velocity` (その水平の成分) で、別の指示があるまで歩く; 地面にいる
    // 時にジャンプする; 立っている所。
    bool Move(game_production::Scene& scene, int entity, Vec3 velocity);
    bool Jump(game_production::Scene& scene, int entity, float speed);
    bool Grounded(int entity) const;
    std::optional<Vec3> GroundNormal(int entity) const;

    // いま物体 / キャラクターがいくつあるか (テスト、コンソール用)。
    size_t Bodies() const;
    size_t Characters() const;
    // ほかのプラグインの動かない形の物体の数 (口 static_shapes の持ち主・鍵ごとに 1 つ)。
    size_t StaticPieces() const;
    // エディターの デバッグ パネル用: 起きている (動いている: 他は眠っている) 物体、いま触れている組、
    // 直近のステップにかかった時間。
    size_t ActiveBodies() const;
    size_t TouchingPairs() const;
    double LastStepMs() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// 再生中のシーンの物理ワールド (最初に呼んだ時に作る)。
PhysicsWorld& WorldOf(game_production::Scene& scene);
// シーンのワールドがあればそれ (再生中にステップしたもの); null: 無い (何も作らない)。
PhysicsWorld* FindWorldOf(game_production::Scene& scene);

// システム 物理 (SystemPhase::Fixed): シーンのワールドの PhysicsWorld::Step。
void RunPhysics(game_production::SystemContext& ctx);

}  // namespace rm::physics
