#include "rm_engine/tilemap_x_physics/editor/Register.h"

#include "editor/widgets/Theme.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/physics/runtime/Shapes.h"
#include "rm_engine/tilemap_x_physics/runtime/TileColliders.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace rm::tilemap_x_physics {

namespace {

namespace theme = editor::theme;
using game_production::DebugDraw;
using game_production::DebugSpace;

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// 当たりの線: 選んだ層 (「形: 全部」なら全部の層) の、物理に渡すのと同じまとめた形。有効でない層・「当たりを作る」を
// 外した層は薄く (物理では当たらない)。
void TileLines(editor::SoftwareHost& host, const game_production::Scene& scene, std::span<const int> selection, bool all, DebugDraw& out) {
    if (!all && selection.empty()) return;
    static TileColliders colliders{true};  // (エディターのビューすべてで共有。中身は区画の文字で決まる)
    const AssetBytes bytes = [&host](const AssetRef& ref) { return host.AssetContent(ref); };
    std::unordered_map<int, physics::EntityPlace> places;
    std::vector<int> alive;
    const double now = Now();
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const auto& e = *t.entity;
        const auto parent = places.find(e.data.parent);
        physics::EntityPlace p;
        const bool placed = physics::PlaceUnder(e, parent != places.end() ? &parent->second : nullptr, p);
        places[e.data.id] = p;
        if (!placed || !p.flat) continue;
        const game_x_tilemap::TileLayer* layer = game_x_tilemap::LayerOf(scene, e.data.id);
        if (!layer) continue;
        if (!all && std::find(selection.begin(), selection.end(), e.data.id) == selection.end()) continue;
        alive.push_back(e.data.id);
        const Color color = t.activeInHierarchy && layer->collision ? theme::SceneCollider() : theme::SceneColliderInactive();
        auto line = [&](Vec2 a, Vec2 b) {
            const Vec2 wa = space2d::TransformPoint(p.world2, a), wb = space2d::TransformPoint(p.world2, b);
            out.Add(DebugSpace::Space2D, {wa.x, wa.y, 0}, {wb.x, wb.y, 0}, color, 0);
        };
        for (const physics::StaticPiece& piece : colliders.Pieces(scene, e.data.id, *layer, bytes, now)) {
            for (const physics::StaticBox2D& b : piece.geometry->boxes2D) {
                line(b.min, {b.max.x, b.min.y});
                line({b.max.x, b.min.y}, b.max);
                line(b.max, {b.min.x, b.max.y});
                line({b.min.x, b.max.y}, b.min);
            }
            for (const auto& poly : piece.geometry->polygons2D) {
                for (size_t i = 0; i < poly.size(); ++i) line(poly[i], poly[(i + 1) % poly.size()]);
            }
        }
    }
    if (all) colliders.Forget(alive);  // (全部を見たときだけ: 選んだ物だけのときは、ほかの層の覚えた物を残す)
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::SceneLinesInfo kLines{.lines = nullptr, .hostLines = &TileLines};
    registry.Contribute(game_production::kSceneLinesPoint, kLines);
}

}  // namespace rm::tilemap_x_physics
