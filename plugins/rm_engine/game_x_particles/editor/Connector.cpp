#include "rm_engine/game_x_particles/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_particles/editor/ParticleTab.h"
#include "rm_engine/game_x_particles/runtime/ParticleStats.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"

#include <algorithm>

namespace rm::game_x_particles {

namespace {

// 追加 ▸ エフェクト ▸ パーティクル: an entity of the game's space (2D or 3D: the particles follow it) with a
// particle system.
void FillParticles(game_production::Scene& scene, int entity) { game_production::AddComponentTo<ParticleSystem>(scene, entity); }

// デバッグ ▸ パーティクル: each 3D picture's particles (the players count the alive ones while it is shown).
void ParticleLines(const game_production::DebugContext&, std::vector<game_production::DebugInfoLine>& out) {
    particles::CountAliveFor(1.5);
    std::vector<PictureParticles> list = RecentParticles();
    std::sort(list.begin(), list.end(), [](const PictureParticles& a, const PictureParticles& b) { return a.picture > b.picture; });
    for (const PictureParticles& p : list) {
        const std::wstring where = p.picture == "scene"  ? editor::Tr(L"シーンビュー")
                                   : p.picture == "game" ? editor::Tr(L"ゲームビュー")
                                                         : editor::Tr(L"プレビュー");
        const std::wstring alive = p.aliveKnown ? editor::TrF(L"粒 {} 個", p.alive) : editor::Tr(L"粒 (数えています…)");
        out.push_back({where, editor::TrF(L"{} ・ 上限 {} 個 ・ エフェクト {} 個 ・ エミッター {} 個 ・ 部品 {} 個", alive, p.capacity, p.effects, p.emitters,
                                          p.systems)});
    }
    if (list.empty()) out.push_back({L"", editor::Tr(L"3D のビューにパーティクルが出ていません (止まっているときは、選んだものだけが出ます)")});
}
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
    // デバッグ: how many particles each view plays.
    static const game_production::DebugInfoInfo kDebug{editor::TrKey(L"パーティクル"), 10, game_production::DebugTab::Performance, &ParticleLines};
    registry.Contribute(game_production::kDebugInfoPoint, kDebug);
}

}  // namespace rm::game_x_particles
