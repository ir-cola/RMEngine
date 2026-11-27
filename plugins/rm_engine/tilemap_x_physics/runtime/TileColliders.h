#pragma once
// タイルマップ × 物理 - 層の区画の当たり (まとめた形) を覚えておく 1 か所。物理に渡す形 (Register.cpp の
// StaticShapeSource) とシーンビューの当たりの線 (editor/Register.cpp) が同じ物を使う (描くものと当たるものを
// 食い違わせない)。区画の文字・タイルセットの当たりの表・マスの大きさが変わったときだけ作り直す。

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"
#include "rm_engine/physics/runtime/StaticShapes.h"
#include "rm_engine/tilemap/runtime/TileCollision.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::tilemap_x_physics {

// アセットの中身を読む (エディター: 今持っている中身 / ゲーム: .rmpak)。無い: nullopt。
using AssetBytes = std::function<std::optional<std::string>(const AssetRef&)>;

class TileColliders {
public:
    // `reread`: タイルセットを読み直してよい (エディターは間を空けて毎回、物理のワールドは 1 回だけ読めば足りる)。
    explicit TileColliders(bool reread) : reread_(reread) {}

    // 層 `entity` の区画ごとの当たり (物理の口の形)。区画の鍵 = (x, y) を 1 つの数にしたもの。
    // タイルセットが無い・読めない・当たりのあるタイルが無い: 空。
    std::vector<physics::StaticPiece> Pieces(const game_production::Scene& scene, int entity, const game_x_tilemap::TileLayer& layer,
                                             const AssetBytes& bytes, double now = 0);
    // 層を使わなくなったら覚えた物を捨てる (`alive` に無い層)。
    void Forget(const std::vector<int>& alive);

private:
    struct Table {
        std::string bytes;
        tilemap::CollisionTable table;
        double readAt = -1;
        bool ok = false, told = false;
    };
    struct Chunk {
        std::string cells;
        uint64_t table = 0;
        Vec2 cellSize{1, 1};
        uint64_t version = 0;
        std::shared_ptr<const physics::StaticGeometry> geometry;
    };
    const Table* TableOf(const AssetRef& ref, const AssetBytes& bytes, double now);
    bool reread_;
    std::unordered_map<std::string, Table> tables_;  // タイルセットの ID ごと
    std::unordered_map<int, std::map<std::pair<int, int>, Chunk>> layers_;
    uint64_t nextVersion_ = 1;
};

// 区画の番号 (x, y) -> 鍵 (と、その逆)。
uint64_t ChunkKey(int x, int y);

}  // namespace rm::tilemap_x_physics
