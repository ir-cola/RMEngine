#include "rm_engine/game_ui/runtime/UiScreenDraw.h"

#include "rm_engine/game_ui/runtime/Register.h"
#include "rm_engine/game_ui/runtime/UiService.h"

namespace rm::game_ui {

void UiScreenDrawer::Draw(const game_production::ScreenDrawFrame& f) {
    if (f.screen.x <= 0 || f.screen.y <= 0 || f.width <= 0 || f.height <= 0) return;
    const UiTree tree = BuildUi(f.scene, ScreenOf(f.settings, f.screen));
    if (tree.nodes.empty()) return;
    const Affine toDc = Translate({f.x, f.y}) * Scaling(f.width / f.screen.x, f.height / f.screen.y);
    renderer_.SetPlaying(f.playing);  // (動画 play while the game runs)
    renderer_.Draw(f.dc, f.gfx, tree, toDc, f.asset);
}

std::unique_ptr<game_production::ScreenDrawer> CreateUiScreenDrawer() { return std::make_unique<UiScreenDrawer>(); }

const game_production::ScreenDrawInfo& ScreenDrawPart() {
    static const game_production::ScreenDrawInfo kDraw{0, &CreateUiScreenDrawer};
    return kDraw;
}

namespace {

// What the exported game registers of ゲーム UI (its generated main calls it by the C name below): the
// runtime part and the drawing over the screen.
void RegisterForGame(RuntimeRegistry& registry) {
    RegisterRuntime(registry);
    registry.Contribute(game_production::kScreenDrawPoint, ScreenDrawPart());
}

}  // namespace

}  // namespace rm::game_ui

RM_RUNTIME_ENTRY(rm_engine, game_ui, rm::game_ui::RegisterForGame)
