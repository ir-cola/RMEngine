#include "rm_engine/physics/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/physics/runtime/Shapes.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <vector>

namespace rm::physics {

namespace {

namespace theme = editor::theme;
using game_production::DebugDraw;
using game_production::DebugSpace;
using game_production::Scene;
using game_production::SceneEntity;

template <class T>
const T* Find(const SceneEntity& e) {
    const game_production::Component* c = e.FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
}

// The shapes the physics makes (Shapes.h: the same sizes and places as the bodies), as lines: of the
// selected entities, or of all. Switched off (it or a parent): faint - it has no body while so.
void ShapeLines(const Scene& scene, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    std::unordered_map<int, EntityPlace> places;
    std::vector<Segment> segments;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const SceneEntity& e = *t.entity;
        const auto parent = places.find(e.data.parent);
        EntityPlace p;
        const bool placed = PlaceUnder(e, parent != places.end() ? &parent->second : nullptr, p);
        places[e.data.id] = p;
        if (!placed) continue;
        if (!all && std::find(selection.begin(), selection.end(), e.data.id) == selection.end()) continue;
        const DebugSpace space = p.flat ? DebugSpace::Space2D : DebugSpace::Space3D;
        auto draw = [&](bool trigger) {
            const Color color = !t.activeInHierarchy ? theme::SceneColliderInactive() : trigger ? theme::SceneTrigger() : theme::SceneCollider();
            for (const Segment& s : segments) out.Add(space, s.a, s.b, color, 0);
            segments.clear();
        };
        if (p.flat) {
            if (const Collider2D* c = Find<Collider2D>(e)) {
                const Pose pose = PoseOf(p.world2);
                Outline(DimsOf(*c, pose.scale), pose, true, segments);
                draw(c->trigger);
            }
            continue;
        }
        const Pose pose = PoseOf(p.world3);
        if (const Collider* c = Find<Collider>(e)) {
            Outline(DimsOf(*c, pose.scale), pose, false, segments);
            draw(c->trigger);
        }
        if (const Character* c = Find<Character>(e)) {
            Pose upright;  // a character stands upright whatever its turn (and is not scaled)
            upright.position = pose.position;
            Outline(DimsOf(*c), upright, false, segments);
            draw(false);
        }
    }
}

// デバッグ ▸ 物理: the bodies and what touches, while playing.
void PhysicsLines(const game_production::DebugContext& ctx, std::vector<game_production::DebugInfoLine>& out) {
    const PhysicsWorld* w = ctx.playing && ctx.scene ? FindWorldOf(*ctx.scene) : nullptr;
    if (!w) {
        out.push_back({L"", ctx.playing ? editor::Tr(L"(このシーンに物理で動く物はありません)") : editor::Tr(L"Play 中に出ます")});
        return;
    }
    const size_t bodies = w->Bodies(), active = w->ActiveBodies();
    out.push_back({editor::Tr(L"剛体"), editor::TrF(L"{} 個 (動いている {} ・ 眠っている {})", bodies, active, bodies > active ? bodies - active : 0)});
    out.push_back({editor::Tr(L"キャラクター"), editor::TrF(L"{} 個", w->Characters())});
    out.push_back({editor::Tr(L"触れている組"), editor::TrF(L"{} 組", w->TouchingPairs())});
    out.push_back({editor::Tr(L"1 回の刻みの時間"), std::format(L"{:.3f} ms", w->LastStepMs())});
}

// A 3D object made by 追加 ▸ 3D オブジェクト gets a collider of its shape (2026-11-20): a box, a ball, a
// capsule as they are; a cylinder as a box around it (there is no cylinder collider); the plane a thin
// box under it. What has no shape to say (a light, a camera): nothing.
void AddCollider(Scene& scene, int entity, std::string_view made) {
    const bool box = made == "box" || made == "cylinder";
    if (!box && made != "sphere" && made != "capsule" && made != "plane") return;
    Collider* c = game_production::AddComponentTo<Collider>(scene, entity);
    if (!c) return;
    if (box) {
        c->shape = Shape::Box;
        c->size = {1, 1, 1};
    } else if (made == "sphere") {
        c->shape = Shape::Sphere;
        c->radius = 0.5f;
    } else if (made == "capsule") {
        c->shape = Shape::Capsule;
        c->radius = 0.5f;
        c->height = 2.f;
    } else {
        c->shape = Shape::Box;
        c->size = {10, 0.02f, 10};
        c->offset = {0, -0.01f, 0};
    }
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::CreatedInfo kCreated{&AddCollider};
    registry.Contribute(game_production::kCreatedPoint, kCreated);
    static const game_production::SceneLinesInfo kLines{&ShapeLines};
    registry.Contribute(game_production::kSceneLinesPoint, kLines);
    static const game_production::DebugInfoInfo kDebug{editor::TrKey(L"物理"), 20, game_production::DebugTab::Game, &PhysicsLines};
    registry.Contribute(game_production::kDebugInfoPoint, kDebug);
}

}  // namespace rm::physics
