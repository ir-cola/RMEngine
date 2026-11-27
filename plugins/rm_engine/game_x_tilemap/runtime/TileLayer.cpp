#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"

namespace rm::game_x_tilemap {

Vec2 GridCellSize(const game_production::Scene& scene, int entity) {
    const std::string grid = reflect::TypeOf<TileGrid>().name;
    const game_production::SceneEntity* e = scene.Find(entity);
    for (int guard = 0; e && guard < 100000; ++guard) {
        if (const game_production::Component* c = e->FindComponent(grid); c && c->Known()) {
            return tilemap::UsableCellSize(static_cast<const TileGrid*>(c->value.Get())->cellSize);
        }
        e = e->data.parent != 0 ? scene.Find(e->data.parent) : nullptr;
    }
    return {1, 1};
}

const TileLayer* LayerOf(const game_production::Scene& scene, int entity) {
    const game_production::SceneEntity* e = scene.Find(entity);
    const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<TileLayer>().name) : nullptr;
    return c && c->Known() ? static_cast<const TileLayer*>(c->value.Get()) : nullptr;
}

}  // namespace rm::game_x_tilemap
