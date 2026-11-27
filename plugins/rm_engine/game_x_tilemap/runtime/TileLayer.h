#pragma once
// ゲーム制作 × タイルマップ - 部品「タイルのグリッド」と「タイルの層」。設計: docs/TILEMAP.md 3-2・3-3。
//
//   タイルマップ          タイルのグリッド (マスの大きさ・形)      ← Transform 2D で動かせる・回せる
//    ├ 背景              タイルの層 (タイルセット・並び順 -10)
//    ├ 地面              タイルの層 (並び順 0)
//    └ 前景              タイルの層 (並び順 10)
//
// 層 = エンティティ: 階層で並べ替え・名前・有効 / 無効・Prefab が今の道のまま使える。前後は Transform 2D の
// 並び順、次に層の並び順 (スプライトと同じ列で並ぶ: キャラクターを層の間に挟める)。
// 層は親を順にたどって一番近いグリッドのマス目に乗る (無ければ 1 x 1 の四角)。マスの大きさは 1 か所
// (グリッド) なので、層ごとにずれない (憲法 2)。
// マスは区画 (32 x 32) ごとのランレングスの文字で層の部品の中に持つ (tilemap/runtime/TileStore.h)。

#include "base/Math.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/tilemap/runtime/TileStore.h"

#include <vector>

namespace rm::game_x_tilemap {

RM_ENUM()
enum class TileShape {
    RM_ITEM(Label="四角") Square,
};

RM_STRUCT(Label="タイルのグリッド")
struct TileGrid {
    RM_PROPERTY(Edit, Label="マスの大きさ", Tooltip="1 マスの幅と高さ (単位。1 単位 = 物理の 1 m)。子のタイルの層は全部このマス目に乗ります", Min=0.001, Max=100000)
    Vec2 cellSize{1, 1};
    RM_PROPERTY(Edit, Label="形", Tooltip="マスの形 (今は四角だけ。アイソメ・六角は後で足します)") TileShape shape = TileShape::Square;
};

RM_STRUCT(Label="タイルの層")
struct TileLayer {
    RM_PROPERTY(Edit, Label="タイルセット", Tooltip="塗るタイルの組 (.rmtileset)。アセットのパネルの「作成 ▸ タイルセット」で作ります", AssetKind=".rmtileset")
    AssetRef tileset;
    RM_PROPERTY(Edit, Label="並び順", Tooltip="Transform 2D の並び順が同じ物の中で、大きいほど手前に描きます (スプライトの並び順と同じ列)", Min=-100000, Max=100000)
    int order = 0;
    RM_PROPERTY(Edit, Label="色", Tooltip="タイルの色に掛けます (白でそのまま)。透明度も効きます", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY() int chunk = tilemap::kChunkSize;  // 区画の一辺 (読むときは今の大きさに詰め直す)
    RM_PROPERTY() std::vector<tilemap::TileChunk> chunks;  // 塗ったマス (シーンビューの塗る道具で変える)
};

// エンティティ `entity` から親を順にたどって一番近いグリッドのマスの大きさ (無ければ 1 x 1)。
Vec2 GridCellSize(const game_production::Scene& scene, int entity);
// エンティティの「タイルの層」(無い・型を知らない: null)。
const TileLayer* LayerOf(const game_production::Scene& scene, int entity);

}  // namespace rm::game_x_tilemap
