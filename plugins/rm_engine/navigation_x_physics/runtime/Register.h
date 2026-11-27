#pragma once
// ナビ × 物理 のランタイム部分 (ゲーム制作 × ナビゲーションの口):
//   source  動かないコライダー (剛体の無い物・動き方が「動かない」の物) の形を、ナビメッシュを焼く物に。
//           動く剛体とキャラクターは「動く物」(焼かない)
//   mover   キャラクターのある物は、キャラクターで歩かせる (PhysicsWorld::Move: 坂・段差・押されるのが物理どおり)。
//           キャラクターの Move を呼ぶのはこの 1 か所だけ (NavPorts.h)。ルートモーションで動く物はナビ × アニメーションが先に持つ
//   body    キャラクターの体の形 (型と違うときの印)
// plugins/RuntimePlugins.cpp に並べる。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_x_navigation/runtime/NavPorts.h"

#include <vector>

namespace rm::navigation_x_physics {

void RegisterRuntime(RuntimeRegistry& registry);

// 口の中身 (テスト用に出す)。
void CollectColliders(const game_x_navigation::NavSourceContext& ctx, std::vector<game_x_navigation::NavShape>& out);
bool Moves(const game_production::SceneEntity& e);

}  // namespace rm::navigation_x_physics
