#pragma once
// 物理 - 口 `rm_engine.physics.static_shapes`: エンティティの部品でない、ほかのプラグインが作る動かない形
// (タイルマップの区画の当たり。後で地形の高さの格子と木のカプセル: docs/TERRAIN.md 3 章・5-5)。
// 物理はタイルも地形も知らない: 持ち主 (エンティティ)・鍵 (区画など)・版・形だけを受け取る。
//
//   // つなぎの runtime/Register.cpp で:
//   static const physics::StaticShapesInfo kShapes{&CreateMySource};
//   registry.Contribute(physics::kStaticShapesPoint, kShapes);
//
// 固定の刻みごとに (システム「物理」の Step の始め)、物理はワールドごとに 1 つ作った StaticShapeSource に
// 今の形を尋ねる。持ち主・鍵ごとに動かない物体 1 つ (形は寄せ集め: StaticCompoundShape) を作り、
// 版が変わったときだけ作り直す。答えに無くなった物は消す。物体は持ち主の場所と向き (Transform 2D / 3D の
// ワールド) に置き、形は持ち主の大きさを掛けて作る。当たった相手 (OnHit の other・光線) は持ち主。
// 物体はつなぎ目の引っかかりを減らす (Jolt の mEnhancedInternalEdgeRemoval)。

#include "base/Math.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace rm::physics {

inline constexpr char kStaticShapesPoint[] = "rm_engine.physics.static_shapes";

// 平面 (2D) の長方形: 持ち主の平面の中 (単位)。厚さは 2D の物体と同じ (Shapes.h の kPlaneHalfDepth)。
struct StaticBox2D {
    Vec2 min, max;
};

// 形の中身。種類ごとの並び (今は 2D の箱と凸の多角形。地形の段階で 高さの格子・カプセル を足す)。
struct StaticGeometry {
    std::vector<StaticBox2D> boxes2D;
    std::vector<std::vector<Vec2>> polygons2D;  // 凸 (3 点以上)
    float friction = 0.5f;
    float bounciness = 0.f;
    bool Empty() const { return boxes2D.empty() && polygons2D.empty(); }
};

// 持ち主の中の 1 つ (区画)。`version` が前と同じなら、物理は `geometry` を見ずに前の物体を使う。
struct StaticPiece {
    uint64_t key = 0;
    uint64_t version = 0;
    std::shared_ptr<const StaticGeometry> geometry;  // (作った側が覚えておけば、毎回作らずに渡せる)
};

struct StaticOwner {
    int entity = 0;  // この場所 (ワールドの行列) に置く。当たった相手もこれ
    std::vector<StaticPiece> pieces;
};

class StaticShapeSource {
public:
    virtual ~StaticShapeSource() = default;
    // 今ある動かない形を `out` に足す。有効でない物は出さない。毎回同じ形なら同じ版を返すこと。
    virtual void Collect(const game_production::Scene& scene, const game_production::AssetAccess* assets, std::vector<StaticOwner>& out) = 0;
};

struct StaticShapesInfo {
    std::unique_ptr<StaticShapeSource> (*create)() = nullptr;  // 物理のワールド (Play のコピー) ごとに 1 つ
};

}  // namespace rm::physics
