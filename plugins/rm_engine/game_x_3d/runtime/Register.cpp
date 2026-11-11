#include "rm_engine/game_x_3d/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/CameraCycle.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"

namespace rm::game_x_3d {

void RegisterRuntime(RuntimeRegistry& registry) {
    // 回り続ける: turns its entity while the game plays (several add up), and the system that does it.
    static const game_production::ComponentInfo kSpin{.type = &reflect::TypeOf<Spin>, .multiple = true};
    registry.Contribute(game_production::kComponentPoint, kSpin);
    static const game_production::SystemInfo kSpinSystem{"回り続ける", game_production::SystemPhase::Frame, 0, &RunSpin};
    registry.Contribute(game_production::kSystemPoint, kSpinSystem);
    // カメラを順に切り替える: a system switching the game's camera while it plays (SystemContext::camera).
    static const game_production::ComponentInfo kCycle{.type = &reflect::TypeOf<CameraCycle>};
    registry.Contribute(game_production::kComponentPoint, kCycle);
    static const game_production::SystemInfo kCycleSystem{"カメラを順に切り替える", game_production::SystemPhase::Frame, 0, &RunCameraCycle};
    registry.Contribute(game_production::kSystemPoint, kCycleSystem);
}

}  // namespace rm::game_x_3d
