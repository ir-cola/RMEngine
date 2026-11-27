#pragma once
// ゲーム制作 × アニメーション - 足の IK が地面を探す口 (docs/ANIMATION.md 7-2-1)。ゲーム制作 × アニメーション は
// 物理を知らない: 寄せられた答え手 (つなぎ アニメーション × 物理: PhysicsWorld::Raycast) に順に尋ね、最初の答えを
// 使う。誰も答えなければ (物理が無い)、部品「IK」の「地面」の鎖は何もしない (足はクリップどおり)。
//
//   static const game_x_animation::GroundProbeInfo kGround{&CastGround};
//   registry.Contribute(game_x_animation::kGroundPoint, kGround);

#include "base/Math.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_animation/runtime/AnimatorIk.h"

#include <optional>

namespace rm::game_x_animation {

// RuntimeRegistry の口: 項目は GroundProbeInfo (static)。
inline constexpr char kGroundPoint[] = "rm_engine.game_x_animation.ground";

struct GroundProbeInfo {
    // `from` から `dir` (長さ 1) へ `max` までの地面。エンティティ `ignore` (自分のキャラクター) には当たらない。
    std::optional<GroundHit> (*cast)(game_production::Scene& scene, Vec3 from, Vec3 dir, float max, int ignore) = nullptr;
};

}  // namespace rm::game_x_animation
