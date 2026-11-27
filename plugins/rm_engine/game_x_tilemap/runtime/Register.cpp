#include "rm_engine/game_x_tilemap/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_2d/runtime/World2D.h"
#include "rm_engine/game_x_tilemap/runtime/TileDraw.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"

namespace rm::game_x_tilemap {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    // タイルのグリッド (マス目) と、その子のタイルの層 (手で足すか、追加 ▸ 2D オブジェクト ▸ タイルマップ)。
    static const ComponentInfo kGrid{.type = &reflect::TypeOf<TileGrid>, .folder = "2D"};
    registry.Contribute(game_production::kComponentPoint, kGrid);
    static const ComponentInfo kLayer{.type = &reflect::TypeOf<TileLayer>, .folder = "2D"};
    registry.Contribute(game_production::kComponentPoint, kLayer);
    // 2D の絵に描く: シーンビュー、ゲームビュー、書き出したゲーム。
    static const game_x_2d::WorldDraw2DInfo kDraw{&CreateTileDraw};
    registry.Contribute(game_x_2d::kWorldDraw2DPoint, kDraw);
}

}  // namespace rm::game_x_tilemap

RM_RUNTIME_ENTRY(rm_engine, game_x_tilemap, rm::game_x_tilemap::RegisterRuntime)
