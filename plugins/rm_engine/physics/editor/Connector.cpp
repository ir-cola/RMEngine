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

// 物理が作る形 (Shapes.h: 物体と同じ大きさ、同じ位置) を線で描く: 選んだエンティティのもの、または
// すべて。切ってある (それ自身か親が) もの: 薄く - 切ってある間は物体が無いため。
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
            Pose upright;  // キャラクターは回転に関係なく立っている (拡縮もしない)
            upright.position = pose.position;
            Outline(DimsOf(*c), upright, false, segments);
            draw(false);
        }
    }
}

// デバッグ ▸ 物理: 再生中の物体と、触れているもの。
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

// 追加 ▸ 3D オブジェクト で作った 3D オブジェクトは、その形のコライダーを持つ (2026-11-20): 箱、球、
// カプセル、円柱 (2026-11-24 から。前は囲む箱で、雪が四角く積もった) はそのまま; 平面はその下の薄い箱。
// 形を言えないもの (ライト、カメラ): 何も付けない。
void AddCollider(Scene& scene, int entity, std::string_view made) {
    const bool box = made == "box";
    if (!box && made != "sphere" && made != "capsule" && made != "cylinder" && made != "plane") return;
    Collider* c = game_production::AddComponentTo<Collider>(scene, entity);
    if (!c) return;
    if (box) {
        c->shape = Shape::Box;
        c->size = {1, 1, 1};
    } else if (made == "sphere") {
        c->shape = Shape::Sphere;
        c->radius = 0.5f;
    } else if (made == "cylinder") {
        c->shape = Shape::Cylinder;
        c->radius = 0.5f;
        c->height = 1.f;
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
