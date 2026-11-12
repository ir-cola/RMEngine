#include "rm_engine/game_x_3d/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/CameraCycle.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/ScriptDebug3D.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // A 3D game's entities have a place in 3D space: every new entity gets one while the game's space is
    // 3D. It is where a placed prefab is (its values stay the placed one's).
    static const ComponentInfo kTransform{.type = &reflect::TypeOf<space3d::Transform>, .onNewEntities = true, .placement = true, .space = "3d"};
    registry.Contribute(kComponentPoint, kTransform);
    // ... and may be drawn as a 3D model (added by hand: a new entity is a box until it has one).
    static const ComponentInfo kMesh{.type = &reflect::TypeOf<MeshRenderer>};
    registry.Contribute(kComponentPoint, kMesh);
    // カメラ (what the game sees: the game settings' 最初のカメラ), 光 and 環境光.
    static const ComponentInfo kCamera{.type = &reflect::TypeOf<Camera>};
    registry.Contribute(kComponentPoint, kCamera);
    static const ComponentInfo kLight{.type = &reflect::TypeOf<Light>};
    registry.Contribute(kComponentPoint, kLight);
    static const ComponentInfo kAmbient{.type = &reflect::TypeOf<AmbientLight>};
    registry.Contribute(kComponentPoint, kAmbient);
    // 回り続ける: turns its entity while the game plays (several add up), and the system that does it.
    static const ComponentInfo kSpin{.type = &reflect::TypeOf<Spin>, .multiple = true};
    registry.Contribute(kComponentPoint, kSpin);
    static const game_production::SystemInfo kSpinSystem{"回り続ける", game_production::SystemPhase::Frame, 0, &RunSpin};
    registry.Contribute(game_production::kSystemPoint, kSpinSystem);
    // カメラを順に切り替える: a system switching the game's camera while it plays (SystemContext::camera).
    static const ComponentInfo kCycle{.type = &reflect::TypeOf<CameraCycle>};
    registry.Contribute(kComponentPoint, kCycle);
    static const game_production::SystemInfo kCycleSystem{"カメラを順に切り替える", game_production::SystemPhase::Frame, 0, &RunCameraCycle};
    registry.Contribute(game_production::kSystemPoint, kCycleSystem);
    // Scripts: Transform in 3D (sdk/RM3D.h).
    registry.Contribute(game_production::kScriptApiPoint, ScriptTransform3DPart());
    // ... and debug drawing (rm::Debug: lines the editor shows while playing; nothing in a game).
    registry.Contribute(game_production::kScriptApiPoint, ScriptDebug3DPart());
}

}  // namespace rm::game_x_3d
