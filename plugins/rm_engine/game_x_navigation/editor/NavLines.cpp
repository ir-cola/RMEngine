#include "rm_engine/game_x_navigation/editor/NavLines.h"

#include "editor/widgets/Theme.h"
#include "rm_engine/game_x_navigation/runtime/NavWorld.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::game_x_navigation {

namespace {

namespace theme = editor::theme;
using game_production::DebugDraw;
using game_production::DebugSpace;

constexpr float kLift = 0.04f;  // 床に重ならないように、縁の線を少し上げる (m)

bool Chosen(std::span<const int> selection, bool all, int id) { return all || std::find(selection.begin(), selection.end(), id) != selection.end(); }

template <class T>
const T* Find(const game_production::SceneEntity& e) {
    const game_production::Component* c = e.FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
}

}  // namespace

void BoundsLines(const navigation::NavBounds& b, Color color, DebugDraw& out) {
    if (!b.Valid()) return;
    auto at = [&](int i) { return Vec3{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z}; };
    for (int i = 0; i < 8; ++i) {
        for (const int bit : {1, 2, 4}) {
            if (!(i & bit)) out.Add(DebugSpace::Space3D, at(i), at(i | bit), color, 0);
        }
    }
}

void MeshLines(const NavPreview& p, DebugDraw& out) {
    if (!p.ok) return;
    const Color edge = theme::SceneNavEdge(), inner = theme::SceneNavInner();
    for (const navigation::NavEdge& e : p.edges) {
        out.Add(DebugSpace::Space3D, {e.a.x, e.a.y + kLift, e.a.z}, {e.b.x, e.b.y + kLift, e.b.z}, e.border ? edge : inner, 0);
    }
}

void AreaLines(const game_production::Scene& scene, const Worlds& worlds, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    const NavSettings* settings = scene.Setting<NavSettings>();
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const NavArea* a = Find<NavArea>(*t.entity);
        if (!a || !Chosen(selection, all, t.entity->data.id)) continue;
        const NavAreaKind* kind = settings ? FindAreaKind(*settings, AreaIdOfCode(AreaCodeOf(a->area))) : nullptr;
        Color color = kind ? kind->color : theme::SceneArea();
        if (!t.activeInHierarchy) color = color.Alpha(0.35f);
        const auto w = worlds.find(t.entity->data.id);
        const space3d::Mat4 world = w != worlds.end() ? w->second : space3d::Mat4{};
        const Vec3 h{std::fabs(a->size.x) * 0.5f, std::fabs(a->size.y) * 0.5f, std::fabs(a->size.z) * 0.5f};
        auto line = [&](Vec3 p, Vec3 q) { out.Add(DebugSpace::Space3D, space3d::TransformPoint(world, p), space3d::TransformPoint(world, q), color, 0); };
        if (a->shape == NavAreaShape::Cylinder) {
            constexpr int kSides = 32;
            constexpr float kTurn = 2 * std::numbers::pi_v<float> / kSides;
            for (const float y : {-h.y, h.y}) {
                for (int i = 0; i < kSides; ++i) {
                    const float a0 = kTurn * static_cast<float>(i), a1 = kTurn * static_cast<float>(i + 1);
                    line({std::cos(a0) * h.x, y, std::sin(a0) * h.x}, {std::cos(a1) * h.x, y, std::sin(a1) * h.x});
                }
            }
            for (int i = 0; i < 4; ++i) {
                const float an = kTurn * static_cast<float>(i * kSides / 4);
                line({std::cos(an) * h.x, -h.y, std::sin(an) * h.x}, {std::cos(an) * h.x, h.y, std::sin(an) * h.x});
            }
            continue;
        }
        auto corner = [&](int i) { return Vec3{(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z}; };
        for (int i = 0; i < 8; ++i) {
            for (const int bit : {1, 2, 4}) {
                if (!(i & bit)) line(corner(i), corner(i | bit));
            }
        }
    }
}

void AgentLines(const game_production::Scene& scene, const Worlds& worlds, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    const NavSettings* settings = scene.Setting<NavSettings>();
    // (Play 中だけ、動いているコピーがナビの世界を持つ。読むだけ)
    const NavWorld* world = FindNavWorld(const_cast<game_production::Scene&>(scene));
    const Color ring = theme::SceneNavAgent(), desired = theme::SceneNavDesired(), route = theme::NavRoute();
    std::vector<Vec3> path;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const NavAgent* a = Find<NavAgent>(*t.entity);
        const int id = t.entity->data.id;
        if (!a || !Chosen(selection, all, id)) continue;
        const NavAgentType* type = settings ? FindAgentType(*settings, a->agentType) : nullptr;
        const float r = type ? std::max(type->radius, 0.05f) : 0.3f;
        const auto w = worlds.find(id);
        const Vec3 c = w != worlds.end() ? Vec3{w->second.m[3][0], w->second.m[3][1] + kLift, w->second.m[3][2]} : Vec3{};
        const Color col = t.activeInHierarchy ? ring : ring.Alpha(0.35f);
        constexpr int kSides = 24;
        constexpr float kTurn = 2 * std::numbers::pi_v<float> / kSides;
        for (int i = 0; i < kSides; ++i) {
            const float a0 = kTurn * static_cast<float>(i), a1 = kTurn * static_cast<float>(i + 1);
            out.Add(DebugSpace::Space3D, {c.x + std::cos(a0) * r, c.y, c.z + std::sin(a0) * r}, {c.x + std::cos(a1) * r, c.y, c.z + std::sin(a1) * r}, col, 0);
        }
        const NavWorld::Agent* ag = world ? world->FindAgent(id) : nullptr;
        if (!ag || ag->index < 0) continue;
        const NavWorld::Mesh& m = world->Meshes()[static_cast<size_t>(ag->mesh)];
        if (m.crowd && ag->hasTarget) {
            m.crowd->Path(ag->index, path);
            for (size_t i = 1; i < path.size(); ++i) {
                out.Add(DebugSpace::Space3D, {path[i - 1].x, path[i - 1].y + kLift * 2, path[i - 1].z}, {path[i].x, path[i].y + kLift * 2, path[i].z}, route, 0);
            }
        }
        const Vec3 v = ag->view.velocity, d = ag->view.desired;
        out.Add(DebugSpace::Space3D, c, {c.x + v.x * 0.5f, c.y, c.z + v.z * 0.5f}, ring, 0);
        out.Add(DebugSpace::Space3D, {c.x, c.y + 0.05f, c.z}, {c.x + d.x * 0.5f, c.y + 0.05f, c.z + d.z * 0.5f}, desired, 0);
    }
}

void NavSceneLines(editor::SoftwareHost& host, const game_production::Scene& scene, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    const Worlds worlds = WorldsOf(scene);
    for (const NavVolume& v : Volumes(scene, worlds)) {
        if (!Chosen(selection, all, v.entity)) continue;
        BoundsLines(v.bounds, v.active ? theme::SceneNavVolume() : theme::SceneNavVolume().Alpha(0.35f), out);
        if (const NavPreview* p = FindPreview(host, v.volume.baked)) MeshLines(*p, out);
    }
    AreaLines(scene, worlds, selection, all, out);
    AgentLines(scene, worlds, selection, all, out);
}

}  // namespace rm::game_x_navigation
