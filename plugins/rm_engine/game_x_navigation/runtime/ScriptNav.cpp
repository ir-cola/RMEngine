// スクリプト API のナビゲーションの部分 (sdk/RMNav.h / RMNavAbi.h)。再生しているシーンのナビの世界に頼む。
// RmApi::ctx はゲーム制作の ScriptWorld。

#include "rm_engine/game_x_navigation/runtime/ScriptNav.h"

#include "rm_engine/game_x_navigation/runtime/NavWorld.h"
#include "rm_engine/game_x_navigation/runtime/sdk/RMNavAbi.h"

#include <algorithm>
#include <limits>

namespace rm::game_x_navigation {

namespace {

using game_production::Scene;

Scene* SceneOf(void* ctx) { return static_cast<game_production::ScriptWorld*>(ctx)->scene; }
bool IdOk(uint64_t id) { return id != 0 && id <= static_cast<uint64_t>(std::numeric_limits<int>::max()); }
Vec3 V(const float* f) { return {f[0], f[1], f[2]}; }
void Put(float* out, Vec3 v) { out[0] = v.x, out[1] = v.y, out[2] = v.z; }

// エンティティに「ナビの動く者」がある。
bool IsAgent(Scene& s, uint64_t e) {
    if (!IdOk(e)) return false;
    const game_production::SceneEntity* en = s.Find(static_cast<int>(e));
    return en && en->FindComponent(reflect::TypeOf<NavAgent>().name);
}

int SetDestinationApi(void* ctx, uint64_t e, const float t[3]) {
    Scene* s = SceneOf(ctx);
    if (!s || !IsAgent(*s, e)) return 0;
    NavWorldOf(*s).SetDestination(static_cast<int>(e), V(t));
    return 1;
}
int StopApi(void* ctx, uint64_t e) {
    Scene* s = SceneOf(ctx);
    if (!s || !IsAgent(*s, e)) return 0;
    NavWorldOf(*s).Stop(static_cast<int>(e));
    return 1;
}
int ResumeApi(void* ctx, uint64_t e) {
    Scene* s = SceneOf(ctx);
    if (!s || !IsAgent(*s, e)) return 0;
    NavWorldOf(*s).Resume(static_cast<int>(e));
    return 1;
}
int SetSpeedApi(void* ctx, uint64_t e, float speed) {
    Scene* s = SceneOf(ctx);
    if (!s || !IsAgent(*s, e)) return 0;
    NavWorldOf(*s).SetSpeed(static_cast<int>(e), speed);
    return 1;
}
int WarpApi(void* ctx, uint64_t e, const float p[3]) {
    Scene* s = SceneOf(ctx);
    return s && IsAgent(*s, e) && NavWorldOf(*s).Warp(*s, static_cast<int>(e), V(p)) ? 1 : 0;
}
int StateApi(void* ctx, uint64_t e, RmNavAgentState* out) {
    Scene* s = SceneOf(ctx);
    if (!s || !out || !IdOk(e)) return 0;
    const NavWorld* w = FindNavWorld(*s);
    const NavWorld::Agent* a = w ? w->FindAgent(static_cast<int>(e)) : nullptr;
    if (!a) return 0;
    out->onMesh = a->index >= 0 && a->view.onMesh ? 1 : 0;
    out->hasPath = a->hasTarget && !a->stopped ? 1 : 0;
    out->pending = a->hasTarget && !a->stopped && (a->pending || a->view.target == navigation::AgentView::Target::Pending) ? 1 : 0;
    out->partial = a->hasTarget && (a->view.partial || a->view.target == navigation::AgentView::Target::Failed) ? 1 : 0;
    out->arrived = a->arrived ? 1 : 0;
    out->remaining = a->remaining;
    Put(out->velocity, a->view.velocity);
    Put(out->desired, a->view.desired);
    return 1;
}
int FindPathApi(void* ctx, const float a[3], const float b[3], float* out, int max, int* count) {
    if (count) *count = 0;
    Scene* s = SceneOf(ctx);
    NavWorld* w = s ? FindNavWorld(*s) : nullptr;
    const NavSettings* settings = s ? s->Setting<NavSettings>() : nullptr;
    NavWorld::Mesh* m = w ? w->MeshAt(V(a)) : nullptr;
    if (!m || !settings) return 0;
    const navigation::NavPath p = m->mesh->FindPath(V(a), V(b), w->Filter(*settings));
    if (!p.Ok()) return 0;
    const int n = std::min(static_cast<int>(p.points.size()), std::max(0, max));
    for (int i = 0; i < n && out; ++i) Put(out + i * 3, p.points[static_cast<size_t>(i)]);
    if (count) *count = n;
    return p.status == navigation::NavPath::Status::Found ? 1 : 2;
}
int RaycastApi(void* ctx, const float a[3], const float b[3]) {
    Scene* s = SceneOf(ctx);
    NavWorld* w = s ? FindNavWorld(*s) : nullptr;
    const NavSettings* settings = s ? s->Setting<NavSettings>() : nullptr;
    NavWorld::Mesh* m = w ? w->MeshAt(V(a)) : nullptr;
    return m && settings && m->mesh->Raycast(V(a), V(b), w->Filter(*settings)) ? 1 : 0;
}
int ClosestPointApi(void* ctx, const float p[3], float out[3]) {
    Scene* s = SceneOf(ctx);
    NavWorld* w = s ? FindNavWorld(*s) : nullptr;
    NavWorld::Mesh* m = w ? w->MeshAt(V(p)) : nullptr;
    const auto q = m ? m->mesh->ClosestPoint(V(p), {2, 4, 2}) : std::nullopt;
    if (!q) return 0;
    Put(out, *q);
    return 1;
}
int RandomPointApi(void* ctx, const float c[3], float radius, float out[3]) {
    auto* sw = static_cast<game_production::ScriptWorld*>(ctx);
    Scene* s = sw ? sw->scene : nullptr;
    NavWorld* w = s ? FindNavWorld(*s) : nullptr;
    const NavSettings* settings = s ? s->Setting<NavSettings>() : nullptr;
    NavWorld::Mesh* m = w ? w->MeshAt(V(c)) : nullptr;
    if (!m || !settings) return 0;
    static uint32_t calls = 0;  // (同じフレームに何度呼んでも違う点)
    const uint32_t seed = static_cast<uint32_t>(sw->frame * 2654435761u) ^ (++calls * 40503u);
    const auto q = m->mesh->RandomPoint(V(c), radius, seed, w->Filter(*settings));
    if (!q) return 0;
    Put(out, *q);
    return 1;
}
int SetAreaCostApi(void* ctx, const char* name, float cost) {
    Scene* s = SceneOf(ctx);
    return s && name && NavWorldOf(*s).SetAreaCost(*s, name, cost) ? 1 : 0;
}
int SetAreaOpenApi(void* ctx, const char* name, int open) {
    Scene* s = SceneOf(ctx);
    return s && name && NavWorldOf(*s).SetAreaOpen(*s, name, open != 0) ? 1 : 0;
}

int CompleteLinkApi(void* ctx, uint64_t e) {
    Scene* s = SceneOf(ctx);
    return s && IdOk(e) && NavWorldOf(*s).CompleteLink(*s, static_cast<int>(e)) ? 1 : 0;
}
int SetLinkEnabledApi(void* ctx, uint64_t e, int enabled) {
    Scene* s = SceneOf(ctx);
    return s && IdOk(e) && NavWorld::SetLinkEnabled(*s, static_cast<int>(e), enabled != 0) ? 1 : 0;
}
int SetAreaClosedApi(void* ctx, uint64_t e, int closed) {
    Scene* s = SceneOf(ctx);
    return s && IdOk(e) && NavWorld::SetAreaClosed(*s, static_cast<int>(e), closed != 0) ? 1 : 0;
}

constexpr RmNavApi kTable{&SetDestinationApi, &StopApi,        &ResumeApi,       &SetSpeedApi,    &WarpApi,        &StateApi,
                          &FindPathApi,       &RaycastApi,     &ClosestPointApi, &RandomPointApi, &SetAreaCostApi, &SetAreaOpenApi};


constexpr RmNavApi2 kTable2{kTable, &CompleteLinkApi, &SetLinkEnabledApi, &SetAreaClosedApi};

}  // namespace

const game_production::ScriptApiPart& ScriptNavPart() {
    static const game_production::ScriptApiPart part{RM_PART_NAV, RM_PART_NAV_VERSION, &kTable};
    return part;
}

const game_production::ScriptApiPart& ScriptNavPart2() {
    static const game_production::ScriptApiPart part{RM_PART_NAV, RM_PART_NAV_VERSION2, &kTable2};
    return part;
}

}  // namespace rm::game_x_navigation
