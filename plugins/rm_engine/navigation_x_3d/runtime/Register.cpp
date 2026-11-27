#include "rm_engine/navigation_x_3d/runtime/Register.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/navigation_x_3d/runtime/NavFill.h"
#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Primitives.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>

namespace rm::navigation_x_3d {

namespace {

using game_production::Component;
using game_production::SceneEntity;
using game_x_3d::MeshRenderer;
using game_x_navigation::NavShape;

const MeshRenderer* MeshOf(const SceneEntity& e) {
    const Component* c = e.FindComponent(reflect::TypeOf<MeshRenderer>().name);
    return c && c->Known() ? static_cast<const MeshRenderer*>(c->value.Get()) : nullptr;
}

// 形の点と三角形 (モデル自身の空間)。
struct Local {
    std::vector<Vec3> points;
    std::vector<uint32_t> indices;
    Vec3 min, max;
};

std::optional<Local> LocalOf(const space3d::MeshData& m) {
    if (m.indices.size() < 3) return std::nullopt;
    Local l;
    l.points.reserve(m.vertices.size());
    for (const space3d::MeshVertex& v : m.vertices) l.points.push_back(v.position);
    l.indices = m.indices;
    l.min = m.min, l.max = m.max;
    return l;
}

// 箱の 8 つの角をワールドへ置いて、範囲に触れるか (読む前に、遠い物を飛ばす)。
bool Near(const Local& l, const space3d::Mat4& world, const navigation::NavBounds& b) {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const float x : {l.min.x, l.max.x}) {
        for (const float y : {l.min.y, l.max.y}) {
            for (const float z : {l.min.z, l.max.z}) {
                const Vec3 p = space3d::TransformPoint(world, {x, y, z});
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            }
        }
    }
    return hi.x >= b.min.x && lo.x <= b.max.x && hi.y >= b.min.y && lo.y <= b.max.y && hi.z >= b.min.z && lo.z <= b.max.z;
}

void MeshPrint(const game_production::Scene& scene, const game_x_navigation::Worlds& worlds, const game_production::AssetAccess* assets,
               navigation::Hasher& h) {
    const std::string type = reflect::TypeOf<MeshRenderer>().name;
    for (const SceneEntity& e : scene.Entities()) {
        const Component* c = e.FindComponent(type);
        if (!c || !c->Known()) continue;
        game_x_navigation::HashComponent(h, e, *c, worlds);
        const auto* m = static_cast<const MeshRenderer*>(c->value.Get());
        if (m->shape != game_x_3d::MeshShape::Model || m->model.Empty() || !assets || !assets->file) continue;
        // モデルのファイルが変わった (上書きした) ときも古い。
        const std::filesystem::path file = assets->file(m->model);
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(file, ec);
        h.Int(ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count()));
        const auto size = std::filesystem::file_size(file, ec);
        h.Int(ec ? 0 : static_cast<int64_t>(size));
    }
}

}  // namespace

void CollectMeshes(const game_x_navigation::NavSourceContext& ctx, std::vector<NavShape>& out) {
    std::map<std::string, std::optional<Local>> models;  // モデルの id ごと (1 回だけ読む)
    std::map<int, std::optional<Local>> shapes;           // 形ごと
    for (const SceneEntity& e : ctx.scene.Entities()) {
        if (ctx.stop.stop_requested()) return;
        const MeshRenderer* m = MeshOf(e);
        if (!m) continue;
        const std::optional<space3d::Shape> primitive = game_x_3d::ShapeOf(m->shape);
        const std::optional<Local>* local = nullptr;
        if (primitive) {
            auto [it, fresh] = shapes.try_emplace(static_cast<int>(*primitive));
            if (fresh) it->second = LocalOf(space3d::ShapeMesh(*primitive));
            local = &it->second;
        } else {
            if (m->model.Empty()) continue;
            auto [it, fresh] = models.try_emplace(m->model.guid);
            if (fresh) {
                std::string err;
                std::optional<space3d::MeshData> mesh;
                if (ctx.assets && ctx.assets->file) {
                    if (const auto file = ctx.assets->file(m->model); !file.empty()) mesh = space3d::LoadMeshFile(file, &err);
                }
                if (!mesh && ctx.assets && ctx.assets->bytes) {
                    if (const auto bytes = ctx.assets->bytes(m->model)) mesh = space3d::ReadBakedMesh(*bytes, &err);
                }
                if (mesh) {
                    it->second = LocalOf(*mesh);
                } else {
                    log::Warn("ナビ: メッシュのモデルを読めません ({}): {}", e.data.name, err.empty() ? std::string("モデルがありません") : err);
                }
            }
            local = &it->second;
        }
        if (!local || !*local) continue;
        const auto w = ctx.worlds.find(e.data.id);
        const space3d::Mat4 world = w != ctx.worlds.end() ? w->second : space3d::Mat4{};
        if (!Near(**local, world, ctx.bounds)) continue;
        NavShape s;
        s.entity = e.data.id;
        game_x_navigation::AddMeshTo(s, (*local)->points, (*local)->indices, world);
        out.push_back(std::move(s));
    }
}

void RegisterRuntime(RuntimeRegistry& registry) {
    // 焼く物「メッシュ」: 部品「ナビメッシュ」の「メッシュを焼く」で切り替える (欄の名前 meshes)。
    static const game_x_navigation::NavSourceInfo kMeshes{"meshes", "メッシュ", "3d", &CollectMeshes, &MeshPrint};
    registry.Contribute(game_x_navigation::kNavSourcePoint, kMeshes);
    // シーンビューに焼いたナビメッシュの面を塗る (NavFill.h)。
    static const game_x_3d::WorldDrawInfo kFill{&CreateNavFill};
    registry.Contribute(game_x_3d::kWorldDrawPoint, kFill);
}

}  // namespace rm::navigation_x_3d

RM_RUNTIME_ENTRY(rm_engine, navigation_x_3d, rm::navigation_x_3d::RegisterRuntime)
