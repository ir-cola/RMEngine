#include "rm_engine/navigation_x_animation/runtime/Register.h"

#include "rm_engine/game_x_animation/runtime/ParamFeed.h"
#include "rm_engine/game_x_animation/runtime/RootMotionSink.h"
#include "rm_engine/game_x_navigation/runtime/NavPorts.h"
#include "rm_engine/game_x_navigation/runtime/NavWorld.h"

#include <cmath>
#include <numbers>

namespace rm::navigation_x_animation {

namespace {

using game_production::Scene;
using game_x_navigation::NavWorld;

const NavWorld::Agent* AgentOf(Scene& scene, int entity) {
    const NavWorld* w = game_x_navigation::FindNavWorld(scene);
    const NavWorld::Agent* a = w ? w->FindAgent(entity) : nullptr;
    return a && a->index >= 0 ? a : nullptr;
}

float Flat(Vec3 v) { return std::sqrt(v.x * v.x + v.z * v.z); }

// 体の向き (+Z が前) で見た (横, 前)。
Vec2 Body(const NavWorld::Agent& a, Vec3 v) {
    const float y = a.yaw * std::numbers::pi_v<float> / 180.f;
    const float fx = std::sin(y), fz = std::cos(y);  // 前
    const float rx = fz, rz = -fx;                    // 右 (左手系で Y が上)
    return {v.x * rx + v.z * rz, v.x * fx + v.z * fz};
}

bool OwnsByRootMotion(Scene& scene, int entity) { return game_x_animation::DrivenByRootMotion(scene, entity); }
void LeaveToAnimation(Scene&, int, Vec3) {}  // (動かすのはアニメーションのシステムとルートモーションの行き先)

}  // namespace

float AgentSpeed(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    return a ? Flat(a->view.velocity) : 0.f;
}
float AgentDesiredSpeed(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    return a ? Flat(a->view.desired) : 0.f;
}
float AgentMoveX(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    return a ? Body(*a, a->view.velocity).x : 0.f;
}
float AgentMoveY(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    return a ? Body(*a, a->view.velocity).y : 0.f;
}
float AgentTurn(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    if (!a || Flat(a->view.desired) < 1e-3f) return 0.f;
    const Vec2 d = Body(*a, a->view.desired);
    return std::atan2(d.x, d.y) * 180.f / std::numbers::pi_v<float>;  // (右が +)
}
float AgentRemaining(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    return a ? a->remaining : 0.f;
}
float AgentArrived(Scene& scene, int entity) {
    const NavWorld::Agent* a = AgentOf(scene, entity);
    return a && a->arrived ? 1.f : 0.f;
}

void RegisterRuntime(RuntimeRegistry& registry) {
    // ナビの動く者の値 → アニメーターのパラメーター (部品「アニメーター」の「パラメーターの送り手」で選ぶ)。
    static const game_x_animation::ParamFeedInfo kFeeds[] = {
        {"rm_engine.navigation_x_animation.speed", "ナビ: 速さ (m/s)", &AgentSpeed},
        {"rm_engine.navigation_x_animation.desired_speed", "ナビ: 欲しい速さ (m/s)", &AgentDesiredSpeed},
        {"rm_engine.navigation_x_animation.move_x", "ナビ: 横の速さ (体から見て。右が +)", &AgentMoveX},
        {"rm_engine.navigation_x_animation.move_y", "ナビ: 前の速さ (体から見て)", &AgentMoveY},
        {"rm_engine.navigation_x_animation.turn", "ナビ: 向きの差 (度。右が +)", &AgentTurn},
        {"rm_engine.navigation_x_animation.remaining", "ナビ: 残りの距離 (m)", &AgentRemaining},
        {"rm_engine.navigation_x_animation.arrived", "ナビ: 着いた (1 / 0)", &AgentArrived},
    };
    for (const auto& f : kFeeds) registry.Contribute(game_x_animation::kParamFeedPoint, f);
    // ルートモーションで動く物: ナビは位置も向きも書かない (動かす人は 1 人だけ)。キャラクター (order 0) より先に持つ。
    static const game_x_navigation::NavMoverInfo kMover{&OwnsByRootMotion, &LeaveToAnimation, -10, true,
                                                        "「{}」はルートモーションで動きます (ナビは道を決め、動かすのはクリップ)"};
    registry.Contribute(game_x_navigation::kNavMoverPoint, kMover);
}

}  // namespace rm::navigation_x_animation

RM_RUNTIME_ENTRY(rm_engine, navigation_x_animation, rm::navigation_x_animation::RegisterRuntime)
