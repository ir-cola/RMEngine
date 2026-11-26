#include "rm_engine/game_x_navigation/editor/NavLines.h"

#include "editor/widgets/Theme.h"

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

void NavSceneLines(editor::SoftwareHost& host, const game_production::Scene& scene, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    const Worlds worlds = WorldsOf(scene);
    for (const NavVolume& v : Volumes(scene, worlds)) {
        if (!Chosen(selection, all, v.entity)) continue;
        BoundsLines(v.bounds, v.active ? theme::SceneNavVolume() : theme::SceneNavVolume().Alpha(0.35f), out);
        if (const NavPreview* p = FindPreview(host, v.volume.baked)) MeshLines(*p, out);
    }
    AreaLines(scene, worlds, selection, all, out);
}

}  // namespace rm::game_x_navigation
