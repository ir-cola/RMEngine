#pragma once
// ゲーム画面全体の上のゲーム UI (ゲーム制作の ScreenDrawer): 配置 (UiLayout.h) して描く
// (UiRenderer.h) - エディターのゲームビュー (Play 中でもそうでなくても) と書き出したゲームで、
// どの空間の絵の上にも。ゲームビュー用にはエディター側の部分が、書き出したゲーム用には
// RegisterForGame (このプラグインの入口: 描画はテストのランタイム部分の一覧に入っていない)
// が提供する。

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
