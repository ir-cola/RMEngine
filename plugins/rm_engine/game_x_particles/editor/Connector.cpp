#include "rm_engine/game_x_particles/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_particles/editor/ParticleTab.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"

namespace rm::game_x_particles {

namespace {

// 追加 ▸ エフェクト ▸ パーティクル: an entity of the game's space (2D or 3D: the particles follow it) with a
// particle system.
void FillParticles(game_production::Scene& scene, int entity) { game_production::AddComponentTo<ParticleSystem>(scene, entity); }

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::ViewPanelInfo kTab{"particles", editor::TrKey(L"パーティクル"), &CreateParticleTab};
    registry.Contribute(game_production::kViewPanelPoint, kTab);
    static const game_production::CreateItemInfo kItem{.label = editor::TrKey(L"パーティクル"), .folder = editor::TrKey(L"エフェクト"),
                                                       .fill = &FillParticles};
    registry.Contribute(game_production::kCreateItemPoint, kItem);
    // The 階層's picture and the 3D scene view's mark of an entity with particles: a sparkle.
    static const game_production::ComponentLook kLook{.type = &reflect::TypeOf<ParticleSystem>, .icon = "effect"};
    registry.Contribute(game_production::kComponentLookPoint, kLook);
}

}  // namespace rm::game_x_particles
