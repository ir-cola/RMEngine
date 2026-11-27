#pragma once
// ゲーム制作 × タイルマップ - タイルの層を 2D の絵に描く (ゲーム制作 × 2D の口 rm_engine.game_x_2d.world_draw)。
// シーンビュー・ゲームビュー・書き出したゲームが同じ描き手を使う。設計: docs/TILEMAP.md 5 章。
//
//   層 1 つ x 見えている区画 1 つ x 絵の元 1 つ = スプライトの束 (ID2D1SpriteBatch) 1 つ = 描く命令 1 回
//
// 束は区画ごとに作って覚え、区画の文字・絵 (ビットマップ)・切り方・色が変わったときだけ作り直す。
// 見えている区画だけを描く (カメラの見える四角を層の中へ戻した外接の四角)。束の四角はマスの大きさちょうど、
// 自分の元の四角の外を読まない (隣のタイルがにじまない: CLAMP_TO_SOURCE_RECTANGLE)。

#include "rm_engine/game_x_2d/runtime/World2D.h"

#include <memory>

namespace rm::game_x_tilemap {

std::unique_ptr<game_x_2d::WorldDrawer2D> CreateTileDraw();

}  // namespace rm::game_x_tilemap
