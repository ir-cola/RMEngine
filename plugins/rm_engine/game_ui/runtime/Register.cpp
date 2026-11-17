#include "rm_engine/game_ui/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_ui/runtime/ScriptUi.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"
#include "rm_engine/game_ui/runtime/UiService.h"

namespace rm::game_ui {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // The UI's parts (no space: a UI thing has no Transform - it is laid out on the screen).
    static const ComponentInfo kCanvas{.type = &reflect::TypeOf<UiCanvas>, .folder = "UI"};
    static const ComponentInfo kRect{.type = &reflect::TypeOf<UiRect>, .folder = "UI"};
    static const ComponentInfo kText{.type = &reflect::TypeOf<UiText>, .folder = "UI"};
    static const ComponentInfo kImage{.type = &reflect::TypeOf<UiImage>, .folder = "UI"};
    static const ComponentInfo kButton{.type = &reflect::TypeOf<UiButton>, .folder = "UI"};
    static const ComponentInfo kBar{.type = &reflect::TypeOf<UiBar>, .folder = "UI"};
    static const ComponentInfo kMotion{.type = &reflect::TypeOf<UiMotion>, .folder = "UI"};  // 出る・消える動き (UI-4)
    static const ComponentInfo kPath{.type = &reflect::TypeOf<UiPath>, .folder = "UI"};      // 道に沿って動く (UI-4)
    static const ComponentInfo kStyle{.type = &reflect::TypeOf<UiStyle>, .folder = "UI"};    // 見た目 (UI-5)
    static const ComponentInfo kVideo{.type = &reflect::TypeOf<UiVideo>, .folder = "UI"};    // 動画 (UI-5)
    for (const ComponentInfo* c : {&kCanvas, &kRect, &kText, &kImage, &kButton, &kBar, &kMotion, &kPath, &kStyle, &kVideo}) {
        registry.Contribute(kComponentPoint, *c);
    }
    // While the game plays: the mouse, the keys, the buttons' OnClick.
    static const game_production::PlayServiceInfo kService{kUiServiceName, &CreateUiService};
    registry.Contribute(game_production::kPlayServicePoint, kService);
    // Scripts: sdk/RMUI.h.
    registry.Contribute(game_production::kScriptApiPoint, ScriptUiPart());
    registry.Contribute(game_production::kScriptApiPoint, ScriptUiPartVersion1());  // (scripts built before the tweens)
}

}  // namespace rm::game_ui
