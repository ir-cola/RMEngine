#include "rm_engine/theme_explore/editor/Explore.h"

#include "editor/widgets/MovingBackdrop.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/theme_explore/editor/Scenes.h"

namespace rm::theme_explore {

namespace {

// 絵ごとに、ウィンドウの背景 (強さ 1) とパネル・バーの上 (弱く) の 2 つの描き方。
template <const editor::w::MovingLook& Look>
void DrawWindow(ui::Context& c, const ui::Rect&, bool floating) {
    editor::w::DrawMovingBackdrop(c, Look, floating);
}

template <const editor::w::MovingLook& Look>
void DrawSurface(ui::Context& c, const ui::Rect& r, float corner, bool floating) {
    editor::w::DrawMovingBackdropOn(c, Look, r, corner, floating, scenes::kPanelStrength);
}

constexpr editor::w::OrnamentInfo kSpace{"space", &DrawWindow<scenes::kSpace>, nullptr, &DrawSurface<scenes::kSpace>};
constexpr editor::w::OrnamentInfo kDeepSea{"deep_sea", &DrawWindow<scenes::kDeepSea>, nullptr, &DrawSurface<scenes::kDeepSea>};
constexpr editor::w::OrnamentInfo kVolcano{"volcano", &DrawWindow<scenes::kVolcano>, nullptr, &DrawSurface<scenes::kVolcano>};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    for (const editor::w::OrnamentInfo* o : {&kSpace, &kDeepSea, &kVolcano}) registry.Contribute(editor::w::kOrnamentPoint, *o);
}

}  // namespace rm::theme_explore
