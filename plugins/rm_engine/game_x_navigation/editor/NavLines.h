#pragma once
// ゲーム制作 × ナビゲーション (エディター) - ナビの物を線にする (1 か所: シーンビューの線と「ナビ」タブの地図が同じ物を描く)。
//
//   範囲の箱 (theme::SceneNavVolume)・焼いたナビメッシュの縁 (壁: SceneNavEdge・多角形の境目: SceneNavInner)・
//   ナビのエリアの形 (そのエリアの種類の色)
//
// シーンビュー (口 scene_lines の hostLines): 選んだ物か全部 (帯の「形: 選択 / 全部」)。ナビメッシュの縁は、その
// 「ナビメッシュ」の物を選んでいるときか全部のとき。

#include "editor/plugin/Software.h"
#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_x_navigation/editor/NavPreview.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"

#include <span>

namespace rm::game_x_navigation {

void BoundsLines(const navigation::NavBounds& b, Color color, game_production::DebugDraw& out);
void MeshLines(const NavPreview& p, game_production::DebugDraw& out);
void AreaLines(const game_production::Scene& scene, const Worlds& worlds, std::span<const int> selection, bool all, game_production::DebugDraw& out);
// ナビの動く者: 体の輪 (型の半径)。Play 中は道 (角の点)・今の速さ・望む速さの矢印も。
void AgentLines(const game_production::Scene& scene, const Worlds& worlds, std::span<const int> selection, bool all, game_production::DebugDraw& out);
// 口 scene_lines (hostLines) の中身。
void NavSceneLines(editor::SoftwareHost& host, const game_production::Scene& scene, std::span<const int> selection, bool all,
                   game_production::DebugDraw& out);

}  // namespace rm::game_x_navigation
