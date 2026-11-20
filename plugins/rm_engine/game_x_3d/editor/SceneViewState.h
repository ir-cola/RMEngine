#pragma once
// ユーザーが、ゲームの 3D シーンビューをどう見るか: カメラ、選んだギズモとその刻み、影のオン / オフ。
// パネルのレイアウト (UserSettings/Layout.json) の隣に残し、プロジェクトには入れない - ここに
// 1 度だけ書くので、保存と読み込みは定義から作られる (憲法 9)。インスペクターには出さない。

#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/View.h"

namespace rm::game_x_3d {

RM_STRUCT()
struct SceneViewState {
    RM_PROPERTY() space3d::OrbitView camera;
    RM_PROPERTY() space3d::gizmo::Tool tool = space3d::gizmo::Tool::Move;
    RM_PROPERTY() space3d::gizmo::Space space = space3d::gizmo::Space::Local;
    RM_PROPERTY() space3d::gizmo::Pivot pivot = space3d::gizmo::Pivot::Each;
    // ギズモの刻み (Ctrl を押している間。snapAlways のときは常に刻みで、Ctrl でなめらかに動く)。
    RM_PROPERTY(Min=0.001, Max=1000) float snapMove = 0.25f;  // ワールドの単位
    RM_PROPERTY(Min=0.1, Max=180) float snapDegrees = 15.f;
    RM_PROPERTY(Min=0.001, Max=10) float snapScale = 0.1f;   // 始めたときのスケールに対して
    RM_PROPERTY() bool snapAlways = false;
    // ライトの影 (床と物の上): 影 ボタン。
    RM_PROPERTY() bool shadows = true;
    // シーンビューにも出すシーンの 空 (ゲームビューとプレビューは常に出す): 空。
    RM_PROPERTY() bool sky = true;
    // Play 中: ゲームのスクリプトが描く線 (rm::Debug) - デバッグ ボタン。
    RM_PROPERTY() bool debugDraw = true;
    // プラグインがシーンの上に見せる線 (物理: コライダー) を、選んだものだけでなくすべての物に出す - 形。
    RM_PROPERTY() bool allShapes = false;
};

}  // namespace rm::game_x_3d
