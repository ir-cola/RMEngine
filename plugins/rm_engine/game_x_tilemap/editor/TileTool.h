#pragma once
// ゲーム制作 × タイルマップ (エディター) - 2D のシーンビューの塗る道具 (ゲーム制作 × 2D の口 scene_tool)。
// タイルの層を選んでいるときだけ、帯に 筆 (B)・四角 (U)・消しゴム・スポイト (I) が出る。設計: docs/TILEMAP.md 7-3。
//
//   筆        クリック・ドラッグで、パネル「タイル」で選んだタイル (スタンプ) を塗る。Shift で消す・Alt でスポイト
//   四角      ドラッグした四角をスタンプで敷き詰める。Shift で枠だけ
//   消しゴム  クリック・ドラッグで空にする
//   スポイト  マスのタイルを取る (ドラッグで四角に取るとスタンプ)
//
// ドラッグの間は層の写し (TileStore) と区画の文字を直に書き換え、離したときに Undo の 1 件 (TileEditCommand)。
// Esc で戻す。ドラッグの途中で Undo / Redo があったら、やめる (自分の書いた所だけを戻し、何も積まない)。

#include "rm_engine/game_x_2d/editor/SceneTool2D.h"

#include <memory>

namespace rm::game_x_tilemap {

std::unique_ptr<game_x_2d::SceneTool2D> CreateTileTool();

}  // namespace rm::game_x_tilemap
