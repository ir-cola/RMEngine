#include "rm_engine/game_x_particles/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_particles/editor/ParticleTab.h"

namespace rm::game_x_particles {

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::ViewPanelInfo kTab{"particles", editor::TrKey(L"パーティクル"), &CreateParticleTab};
    registry.Contribute(game_production::kViewPanelPoint, kTab);
}

}  // namespace rm::game_x_particles
