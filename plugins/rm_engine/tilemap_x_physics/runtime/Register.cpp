#include "rm_engine/tilemap_x_physics/runtime/Register.h"

#include "platform/Shell.h"
#include "rm_engine/physics/runtime/StaticShapes.h"
#include "rm_engine/tilemap_x_physics/runtime/TileColliders.h"

namespace rm::tilemap_x_physics {

namespace {

// 物理のワールド (Play のコピー) ごとに 1 つ。タイルセットはワールドの中で 1 回だけ読む (Play の間は変わらない)。
class TileShapes final : public physics::StaticShapeSource {
public:
    void Collect(const game_production::Scene& scene, const game_production::AssetAccess* assets, std::vector<physics::StaticOwner>& out) override {
        // 書き出したゲーム: .rmpak のバイト。エディターの Play: プロジェクトのファイル。
        const AssetBytes bytes = [assets](const AssetRef& ref) -> std::optional<std::string> {
            if (!assets) return std::nullopt;
            if (assets->bytes) {
                if (auto b = assets->bytes(ref)) return b;
            }
            if (assets->file) {
                const std::filesystem::path file = assets->file(ref);
                std::string err;
                if (!file.empty()) return platform::ReadFileBytes(file, &err);
            }
            return std::nullopt;
        };
        std::vector<int> alive;
        for (const game_production::TreeEntry& t : scene.TreeOrder()) {
            if (!t.activeInHierarchy) continue;  // (有効でない層は当たらない)
            const int id = t.entity->data.id;
            const game_x_tilemap::TileLayer* layer = game_x_tilemap::LayerOf(scene, id);
            if (!layer || !layer->collision) continue;
            alive.push_back(id);
            physics::StaticOwner owner;
            owner.entity = id;
            owner.pieces = colliders_.Pieces(scene, id, *layer, bytes);
            if (!owner.pieces.empty()) out.push_back(std::move(owner));
        }
        colliders_.Forget(alive);
    }

private:
    TileColliders colliders_{false};
};

std::unique_ptr<physics::StaticShapeSource> CreateTileShapes() { return std::make_unique<TileShapes>(); }

}  // namespace

void RegisterRuntime(RuntimeRegistry& registry) {
    static const physics::StaticShapesInfo kShapes{&CreateTileShapes};
    registry.Contribute(physics::kStaticShapesPoint, kShapes);
}

}  // namespace rm::tilemap_x_physics

RM_RUNTIME_ENTRY(rm_engine, tilemap_x_physics, rm::tilemap_x_physics::RegisterRuntime)
