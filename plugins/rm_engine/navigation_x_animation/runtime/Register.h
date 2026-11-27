#pragma once
// ナビ × アニメーション のランタイム部分 (docs/NAVIGATION.md 3 章・docs/ANIMATION.md 7-6)。ナビが動かし、アニメーションは
// 速さで合わせる (案 A。既定)。plugins/RuntimePlugins.cpp に並べる。
//
//   param_feed (ゲーム制作 × アニメーションの口)  ナビの動く者の値をアニメーターのパラメーターへ: 速さ・欲しい速さ・体から見た
//                                                 横 / 前の速さ (2D で混ぜる moveX / moveY)・向きの差 (度)・残りの距離・着いた
//   mover (ゲーム制作 × ナビゲーションの口)       ルートモーションで動く物 (DrivenByRootMotion) は、ナビが位置も向きも書かない
//                                                 (動かすのはアニメーション。order -10: キャラクターより先)。ナビは動いた先から
//                                                 道を続ける。歩き方をナビに合わせる (案 B) のは段階 7

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"

namespace rm::navigation_x_animation {

void RegisterRuntime(RuntimeRegistry& registry);

// 口 param_feed の値 (テスト用に出す)。動く者でない・Play していない: 0。
float AgentSpeed(game_production::Scene& scene, int entity);
float AgentDesiredSpeed(game_production::Scene& scene, int entity);
float AgentMoveX(game_production::Scene& scene, int entity);
float AgentMoveY(game_production::Scene& scene, int entity);
float AgentTurn(game_production::Scene& scene, int entity);
float AgentRemaining(game_production::Scene& scene, int entity);
float AgentArrived(game_production::Scene& scene, int entity);

}  // namespace rm::navigation_x_animation
