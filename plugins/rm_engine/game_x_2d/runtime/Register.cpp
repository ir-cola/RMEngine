#include "rm_engine/game_x_2d/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/ScriptDebug2D.h"
#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"
#include "rm_engine/game_x_2d/runtime/Spin2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::game_x_2d {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // A 2D game's entities have a place on the plane: new entities get it while the game's space is 2D
    // (the 3D Transform while it is 3D). It is where a placed prefab is.
    static const ComponentInfo kTransform{.type = &reflect::TypeOf<space2d::Transform2D>, .onNewEntities = true, .placement = true, .space = "2d"};
    registry.Contribute(kComponentPoint, kTransform);
    // ... may be drawn as a picture (added by hand), stepping through the cells of its image (an atlas).
    static const ComponentInfo kSprite{.type = &reflect::TypeOf<Sprite>};
    registry.Contribute(kComponentPoint, kSprite);
    static const ComponentInfo kAnimation{.type = &reflect::TypeOf<SpriteAnimation>};
    registry.Contribute(kComponentPoint, kAnimation);
    static const game_production::SystemInfo kAnimationSystem{"スプライトのアニメーション", game_production::SystemPhase::Frame, 0,
                                                              &RunSpriteAnimation};
    registry.Contribute(game_production::kSystemPoint, kAnimationSystem);
    // ... a camera: what the game sees (the game settings' 最初のカメラ points at one).
    static const ComponentInfo kCamera{.type = &reflect::TypeOf<Camera2D>};
    registry.Contribute(kComponentPoint, kCamera);
    // 回り続ける (2D): turns its entity while the game plays (several add up), and the system that does it.
    static const ComponentInfo kSpin{.type = &reflect::TypeOf<Spin2D>, .multiple = true};
    registry.Contribute(kComponentPoint, kSpin);
    static const game_production::SystemInfo kSpinSystem{"回り続ける (2D)", game_production::SystemPhase::Frame, 0, &RunSpin2D};
    registry.Contribute(game_production::kSystemPoint, kSpinSystem);
    // Scripts: Transform 2D (sdk/RM2D.h).
    registry.Contribute(game_production::kScriptApiPoint, ScriptTransform2DPart());
    // ... and debug drawing (rm::Debug: lines the editor shows while playing; nothing in a game).
    registry.Contribute(game_production::kScriptApiPoint, ScriptDebug2DPart());
}

}  // namespace rm::game_x_2d
