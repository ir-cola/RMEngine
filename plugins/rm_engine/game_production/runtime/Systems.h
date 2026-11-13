#pragma once
// Systems: what moves the game every frame (the S of ECS). Game production does not know what any
// system does - plugins offer them, like components (Components.h):
//
//   // in a plugin's runtime part (runtime/Register.cpp - core/RuntimeRegistry.h), so the exported game
//   // reads the same list as the editor's Play:
//   static const game_production::SystemInfo kSpin{"回り続ける", game_production::SystemPhase::Frame, 0, &RunSpin};
//   registry.Contribute(game_production::kSystemPoint, kSpin);
//
// Two kinds:
//   Fixed  runs in steps of the game settings' 物理の刻み (fixedTimestep): 0, 1 or more times a
//          frame, so it runs the same however fast the PC is (physics, game rules).
//   Frame  runs once a frame with the time since the last one (looks: turning, animation).
// Each frame: every Fixed step first, then the Frame systems. Inside a kind: `order` (smaller
// first), then the order they were offered.
//
// Pure (the runtime: no editor, no drawing, no files) - tested in tests/TestSystems.cpp.

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

namespace rm::game_production {

// PluginRegistry point: items are SystemInfo (static).
inline constexpr char kSystemPoint[] = "rm_engine.game_production.system";

enum class SystemPhase { Fixed, Frame };

// The content of assets, for systems that need it (a mesh collider's model): the host's way to it.
struct AssetAccess {
    // The bytes as the game has them (an exported game: from its .rmpak - baked); nullopt: none.
    std::function<std::optional<std::string>(const AssetRef&)> bytes;
    // The file on disk (the editor: the project's Assets/...); empty: none.
    std::function<std::filesystem::path(const AssetRef&)> file;
};

// A script hook a system asks for (a physics contact ...): called on every started script of `entity`
// after the fixed steps of the frame, in the scripts' order (ScriptRunner::Deliver), with `other`.
// The hook's number is RmAbi.h's RM_HOOK_* (game production does not know what the systems are).
struct HookCall {
    int entity = 0;
    int hook = 0;
    int other = 0;
};

// What a system gets each time it runs.
struct SystemContext {
    Scene& scene;
    float dt = 0;          // seconds: the fixed step (Fixed) or the time since the last frame (Frame)
    double time = 0;       // seconds of game time before this run
    uint64_t frame = 0;    // frames run before this one (the first frame is 0)
    // The entity the game sees through now (its id; 0: none). It starts as the game settings'
    // 最初のカメラ when Play starts; a system switches the game's camera by setting it to another
    // entity (what makes an entity a camera is the space's business: a 3D Camera component ...).
    uint64_t camera = 0;
    // The game settings' 重力 (m/s², the world's axes; a 2D game uses its x and y).
    Vec3 gravity{0, -9.81f, 0};
    // Where hooks for the scripts go (null: nobody delivers them - nothing to ask).
    std::vector<HookCall>* hooks = nullptr;
    // The host's assets (null: none).
    const AssetAccess* assets = nullptr;
};

struct SystemInfo {
    const char* name = "";  // for the console (Japanese, as logs are)
    SystemPhase phase = SystemPhase::Frame;
    int order = 0;
    void (*run)(SystemContext& ctx) = nullptr;
};

// Every component of `type` of the entities that run (they and every parent are 有効), in the 階層's
// order; an entity with several of that type gives each, in its order. Components whose plugin is
// missing are not given.
void ForEachComponent(Scene& scene, std::string_view type, const std::function<void(SceneEntity&, Component&)>& fn);

// The systems the plugins offered (their runtime parts), in the order they were offered - what a game
// runs (SystemRunner::SetSystems sorts them).
std::vector<const SystemInfo*> RegisteredSystems(const RuntimeRegistry& registry);

// The component types the game sees through (ComponentInfo::camera), each once.
std::vector<std::string> CameraTypes(const RuntimeRegistry& registry);
// The camera of a scene that starts playing (Play, the game, a scene switched to): `wanted` (the game
// settings' 最初のカメラ) when it is an entity of this scene with a camera component, else the first
// running entity (in the 階層's order) that has one; 0 when there is none.
uint64_t SceneCamera(const Scene& scene, const std::vector<std::string>& cameraTypes, uint64_t wanted);

// Runs the systems over a scene (the copy that plays). Keeps the game's clock and what is left of
// the fixed step.
class SystemRunner {
public:
    // At most this many Fixed steps in one frame: a frame that took very long (a breakpoint, the
    // window dragged) does not make the game run hundreds of steps to catch up.
    static constexpr int kMaxFixedSteps = 8;

    void SetSystems(std::span<const SystemInfo* const> systems);
    const std::vector<const SystemInfo*>& Systems() const { return systems_; }

    // Back to the start (Play pressed again).
    void Reset();
    // One frame of `dt` seconds (negative: 0). `fixedStep` <= 0: no Fixed steps. Returns how many
    // Fixed steps ran.
    int Advance(Scene& scene, double dt, float fixedStep);

    double Time() const { return time_; }
    uint64_t Frames() const { return frames_; }
    // The camera the game sees through (SystemContext::camera): set when Play starts, changed by the
    // systems. Reset makes it 0.
    void SetCamera(uint64_t entity) { camera_ = entity; }
    uint64_t Camera() const { return camera_; }
    // The game settings' 重力 (SystemContext::gravity).
    void SetGravity(Vec3 g) { gravity_ = g; }
    // How the systems read assets (SystemContext::assets).
    void SetAssets(AssetAccess a) { assets_ = std::move(a); }
    // The hooks the systems asked for in the last Advance (for ScriptRunner::Deliver).
    const std::vector<HookCall>& Hooks() const { return hooks_; }

private:
    std::vector<const SystemInfo*> systems_;  // Fixed first, then Frame; by order inside each
    double time_ = 0;
    double fixedTime_ = 0;  // game time the Fixed steps have reached
    double carry_ = 0;      // time not yet covered by a Fixed step
    uint64_t frames_ = 0;
    uint64_t camera_ = 0;
    Vec3 gravity_{0, -9.81f, 0};
    std::vector<HookCall> hooks_;
    AssetAccess assets_;
};

}  // namespace rm::game_production
