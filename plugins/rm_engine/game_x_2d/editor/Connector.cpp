#include "rm_engine/game_x_2d/editor/Connector.h"

#include "editor/lang/Lang.h"

#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_2d/editor/SceneView2D.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/Spin2D.h"
#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::game_x_2d {

namespace {

// A Transform 2D's place in the world: its parents' Transforms 2D, then its own (shown under it).
void DeriveWorld(std::span<const void* const> chain, void* out) {
    space2d::Mat3 world;
    int order = 0;
    for (const void* p : chain) {
        const auto& t = *static_cast<const space2d::Transform2D*>(p);
        world = space2d::WorldMatrix(world, t);
        order += t.order;
    }
    // Near its own angle: a thing at the top level shows the same numbers as its Transform 2D.
    const float hint = static_cast<const space2d::Transform2D*>(chain.back())->rotation;
    auto& placement = *static_cast<space2d::WorldPlacement2D*>(out);
    placement = space2d::Decompose(world, hint);
    placement.order = order;
}

// World values typed in the inspector -> the Transform 2D that gives them under its parents.
void UnderiveWorld(std::span<const void* const> chain, const void* derived, void* component) {
    space2d::Mat3 parent;
    int order = 0;
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
        const auto& t = *static_cast<const space2d::Transform2D*>(chain[i]);
        parent = space2d::WorldMatrix(parent, t);
        order += t.order;
    }
    auto& t = *static_cast<space2d::Transform2D*>(component);
    t = space2d::FromWorld(t, parent, order, *static_cast<const space2d::WorldPlacement2D*>(derived));
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // A 2D game's entities have a place on the plane - shown as it is (ローカル) or in the world (ワールド).
    // New entities get it while the game's space is 2D (the 3D Transform while it is 3D).
    static const game_production::ComponentInfo kTransform{&reflect::TypeOf<space2d::Transform2D>, true,
                                                           &reflect::TypeOf<space2d::WorldPlacement2D>, &DeriveWorld,
                                                           &UnderiveWorld, editor::TrKey(L"ローカル"), editor::TrKey(L"ワールド"), "square",
                                                           /*multiple*/ false, /*placement*/ true, /*space*/ "2d"};
    registry.Contribute(game_production::kComponentPoint, kTransform);
    // ... and may be drawn as a picture (added by hand).
    static const game_production::ComponentInfo kSprite{.type = &reflect::TypeOf<Sprite>, .icon = "image"};
    registry.Contribute(game_production::kComponentPoint, kSprite);
    // ... a sprite may step through the cells of its image (an atlas) while the game plays.
    static const game_production::ComponentInfo kAnimation{.type = &reflect::TypeOf<SpriteAnimation>, .icon = "image"};
    registry.Contribute(game_production::kComponentPoint, kAnimation);
    static const game_production::SystemInfo kAnimationSystem{"スプライトのアニメーション", game_production::SystemPhase::Frame, 0,
                                                              &RunSpriteAnimation};
    registry.Contribute(game_production::kSystemPoint, kAnimationSystem);
    // ... a camera: what the game sees (the game settings' 最初のカメラ points at one).
    static const game_production::ComponentInfo kCamera{.type = &reflect::TypeOf<Camera2D>, .icon = "camera"};
    registry.Contribute(game_production::kComponentPoint, kCamera);
    // 回り続ける (2D): turns its entity while the game plays (several add up), and the system that does it.
    static const game_production::ComponentInfo kSpin{.type = &reflect::TypeOf<Spin2D>, .multiple = true};
    registry.Contribute(game_production::kComponentPoint, kSpin);
    static const game_production::SystemInfo kSpinSystem{"回り続ける (2D)", game_production::SystemPhase::Frame, 0, &RunSpin2D};
    registry.Contribute(game_production::kSystemPoint, kSpinSystem);
    // ... and are seen on the plane in the 2D scene view (after 3D when both are there).
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView2D, "2d", editor::TrKey(L"2D"), /*order*/ 10};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
}

}  // namespace rm::game_x_2d
