#pragma once
// The game UI over the whole game screen (game production's ScreenDrawer): laid out (UiLayout.h) and drawn
// (UiRenderer.h) - in the editor's game view (Play or not) and in the exported game, over every space's
// picture. Offered by the editor part for the game view, and for the exported game by RegisterForGame
// (this plugin's entry: the drawing is not in the tests' list of runtime parts).

#include "rm_engine/game_production/runtime/GameDraw.h"
#include "rm_engine/game_ui/runtime/UiRenderer.h"

#include <memory>

namespace rm::game_ui {

class UiScreenDrawer final : public game_production::ScreenDrawer {
public:
    void Draw(const game_production::ScreenDrawFrame& f) override;

private:
    UiRenderer renderer_;
};

std::unique_ptr<game_production::ScreenDrawer> CreateUiScreenDrawer();
const game_production::ScreenDrawInfo& ScreenDrawPart();

}  // namespace rm::game_ui
