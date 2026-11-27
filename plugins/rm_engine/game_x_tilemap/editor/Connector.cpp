#include "rm_engine/game_x_tilemap/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_2d/editor/SceneTool2D.h"
#include "rm_engine/game_x_tilemap/editor/TilePanel.h"
#include "rm_engine/game_x_tilemap/editor/TileTool.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::game_x_tilemap {

namespace {

using game_production::AddComponentTo;
using game_production::Scene;

// 追加 ▸ 2D オブジェクト ▸ タイルマップ: グリッドのエンティティ「タイルマップ」と、その子の層 1 つ「地面」
// (どちらも Transform 2D つき。名前は使っている言語で)。Undo の 1 件 (ゲーム制作が積む)。
int MakeTilemap(Scene& scene, int parent, size_t index) {
    const int grid = scene.Create(editor::TrU8("タイルマップ"), parent, index);
    AddComponentTo<space2d::Transform2D>(scene, grid);
    AddComponentTo<TileGrid>(scene, grid);
    const int ground = scene.Create(editor::TrU8("地面"), grid);
    AddComponentTo<space2d::Transform2D>(scene, ground);
    AddComponentTo<TileLayer>(scene, ground);
    return grid;
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // (部品の型と描き手はランタイム側のもの: runtime/Register.cpp。ここはエディターがどう見せ、どう編集するか。)
    using game_production::ComponentLook;
    static const ComponentLook kGrid{.type = &reflect::TypeOf<TileGrid>, .icon = "square"};
    registry.Contribute(game_production::kComponentLookPoint, kGrid);
    static const ComponentLook kLayer{.type = &reflect::TypeOf<TileLayer>, .icon = "image"};
    registry.Contribute(game_production::kComponentLookPoint, kLayer);
    static const game_production::CreateItemInfo kItem{.label = editor::TrKey(L"タイルマップ"), .folder = editor::TrKey(L"2D オブジェクト"), .make = &MakeTilemap};
    registry.Contribute(game_production::kCreateItemPoint, kItem);
    // パネル「タイル」: パレットとタイルセットの編集。
    static const game_production::ViewPanelInfo kPanel{"tiles", editor::TrKey(L"タイル"), &CreateTilePanel};
    registry.Contribute(game_production::kViewPanelPoint, kPanel);
    // 2D のシーンビューの塗る道具 (タイルの層を選んでいるときだけ出る)。
    static const game_x_2d::SceneTool2DInfo kTool{&CreateTileTool};
    registry.Contribute(game_x_2d::kSceneTool2DPoint, kTool);
}

}  // namespace rm::game_x_tilemap
