#include "rm_engine/game_x_2d/editor/Connector.h"

#include "editor/lang/Lang.h"

#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_x_2d/editor/SceneView2D.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
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
    // (The component types and the systems are the runtime part's: runtime/Register.cpp. Here only how
    // the editor shows them.)
    using game_production::ComponentLook;
    using game_production::kComponentLookPoint;
    // A Transform 2D is shown as it is (ローカル) or in the world (ワールド).
    static const ComponentLook kTransform{.type = &reflect::TypeOf<space2d::Transform2D>,
                                          .derivedType = &reflect::TypeOf<space2d::WorldPlacement2D>,
                                          .derive = &DeriveWorld,
                                          .underive = &UnderiveWorld,
                                          .ownLabel = editor::TrKey(L"ローカル"),
                                          .derivedLabel = editor::TrKey(L"ワールド"),
                                          .icon = "square"};
    registry.Contribute(kComponentLookPoint, kTransform);
    static const ComponentLook kSprite{.type = &reflect::TypeOf<Sprite>, .icon = "image"};
    registry.Contribute(kComponentLookPoint, kSprite);
    static const ComponentLook kAnimation{.type = &reflect::TypeOf<SpriteAnimation>, .icon = "image"};
    registry.Contribute(kComponentLookPoint, kAnimation);
    static const ComponentLook kCamera{.type = &reflect::TypeOf<Camera2D>, .icon = "camera"};
    registry.Contribute(kComponentLookPoint, kCamera);
    // ... and are seen on the plane in the 2D scene view (after 3D when both are there).
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView2D, "2d", editor::TrKey(L"2D"), /*order*/ 10};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
}

}  // namespace rm::game_x_2d
