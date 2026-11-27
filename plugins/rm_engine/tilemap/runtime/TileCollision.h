#pragma once
// タイルマップ - 当たりをまとめる (設計図 4-5)。区画 (32 x 32 マス) ごとに、当たりの形のあるタイルを
// つながった形にまとめる。物理を知らない (形を出すだけ: 物理に渡すのはつなぎ tilemap_x_physics)。
// 純粋 - tests/TestTilemap.cpp でテストする。
//
//   「全部」と半分の形   マスを縦横 2 つずつに割った細かい格子 (区画で 64 x 64) に塗り、横に続く帯を作って、
//                        すぐ上の帯と同じ幅なら縦にもつなぐ (貪欲な長方形の詰め方: greedy meshing)。
//                        タイルの絵が違っても当たりがつながっていれば 1 つの箱 (床一面 = 箱 1 つ。L 字 = 箱 2 つ)
//   斜め・自分で描いた形 1 マスずつの凸の多角形
//
// マスの値の反転 (左右・上下・斜め) は形にも効く。区画をまたいではまとめない (変えた区画だけ作り直せる)。
// 出す形の座標は層の中の平面 (マスの大きさを掛けたもの。Y が上)。

#include "base/Math.h"
#include "rm_engine/tilemap/runtime/TileCell.h"
#include "rm_engine/tilemap/runtime/Tileset.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace rm::tilemap {

struct CollisionBox {
    Vec2 min, max;
    bool operator==(const CollisionBox&) const = default;
};

struct ChunkCollision {
    std::vector<CollisionBox> boxes;
    std::vector<std::vector<Vec2>> polygons;  // 凸。反時計回り
    bool Empty() const { return boxes.empty() && polygons.empty(); }
};

// タイルセットの当たりの表 (タイル -> 形)。タイルセットごとに 1 回作る。
class CollisionTable {
public:
    CollisionTable() = default;
    explicit CollisionTable(const Tileset& set);
    // マスの値 (反転を含む) の形。当たりの無いタイル: null。
    const TileInfo* Find(TileValue v) const;
    bool Empty() const { return shapes_.empty(); }
    // 表の中身の指紋 (変わったら当たりを作り直す)。
    uint64_t Fingerprint() const { return fingerprint_; }

private:
    std::unordered_map<uint32_t, TileInfo> shapes_;  // (絵の元・行・列) -> 情報
    uint64_t fingerprint_ = 0;
};

// マスの中 (0〜1。Y が上) の形の多角形 (反転を当てた後)。なし: 空。「全部」と半分は四角の 4 点。
std::vector<Vec2> ShapePolygon(const TileInfo& info, TileValue flips);
// 点の並びを包む凸の多角形 (反時計回り)。3 点より少ない・面積が無い: 空。
std::vector<Vec2> ConvexHull(std::vector<Vec2> points);

// 区画 `chunk` のマス (1024。下の行から左から右へ) の当たりをまとめる。
ChunkCollision CollideChunk(std::span<const TileValue> cells, Cell chunk, Vec2 cellSize, const CollisionTable& table);

}  // namespace rm::tilemap
