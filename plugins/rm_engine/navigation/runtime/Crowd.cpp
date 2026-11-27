#include "rm_engine/navigation/runtime/Crowd.h"

#include "rm_engine/navigation/runtime/DetourShared.h"

#pragma warning(push, 0)
#include "DetourCommon.h"
#include "DetourCrowd.h"
#include "DetourNavMesh.h"
#pragma warning(pop)

#include <algorithm>
#include <cmath>

namespace rm::navigation {

namespace {

constexpr int kMaxPathCorners = 128;  // Path / Remaining が数える角の数の上限

struct Free {
    void operator()(dtCrowd* p) const { dtFreeCrowd(p); }
};

Vec3 Get(const float* f) { return {f[0], f[1], f[2]}; }
float Dist(Vec3 a, Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

dtCrowdAgentParams ParamsOf(const AgentParams& p) {
    dtCrowdAgentParams d{};
    d.radius = std::max(0.f, std::isfinite(p.radius) ? p.radius : 0.3f);
    d.height = std::max(0.05f, std::isfinite(p.height) ? p.height : 1.8f);
    d.maxSpeed = std::max(0.f, std::isfinite(p.maxSpeed) ? p.maxSpeed : 0.f);
    d.maxAcceleration = std::max(0.f, std::isfinite(p.maxAcceleration) ? p.maxAcceleration : 0.f);
    d.collisionQueryRange = std::max(d.radius * 12.f, 1.f);
    d.pathOptimizationRange = std::max(d.radius * 30.f, 3.f);
    d.separationWeight = std::clamp(std::isfinite(p.separation) ? p.separation : 1.f, 0.f, 4.f);
    d.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
    if (p.avoid > 0) d.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    d.obstacleAvoidanceType = static_cast<unsigned char>(p.avoid >= 2 ? 2 : 1);  // (Init で 1 = ふつう・2 = ていねい を決める)
    d.queryFilterType = 0;
    return d;
}

}  // namespace

struct NavCrowd::Impl {
    std::unique_ptr<dtCrowd, Free> crowd;
    NavMesh* mesh = nullptr;
    int max = 0;

    const dtCrowdAgent* Agent(int i) const {
        if (!crowd || i < 0 || i >= max) return nullptr;
        const dtCrowdAgent* a = crowd->getAgent(i);
        return a && a->active ? a : nullptr;
    }
    // 通路の角 (今の所から終わりまで。始めは入れない)。
    int Corners(const dtCrowdAgent& a, float* verts, unsigned char* flags) const {
        if (a.targetState != DT_CROWDAGENT_TARGET_VALID || a.corridor.getPathCount() == 0) return 0;
        dtPolyRef polys[kMaxPathCorners];
        auto& corridor = const_cast<dtPathCorridor&>(a.corridor);
        return corridor.findCorners(verts, flags, polys, kMaxPathCorners, const_cast<dtNavMeshQuery*>(crowd->getNavMeshQuery()), crowd->getFilter(0));
    }
};

NavCrowd::NavCrowd() : impl_(std::make_unique<Impl>()) {}
NavCrowd::~NavCrowd() = default;

bool NavCrowd::Init(NavMesh& mesh, int maxAgents, float maxRadius) {
    impl_ = std::make_unique<Impl>();
    auto* nav = static_cast<dtNavMesh*>(mesh.Native());
    if (!nav || maxAgents <= 0) return false;
    std::unique_ptr<dtCrowd, Free> c(dtAllocCrowd());
    if (!c || !c->init(maxAgents, std::max(0.05f, maxRadius), nav)) return false;
    // よけ方 (Detour の見本と同じ値): 1 = ふつう (速さの候補 5 方向 x 2 輪・細かく 2 回)、2 = ていねい (7 方向 x 3 輪・3 回)。
    dtObstacleAvoidanceParams oa = *c->getObstacleAvoidanceParams(0);
    oa.velBias = 0.5f;
    oa.adaptiveDivs = 5, oa.adaptiveRings = 2, oa.adaptiveDepth = 1;
    c->setObstacleAvoidanceParams(0, &oa);
    oa.adaptiveDivs = 5, oa.adaptiveRings = 2, oa.adaptiveDepth = 2;
    c->setObstacleAvoidanceParams(1, &oa);
    oa.adaptiveDivs = 7, oa.adaptiveRings = 3, oa.adaptiveDepth = 3;
    c->setObstacleAvoidanceParams(2, &oa);
    impl_->crowd = std::move(c);
    impl_->mesh = &mesh;
    impl_->max = maxAgents;
    SetFilter({});
    return true;
}

void NavCrowd::SetFilter(const NavFilter& filter) {
    if (!impl_->crowd) return;
    if (dtQueryFilter* q = impl_->crowd->getEditableFilter(0)) detail::ApplyFilter(filter, *q);
}

int NavCrowd::Add(Vec3 position, const AgentParams& params) {
    if (!impl_->crowd) return -1;
    // 近くのナビメッシュの上に置く (無ければ足さない)。
    const auto on = impl_->mesh->ClosestPoint(position, {std::max(params.radius * 4.f, 1.f), std::max(params.height, 2.f), std::max(params.radius * 4.f, 1.f)});
    if (!on) return -1;
    const float p[3] = {on->x, on->y, on->z};
    const dtCrowdAgentParams d = ParamsOf(params);
    return impl_->crowd->addAgent(p, &d);
}

void NavCrowd::Remove(int agent) {
    if (impl_->Agent(agent)) impl_->crowd->removeAgent(agent);
}

void NavCrowd::SetParams(int agent, const AgentParams& params) {
    if (!impl_->Agent(agent)) return;
    const dtCrowdAgentParams d = ParamsOf(params);
    impl_->crowd->updateAgentParameters(agent, &d);
}

bool NavCrowd::SetTarget(int agent, Vec3 target) {
    if (!impl_->Agent(agent)) return false;
    const float* half = impl_->crowd->getQueryHalfExtents();
    float c[3] = {target.x, target.y, target.z}, nearest[3];
    // (行き先は少し広く探す: 高い所・低い所をクリックしても、その下や上の床へ)
    const float wide[3] = {std::max(half[0], 2.f), std::max(half[1], 4.f), std::max(half[2], 2.f)};
    dtPolyRef ref = 0;
    impl_->crowd->getNavMeshQuery()->findNearestPoly(c, wide, impl_->crowd->getFilter(0), &ref, nearest);
    if (!ref) return false;
    return impl_->crowd->requestMoveTarget(agent, ref, nearest);
}

void NavCrowd::ClearTarget(int agent) {
    if (impl_->Agent(agent)) impl_->crowd->resetMoveTarget(agent);
}

void NavCrowd::MoveTo(int agent, Vec3 position) {
    if (!impl_->Agent(agent)) return;
    dtCrowdAgent* a = impl_->crowd->getEditableAgent(agent);
    const float p[3] = {position.x, position.y, position.z};
    if (a->corridor.getPathCount() > 0 && a->state == DT_CROWDAGENT_STATE_WALKING) {
        a->corridor.movePosition(p, const_cast<dtNavMeshQuery*>(impl_->crowd->getNavMeshQuery()), impl_->crowd->getFilter(0));
        dtVcopy(a->npos, a->corridor.getPos());
    }
}

void NavCrowd::Update(float dt) {
    if (impl_->crowd && dt > 0 && std::isfinite(dt)) impl_->crowd->update(dt, nullptr);
}

AgentView NavCrowd::View(int agent) const {
    AgentView v;
    const dtCrowdAgent* a = impl_->Agent(agent);
    if (!a) return v;
    v.onMesh = a->state != DT_CROWDAGENT_STATE_INVALID;
    v.partial = a->partial;
    v.position = Get(a->npos);
    v.velocity = Get(a->vel);
    v.desired = Get(a->dvel);
    switch (a->targetState) {
        case DT_CROWDAGENT_TARGET_NONE: v.target = AgentView::Target::None; break;
        case DT_CROWDAGENT_TARGET_FAILED: v.target = AgentView::Target::Failed; break;
        case DT_CROWDAGENT_TARGET_VALID: v.target = AgentView::Target::Moving; break;
        case DT_CROWDAGENT_TARGET_VELOCITY: v.target = AgentView::Target::Moving; break;
        default: v.target = AgentView::Target::Pending; break;
    }
    v.goal = a->corridor.getPathCount() > 0 ? Get(a->corridor.getTarget()) : v.position;
    return v;
}

void NavCrowd::Path(int agent, std::vector<Vec3>& out) const {
    out.clear();
    const dtCrowdAgent* a = impl_->Agent(agent);
    if (!a) return;
    out.push_back(Get(a->npos));
    float verts[kMaxPathCorners * 3];
    unsigned char flags[kMaxPathCorners];
    const int n = impl_->Corners(*a, verts, flags);
    for (int i = 0; i < n; ++i) out.push_back(Get(&verts[i * 3]));
}

float NavCrowd::Remaining(int agent) const {
    std::vector<Vec3> pts;
    Path(agent, pts);
    float d = 0;
    for (size_t i = 1; i < pts.size(); ++i) d += Dist(pts[i - 1], pts[i]);
    return d;
}

}  // namespace rm::navigation
