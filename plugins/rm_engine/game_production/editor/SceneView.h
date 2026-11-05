#pragma once
// The scene view's frame. Game production shows the panel; what is drawn in it - and how the
// camera moves - comes from a plugin that knows a space (the connector ゲーム制作 × 3D draws it in
// 3D). Game production itself never knows 3D or 2D (design RME_PLATFORM 4-f).
//
//   // in a connector's RegisterEditor:
//   static const game_production::SceneViewInfo kView{&CreateMySceneView};
//   registry.Contribute(game_production::kSceneViewPoint, kView);
//
// The first scene view offered is used. Without one, the panel says which plugins bring it.

#include "editor/plugin/Software.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace rm::game_production {

// PluginRegistry point: items are SceneViewInfo (static).
inline constexpr char kSceneViewPoint[] = "rm_engine.game_production.scene_view";

// What a scene view gets each time it is drawn.
struct SceneViewHost {
    editor::SoftwareHost& host;                     // the GPU, the history ...
    const Scene& scene;
    std::span<const int> selection;                 // the selected entities (the 階層's)
    std::function<void(std::vector<int>)> select;   // select these instead (empty: nothing)
};

// One per open game: keeps its own camera.
class SceneView {
public:
    virtual ~SceneView() = default;
    virtual void Draw(ui::Context& c, const ui::Rect& r, SceneViewHost& host) = 0;
};

struct SceneViewInfo {
    std::unique_ptr<SceneView> (*create)();
};

}  // namespace rm::game_production
