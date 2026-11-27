// RMNav.h - スクリプト用のナビゲーション (つなぎ ゲーム制作 × ナビゲーション が提供する。中身は Recast / Detour)。
//
// シーンに部品「ナビメッシュ」を置いて「ナビ」タブで焼き、歩かせる物に部品「ナビの動く者」を付ける。そのうえで:
//
//   #include "RMNav.h"
//   rm::NavAgent agent(self);
//   agent.SetDestination(target);        // 行き先 (道は次の刻みから探す)
//   agent.Stop();  agent.Resume();
//   bool waiting = agent.PathPending();  // まだ道を探している
//   bool partial = agent.PathPartial();  // 行き先まで行けない (一番近い所へ行く)
//   float left   = agent.RemainingDistance();
//   rm::Vec3 v   = agent.Velocity();
//   agent.SetSpeed(5.0f);
//   agent.Warp(pos);                     // 道を捨ててその場所へ (ナビメッシュの上の一番近い点)
//
//   std::vector<rm::Vec3> path;
//   if (rm::Nav::FindPath(a, b, path)) { ... }      // 動く者なしで道だけ
//   bool clear = rm::Nav::Raycast(a, b);             // まっすぐ歩けるか
//   rm::Vec3 p  = rm::Nav::ClosestPoint(clicked);
//   rm::Vec3 r  = rm::Nav::RandomPoint(center, 10.0f);
//   rm::Nav::SetAreaCost("沼", 5.0f);                // エリアの費用をゲームの中で変える
//   rm::Nav::SetAreaOpen("立入禁止", true);          // 通れないエリアを通れるように (扉)
//
//   void Enemy::OnEvent(rm::Entity self, const rm::Event& e) {
//       if (e.Is("NavArrived")) { ... }              // 着いた (動く者のエンティティへ)
//   }
//
// ナビメッシュは最初の固定の刻みで読むので、BeginPlay の中ではまだ道を探せない (行き先は覚えておく: 頼んでよい)。
// プラグインが無い・ナビメッシュが無いと何も起きず、取得は 0 を返す。
#pragma once

#include "RM.h"
#include "RMNavAbi.h"

#include <vector>

namespace rm {

namespace Nav {
namespace detail {
inline const RmNavApi* Api() { return Part<RmNavApi>(RM_PART_NAV, RM_PART_NAV_VERSION); }
}  // namespace detail

// 道 (角の点。始めと終わりを含む)。行き先まで行けないときも、一番近い所までの道で true (`partial` が true)。
inline bool FindPath(Vec3 a, Vec3 b, std::vector<Vec3>& out, bool* partial = nullptr) {
    out.clear();
    const RmNavApi* api = detail::Api();
    if (!api) return false;
    const float fa[3] = {a.x, a.y, a.z}, fb[3] = {b.x, b.y, b.z};
    float pts[3 * 256];
    int n = 0;
    const int r = api->findPath(rm::Api()->ctx, fa, fb, pts, 256, &n);
    for (int i = 0; i < n; ++i) out.push_back({pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]});
    if (partial) *partial = r == 2;
    return r != 0;
}
inline bool Raycast(Vec3 a, Vec3 b) {
    const RmNavApi* api = detail::Api();
    const float fa[3] = {a.x, a.y, a.z}, fb[3] = {b.x, b.y, b.z};
    return api && api->raycast(rm::Api()->ctx, fa, fb) != 0;
}
// ナビメッシュの上の一番近い点 (無い: `p` のまま)。
inline Vec3 ClosestPoint(Vec3 p) {
    const RmNavApi* api = detail::Api();
    const float f[3] = {p.x, p.y, p.z};
    float o[3] = {p.x, p.y, p.z};
    if (api) api->closestPoint(rm::Api()->ctx, f, o);
    return {o[0], o[1], o[2]};
}
// `center` から `radius` 以内の歩ける所のでたらめな点 (無い: `center` のまま)。
inline Vec3 RandomPoint(Vec3 center, float radius) {
    const RmNavApi* api = detail::Api();
    const float f[3] = {center.x, center.y, center.z};
    float o[3] = {center.x, center.y, center.z};
    if (api) api->randomPoint(rm::Api()->ctx, f, radius, o);
    return {o[0], o[1], o[2]};
}
inline bool SetAreaCost(const char* name, float cost) {
    const RmNavApi* api = detail::Api();
    return api && api->setAreaCost(rm::Api()->ctx, name, cost) != 0;
}
inline bool SetAreaOpen(const char* name, bool open) {
    const RmNavApi* api = detail::Api();
    return api && api->setAreaOpen(rm::Api()->ctx, name, open ? 1 : 0) != 0;
}
}  // namespace Nav

// 部品「ナビの動く者」のある物。
class NavAgent {
public:
    explicit NavAgent(Entity e) : e_(e) {}
    bool SetDestination(Vec3 target) const {
        const RmNavApi* api = Nav::detail::Api();
        const float f[3] = {target.x, target.y, target.z};
        return api && api->setDestination(rm::Api()->ctx, e_.id, f) != 0;
    }
    bool Stop() const {
        const RmNavApi* api = Nav::detail::Api();
        return api && api->stop(rm::Api()->ctx, e_.id) != 0;
    }
    bool Resume() const {
        const RmNavApi* api = Nav::detail::Api();
        return api && api->resume(rm::Api()->ctx, e_.id) != 0;
    }
    bool SetSpeed(float speed) const {
        const RmNavApi* api = Nav::detail::Api();
        return api && api->setSpeed(rm::Api()->ctx, e_.id, speed) != 0;
    }
    bool Warp(Vec3 p) const {
        const RmNavApi* api = Nav::detail::Api();
        const float f[3] = {p.x, p.y, p.z};
        return api && api->warp(rm::Api()->ctx, e_.id, f) != 0;
    }
    bool OnMesh() const { return State().onMesh != 0; }
    bool HasPath() const { return State().hasPath != 0; }
    bool PathPending() const { return State().pending != 0; }
    bool PathPartial() const { return State().partial != 0; }
    bool Arrived() const { return State().arrived != 0; }
    float RemainingDistance() const { return State().remaining; }
    Vec3 Velocity() const {
        const RmNavAgentState s = State();
        return {s.velocity[0], s.velocity[1], s.velocity[2]};
    }
    Vec3 DesiredVelocity() const {
        const RmNavAgentState s = State();
        return {s.desired[0], s.desired[1], s.desired[2]};
    }

private:
    RmNavAgentState State() const {
        RmNavAgentState s{};
        const RmNavApi* api = Nav::detail::Api();
        if (api) api->state(rm::Api()->ctx, e_.id, &s);
        return s;
    }
    Entity e_;
};

}  // namespace rm
