#pragma once
// ゲーム制作 × 3D - what other plugins draw into the game's 3D world themselves (パーティクル, 天候): the
// scene view, the game view (the editor's Play) and the exported game each make one drawer of every
// contribution and ask it once a frame what to draw into their picture (space3d::ExtraDraw: after the
// see-through parts, depth-tested).
//
//   // in a connector's runtime/Register.cpp:
//   static const game_x_3d::WorldDrawInfo kDraw{&CreateParticleDraw};
//   registry.Contribute(game_x_3d::kWorldDrawPoint, kDraw);
//
// A drawer keeps what it needs between frames (one per picture: the scene view's picture and its camera
// preview each have their own) and makes its GPU things on `gfx` (made again when gfx.Generation()
// changes).

#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "platform/Gfx.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rm::game_x_3d {

struct SkyLook;

// RuntimeRegistry point: items are WorldDrawInfo (static).
inline constexpr char kWorldDrawPoint[] = "rm_engine.game_x_3d.world_draw";

struct WorldDrawFrame {
    const game_production::Scene& scene;
    platform::Gfx& gfx;
    // Where an entity is in the world (its Transform with its parents'); null: it has none (or it is
    // switched off: nothing of it is drawn).
    std::function<const space3d::Mat4*(int entity)> world;
    float dt = 0;          // seconds since this picture's last frame (0: it did not move on - paused)
    bool playing = false;  // the game plays (the editor's Play, the exported game): everything runs
    // Not playing (the editor): what is selected - what a drawer may show moving to be looked at.
    std::span<const int> selection;
    // The bytes of an asset (the editor: its file; the game: what the export put in for it).
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    // An asset's name for messages (the editor: its file's name); null: its id is used.
    std::function<std::string(const AssetRef&)> name;
    // What the plugins offer (a drawer may ask other plugins for more: パーティクル the collision shapes).
    const RuntimeRegistry* registry = nullptr;
    // Which picture this is, for the debug panel: "scene" / "game" / "preview" (null: the exported game's).
    const char* picture = nullptr;
    // The game's time this picture moves by (the sky's: 0 in the editor until Play).
    double seconds = 0;
    // The scene's 空 as this picture shows it (its light: 天候's drops are lit by it); null: none.
    const SkyLook* sky = nullptr;
    // The sky and the weather are shown in this picture (the scene view's own camera: its 空 button).
    bool atmosphere = true;
    // The model an entity is drawn with in this picture (null: none - a mark, a box), and the renderer's
    // simple shapes (箱, 球 ...).
    std::function<const space3d::GpuMesh*(int entity)> model;
    std::function<const space3d::GpuMesh*(space3d::Shape)> shape;
};

// What a drawer adds to the frame itself (WorldDrawer::PrepareFrame): spans into the drawer's own (kept
// until its next PrepareFrame). Only the first drawer's 向きの地図 and surface touch are used.
struct FrameAdditions {
    std::span<const Vec3> mapDirections;
    std::span<const space3d::MapShape> mapShapes;
    const space3d::SurfaceTouch* surface = nullptr;
};

class WorldDrawer {
public:
    virtual ~WorldDrawer() = default;
    // Once a frame, before Prepare: what it adds to the frame (天候: the 向きの地図 of the colliders and
    // 濡れ on every surface; RenderFrame::mapDirections / mapShapes / surface).
    virtual void PrepareFrame(const WorldDrawFrame& f, FrameAdditions& out) {
        (void)f;
        (void)out;
    }
    // Once a frame, before the picture is drawn: what to draw into it (in `out`).
    virtual void Prepare(const WorldDrawFrame& f, std::vector<space3d::ExtraDraw>& out) = 0;
    // Something of it moves by itself: the editor draws the view again next frame.
    virtual bool Animating() const { return false; }
};

struct WorldDrawInfo {
    std::unique_ptr<WorldDrawer> (*create)() = nullptr;
};

}  // namespace rm::game_x_3d
