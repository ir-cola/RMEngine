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

// 追加 ▸ エフェクト ▸ パーティクル: ゲームの空間 (2D でも 3D でも: パーティクルはそれに
// ついていく) に、パーティクルシステムを持つエンティティを置く。
void FillParticles(game_production::Scene& scene, int entity) { game_production::AddComponentTo<ParticleSystem>(scene, entity); }

// デバッグ ▸ パーティクル: 各 3D の絵のパーティクル (その絵が出ているあいだ、プレイヤーが生きている数を数える)。
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
    // パーティクルを持つエンティティの、階層の絵と 3D シーンビューの印: きらめき。
    static const game_production::ComponentLook kLook{.type = &reflect::TypeOf<ParticleSystem>, .icon = "effect"};
    registry.Contribute(game_production::kComponentLookPoint, kLook);
    // デバッグ: ビューごとに、いくつのパーティクルを再生しているか。
    static const game_production::DebugInfoInfo kDebug{editor::TrKey(L"パーティクル"), 10, game_production::DebugTab::Performance, &ParticleLines};
    registry.Contribute(game_production::kDebugInfoPoint, kDebug);
}

}  // namespace rm::game_x_particles
