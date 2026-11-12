#pragma once
// How an exported game is drawn: by the plugin that knows the game's space (3D: ゲーム制作 × 3D, 2D:
// ゲーム制作 × 2D - game production never knows a space). Each offers a drawer from its RUNTIME part;
// the game takes the one of the game settings' space (GameSettings::space), or the one with the
// smallest order when that is not set or not there.
//
//   // in a connector's runtime/Register.cpp:
//   static const game_production::GameDrawInfo kDraw{"3d", 0, &CreateGameDraw3D};
//   registry.Contribute(game_production::kGameDrawPoint, kDraw);

#include "base/Math.h"
#include "base/Ref.h"
#include "platform/Gfx.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <d2d1_1.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace rm::game_production {

// What one frame of the game is drawn from.
struct GameDrawFrame {
    const Scene& scene;
    uint64_t camera = 0;         // the entity the game sees through (SystemContext::camera)
    Color background{0, 0, 0, 1};  // the game settings' 背景の色
    platform::Gfx& gfx;
    ID2D1DeviceContext* dc;      // the window's (its whole client area)
    float width = 0, height = 0;  // DIPs
    // The bytes of an asset (the game: from its .rmpak); nullopt when it is not there.
    std::function<std::optional<std::string>(const AssetRef&)> asset;
};

class GameDrawer {
public:
    virtual ~GameDrawer() = default;
    virtual void Draw(const GameDrawFrame& f) = 0;
};

// RuntimeRegistry point: items are GameDrawInfo (static).
inline constexpr char kGameDrawPoint[] = "rm_engine.game_production.game_draw";
struct GameDrawInfo {
    const char* space = "";  // as SceneViewInfo::space ("3d", "2d")
    int order = 0;
    std::unique_ptr<GameDrawer> (*create)() = nullptr;
};

}  // namespace rm::game_production
