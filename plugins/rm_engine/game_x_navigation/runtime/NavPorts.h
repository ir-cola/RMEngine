#pragma once
// ゲーム制作 × ナビゲーション - ほかのプラグインに開く口 (設計 docs/NAVIGATION.md 4-3)。
//
//   rm_engine.game_x_navigation.source  焼く物を差し出す (ナビ × 3D: メッシュ・ナビ × 物理: 動かないコライダー・
//                                       あとで 地形 × ナビ / タイルマップ × ナビ)
//   rm_engine.game_x_navigation.mover   動く者を動かす道を差し出す (ナビ × 物理: キャラクターの PhysicsWorld::Move)
//   (rm_engine.game_x_navigation.grid   マス目のゲームの「このマスは通れるか・費用は」: 段階 5。名前だけ決めてある)
//
//   // つなぎのランタイムの部分 (runtime/Register.cpp) で:
//   static const game_x_navigation::NavSourceInfo kMeshes{"meshes", "メッシュ", "3d", &CollectMeshes, &MeshPrint};
//   registry.Contribute(game_x_navigation::kNavSourcePoint, kMeshes);
//
// **キャラクターの Move を呼ぶのは 1 人だけ**: 動く者を物理のキャラクターで歩かせるのは mover の持ち主だけ。
// ルートモーション (docs/ANIMATION.md 段階 5) もキャラクターを動かすので、同じ物を 2 つが動かさないように、
// ナビの動く者が付いた物では、ナビが道を決めて mover が動かす (ルートモーションは「動いた分」をナビへ返す: 段階 7)。

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/navigation/runtime/NavTypes.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <stop_token>
#include <unordered_map>
#include <vector>

namespace rm::game_x_navigation {

// エンティティのワールド行列 (3D の Transform を持つ物と、その下の物。持たない物は親のもの)。
using Worlds = std::unordered_map<int, space3d::Mat4>;

// 焼く物 1 つ: ワールドの三角形 (表はエンジンの決まり: 外から見て反時計回り)。どのエンティティのものか。
struct NavShape {
    int entity = 0;
    std::vector<Vec3> points;
    std::vector<uint32_t> indices;
};

// 焼く物を集めるときに渡すもの (作業スレッドで: UI スレッドで写したシーンだけを読む)。
struct NavSourceContext {
    const game_production::Scene& scene;
    const Worlds& worlds;
    navigation::NavBounds bounds;                         // この箱に触れる物だけでよい (ワールド)
    const game_production::AssetAccess* assets = nullptr;  // モデルのファイル (null: 読めない)
    std::stop_token stop;
};

inline constexpr char kNavSourcePoint[] = "rm_engine.game_x_navigation.source";
struct NavSourceInfo {
    // 部品「ナビメッシュ」の欄で切り替えるときの名前: "meshes" / "colliders" (その名前の bool の欄。無い名前はいつも焼く)。
    const char* id = "";
    const char* name = "";  // UTF-8・日本語 (エディターが訳す)
    const char* space = "3d";
    void (*collect)(const NavSourceContext& ctx, std::vector<NavShape>& out) = nullptr;
    // 形の指紋に足す (焼き直しが要るか: 「古い」の印)。読む部品の値・ワールドの位置・モデルのファイルの日付。
    // 軽く (ファイルを読まない)。UI スレッドで呼ぶ。
    void (*fingerprint)(const game_production::Scene& scene, const Worlds& worlds, const game_production::AssetAccess* assets,
                        navigation::Hasher& h) = nullptr;
    // 省略可: このエンティティは動く (焼かない: よける相手か障害物にする)。物理の剛体・キャラクター。
    bool (*moves)(const game_production::SceneEntity& e) = nullptr;
};

inline constexpr char kNavMoverPoint[] = "rm_engine.game_x_navigation.mover";
struct NavMoverInfo {
    bool (*owns)(game_production::Scene& scene, int entity) = nullptr;                 // この物はこの道で動かす
    void (*move)(game_production::Scene& scene, int entity, Vec3 velocity) = nullptr;  // 望む速さ (横だけ。次の指示まで)
};

}  // namespace rm::game_x_navigation
