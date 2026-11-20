#pragma once
// ユーザーがゲームの 2D シーンビューをどう見ているか: カメラ、選んだギズモ (と刻み)、グリッドのオン / オフ。
// パネルのレイアウトの隣 (UserSettings/Layout.json の "sceneViews" -> "2d") に保存し、プロジェクトには
// 入れない。書くのはここ 1 か所で、保存と読み込みは定義から作る (憲法 9)。インスペクターには出さない。

#include "rm_engine/space2d/editor/Gizmo2D.h"
#include "rm_engine/space2d/runtime/View2D.h"

namespace rm::game_x_2d {

RM_STRUCT()
struct SceneViewState2D {
    RM_PROPERTY() space2d::Camera2D camera;
    RM_PROPERTY() space2d::gizmo2d::Tool tool = space2d::gizmo2d::Tool::Move;
    RM_PROPERTY() space2d::gizmo2d::Space space = space2d::gizmo2d::Space::Local;
    RM_PROPERTY() space2d::gizmo2d::Pivot pivot = space2d::gizmo2d::Pivot::Each;
    RM_PROPERTY() bool grid = true;
    // Play 中: ゲームのスクリプトが描く線 (rm::Debug) - デバッグ ボタン。
    RM_PROPERTY() bool debugDraw = true;
    // プラグインがシーンの上に出す線 (物理: コライダー) を、選んだ物だけでなくすべての物に - 形。
    RM_PROPERTY() bool allShapes = false;
    // ドラッグの刻み (Ctrl を押しているとき、または snapAlways ならいつも。そのとき Ctrl は滑らかに動かす)。
    RM_PROPERTY() bool snapToGrid = true;                     // グリッドの刻みで動かす (近いほど細かい)
    RM_PROPERTY(Min=0.001, Max=100000) float snapMove = 0.25f;  // ... または、この値 (単位) で
    RM_PROPERTY(Min=0.1, Max=180) float snapDegrees = 15.f;
    RM_PROPERTY(Min=0.001, Max=10) float snapScale = 0.1f;     // 始めの拡大縮小に対して
    RM_PROPERTY() bool snapAlways = false;
};

}  // namespace rm::game_x_2d
