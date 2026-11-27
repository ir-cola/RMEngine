#pragma once
// ゲーム制作 × タイルマップ (エディター) - パネル「タイル」(ゲーム制作の口 view_panel)。シーンビューの隣に
// ドッキングして使う。設計: docs/TILEMAP.md 7-2・7-5。
//
//   [パレット | タイルセット]                          tiles.rmtileset
//   パレット      選んだ層のタイルセットの絵の元を、切ったセルの格子で出す。クリックで 1 つ、ドラッグで四角
//                 (スタンプ)。選んだものはシーンビューの筆 / 四角が塗る (TileBrush)
//   タイルセット  絵の元の一覧 (足す・消す) と、選んだ絵の元の欄 (画像・タイルの大きさ・余白・間) をインスペクター
//                 と同じ描き方で。切り方は絵の上の格子ですぐ見える。編集は .rmtileset の中身の Undo の 1 件ずつ

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_x_tilemap {

std::unique_ptr<game_production::SceneView> CreateTilePanel();

}  // namespace rm::game_x_tilemap
