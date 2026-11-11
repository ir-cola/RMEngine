#pragma once
// Systems: what moves the game every frame (the S of ECS). Game production does not know what any
// system does - plugins offer them, like components (Components.h):
//
//   // in a plugin's RegisterEditor (the exported game will read the same list):
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

#include "rm_engine/game_production/runtime/Scene.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace rm::game_production {

// PluginRegistry point: items are SystemInfo (static).
inline constexpr char kSystemPoint[] = "rm_engine.game_production.system";

enum class SystemPhase { Fixed, Frame };

// What a system gets each time it runs.
struct SystemContext {
    Scene& scene;
    float dt = 0;          // seconds: the fixed step (Fixed) or the time since the last frame (Frame)
    double time = 0;       // seconds of game time before this run
    uint64_t frame = 0;    // frames run before this one (the first frame is 0)
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

private:
    std::vector<const SystemInfo*> systems_;  // Fixed first, then Frame; by order inside each
    double time_ = 0;
    double fixedTime_ = 0;  // game time the Fixed steps have reached
    double carry_ = 0;      // time not yet covered by a Fixed step
    uint64_t frames_ = 0;
};

}  // namespace rm::game_production
