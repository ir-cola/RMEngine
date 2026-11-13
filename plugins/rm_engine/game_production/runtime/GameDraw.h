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
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <d2d1_1.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rm::game_production {

// What one frame of the game is drawn from.
struct GameDrawFrame {
    const Scene& scene;
    uint64_t camera = 0;         // the entity the game sees through (SystemContext::camera)
    Color background{0, 0, 0, 1};  // the game settings' 背景の色
    platform::Gfx& gfx;
    ID2D1DeviceContext* dc;      // the window's (its whole client area)
    float width = 0, height = 0;  // DIPs
    // The bytes of an asset (the game: from its .rmpak - what the export put in for it, baked or as
    // its file); nullopt when it is not there.
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    // A file of the game's own by its path (a baked model's pictures: "Baked/<id>/0.dds").
    std::function<std::optional<std::string>(std::string_view path)> file;
};

class GameDrawer {
public:
    virtual ~GameDrawer() = default;
    virtual void Draw(const GameDrawFrame& f) = 0;
    // Over another space's picture (a 2D layer over a 3D game: カメラ (2D)「3D の上に重ねる」). Drawn
    // after the game's own drawer, by the drawers of the other spaces; nothing by default.
    virtual void DrawOverlay(const GameDrawFrame& f) { (void)f; }
};

// Over the whole game screen, after every space's picture (the game UI: rm_engine/game_ui). Not of a
// space: drawn in a game of any. Both hosts draw it: the exported game over its window, the editor's
// game view over its picture of the game (stopped too) - into `area`, the game screen being `screen`
// (the game settings' 画面 in the editor: what is drawn is scaled to the area).
struct ScreenDrawFrame {
    const Scene& scene;
    const GameSettings& settings;
    platform::Gfx& gfx;
    ID2D1DeviceContext* dc;
    float x = 0, y = 0, width = 0, height = 0;  // where on the dc (DIPs)
    Vec2 screen{1280, 720};                    // the game's screen it stands for (DIPs)
    std::function<std::optional<std::string>(const AssetRef&)> asset;  // as GameDrawFrame::asset
    bool playing = false;  // the game runs (the editor: Play) - else the scene as it is being made
};

class ScreenDrawer {
public:
    virtual ~ScreenDrawer() = default;
    virtual void Draw(const ScreenDrawFrame& f) = 0;
};

// RuntimeRegistry point: items are ScreenDrawInfo (static). Drawn in `order` (the smaller first).
inline constexpr char kScreenDrawPoint[] = "rm_engine.game_production.screen_draw";
struct ScreenDrawInfo {
    int order = 0;
    std::unique_ptr<ScreenDrawer> (*create)() = nullptr;
};

// RuntimeRegistry point: items are GameDrawInfo (static).
inline constexpr char kGameDrawPoint[] = "rm_engine.game_production.game_draw";
struct GameDrawInfo {
    const char* space = "";  // as SceneViewInfo::space ("3d", "2d")
    int order = 0;
    std::unique_ptr<GameDrawer> (*create)() = nullptr;
};

}  // namespace rm::game_production
