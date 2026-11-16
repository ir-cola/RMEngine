#pragma once
// ゲーム制作 × パーティクル - the パーティクル tab (a panel of the game software, next to the scene and
// the game: game production's ViewPanelInfo). It makes the effect of the selected entity's パーティクル:
//
//   ┌ fire.rmparticle                         [＋ エミッター] ┐
//   │  the preview: the effect playing over a floor (drag to turn)  │
//   ├ エミッター「火」 ─────────────┬ the chosen module's values ──┤
//   │   ☑ 形: 円すい      ↑ ↓ ×       │  広がる角度 (度)   [ 25 ]    │
//   │   ☑ はじめの値      ↑ ↓ ×       │  出る所の半径      [ 0.1 ]   │
//   │   ＋ モジュール                 │                              │
//
// The emitter's own settings (rate, burst ...) are shown in the インスペクター when its row is chosen
// (from its RM_STRUCT: rule 1). Every edit is one undo entry on the effect's file (its contents before and
// after: SoftwareHost::SetAssetContent - the scene view and Play show it at once; written to the file
// when the edit is finished, as the インスペクター's import settings are).

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_x_particles {

std::unique_ptr<game_production::SceneView> CreateParticleTab();

}  // namespace rm::game_x_particles
