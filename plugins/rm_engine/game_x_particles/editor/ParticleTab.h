#pragma once
// ゲーム制作 × パーティクル - パーティクル タブ (ゲームのソフトのパネル。シーンとゲームの隣:
// ゲーム制作 の ViewPanelInfo)。選んだエンティティの パーティクル のエフェクトを作る:
//
//   ┌ fire.rmparticle                         [＋ エミッター] ┐
//   │  プレビュー: 床の上で動くエフェクト (ドラッグで回す)  │
//   ├ エミッター「火」 ─────────────┬ 選んだモジュールの値 ───────┤
//   │   ☑ 形: 円すい      ↑ ↓ ×       │  広がる角度 (度)   [ 25 ]    │
//   │   ☑ はじめの値      ↑ ↓ ×       │  出る所の半径      [ 0.1 ]   │
//   │   ＋ モジュール                 │                              │
//
// エミッター自身の設定 (rate、burst ...) は、その行を選ぶとインスペクターに出る (その RM_STRUCT から:
// 憲法 1)。編集はどれも、エフェクトのファイルへの Undo の 1 件 (前と後の中身: SoftwareHost::
// SetAssetContent - シーンビューと Play はすぐそれを映す。編集が終わったときファイルに書く。
// インスペクターの取り込みの設定と同じ)。

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_x_particles {

std::unique_ptr<game_production::SceneView> CreateParticleTab();

}  // namespace rm::game_x_particles
