#include "rm_engine/game_ui/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_ui/editor/UiView.h"
#include "rm_engine/game_ui/runtime/UiScreenDraw.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"

namespace rm::game_ui {

namespace {

using editor::TrKey;
using editor::TrU8;
using game_production::Scene;

template <class T>
T* Add(Scene& scene, int id) {
    scene.AddComponent(id, reflect::TypeOf<T>());
    game_production::SceneEntity* e = scene.Find(id);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<T>().name) : nullptr;
    return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
}

// What each row makes: ready to be seen (a size, a colour, a word) - names in the language in use.
int MakeCanvas(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("キャンバス"), parent, index);
    Add<UiCanvas>(scene, id);
    return id;
}

int MakeText(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("文字"), parent, index);
    if (UiRect* r = Add<UiRect>(scene, id)) r->size = {480, 80};
    if (UiText* t = Add<UiText>(scene, id)) t->text = TrU8("文字");
    return id;
}

int MakeImage(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("画像"), parent, index);
    if (UiRect* r = Add<UiRect>(scene, id)) r->size = {200, 200};
    Add<UiImage>(scene, id);
    return id;
}

int MakeButton(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("ボタン"), parent, index);
    if (UiRect* r = Add<UiRect>(scene, id)) r->size = {320, 88};
    if (UiImage* i = Add<UiImage>(scene, id)) i->color = {0.22f, 0.42f, 0.85f, 1};
    Add<UiButton>(scene, id);
    // Its label: a text filling it.
    const int label = scene.Create(TrU8("文字"), id);
    if (UiRect* r = Add<UiRect>(scene, label)) {
        r->anchor = UiAnchor::Stretch;
        r->size = {0, 0};
    }
    if (UiText* t = Add<UiText>(scene, label)) t->text = TrU8("ボタン");
    return id;
}

int MakeBar(Scene& scene, int parent, size_t index) {
    const int id = scene.Create(TrU8("バー"), parent, index);
    if (UiRect* r = Add<UiRect>(scene, id)) r->size = {480, 40};
    Add<UiBar>(scene, id);
    return id;
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // The UI over the game view (the exported game registers its own: UiScreenDraw.cpp).
    registry.Contribute(game_production::kScreenDrawPoint, ScreenDrawPart());
    // The UI view: a tab next to the scene and the game.
    static const game_production::ViewPanelInfo kView{"ui", TrKey(L"UI"), &CreateUiView};
    registry.Contribute(game_production::kViewPanelPoint, kView);
    // The 階層's right-click: エンティティを作る's neighbours.
    static const game_production::CreateItemInfo kItems[] = {
        {TrKey(L"キャンバスを作る"), &MakeCanvas, false},
        {TrKey(L"ボタンを作る"), &MakeButton, true},
        {TrKey(L"文字を作る"), &MakeText, true},
        {TrKey(L"画像を作る"), &MakeImage, true},
        {TrKey(L"バーを作る"), &MakeBar, true},
    };
    for (const auto& item : kItems) registry.Contribute(game_production::kCreateItemPoint, item);
    // Their pictures in the 階層.
    static const game_production::ComponentLook kLooks[] = {
        {.type = &reflect::TypeOf<UiCanvas>, .icon = "square"},
        {.type = &reflect::TypeOf<UiImage>, .icon = "image"},
    };
    for (const auto& look : kLooks) registry.Contribute(game_production::kComponentLookPoint, look);
}

}  // namespace rm::game_ui
