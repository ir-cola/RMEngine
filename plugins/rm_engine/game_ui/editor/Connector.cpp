#include "rm_engine/game_ui/editor/Connector.h"

#include "editor/lang/Lang.h"
#include "base/Utf.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_ui/editor/UiView.h"
#include "rm_engine/game_ui/runtime/UiScreenDraw.h"
#include "rm_engine/game_ui/runtime/UiComponents.h"
#include "rm_engine/game_ui/runtime/UiLayout.h"
#include "rm_engine/game_ui/runtime/UiService.h"

#include <algorithm>

namespace rm::game_ui {

namespace {

using editor::TrKey;
using editor::TrU8;
using game_production::Scene;

template <class T>
T* Add(Scene& scene, int id) {
    return game_production::AddComponentTo<T>(scene, id);
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

// UI の配置's アンカー changed in the inspector: 位置 and 大きさ worked out again so that the thing stays
// where it is (as laid out on the game settings' 画面). Not laid out (switched off): left as typed.
void KeepPlace(const Scene& scene, const game_production::GameSettings& settings, int entity, const reflect::FieldPath&, const void* before,
               void* after) {
    const UiRect& was = *static_cast<const UiRect*>(before);
    UiRect& now = *static_cast<UiRect*>(after);
    if (now.anchor == was.anchor) return;
    const Vec2 screen{static_cast<float>(std::max(1, settings.display.width)), static_cast<float>(std::max(1, settings.display.height))};
    const int show[] = {entity};
    const UiTree tree = BuildUi(scene, ScreenOf(settings, screen), show);
    const UiNode* n = tree.Find(entity);
    if (!n || n->parent < 0 || n->parent >= static_cast<int>(tree.nodes.size())) return;
    const UiRect moved = Reanchored(was, now.anchor, tree.nodes[static_cast<size_t>(n->parent)].box);
    now.position = moved.position;
    now.size = moved.size;
}

// デバッグ ▸ ゲーム UI: which button is chosen, in which window, by the keys or the mouse.
void UiLines(const game_production::DebugContext& ctx, std::vector<game_production::DebugInfoLine>& out) {
    const auto* ui = ctx.playing && ctx.world && ctx.world->services ? dynamic_cast<const UiService*>(ctx.world->services->Find(kUiServiceName)) : nullptr;
    if (!ui || !ctx.scene) {
        out.push_back({L"", editor::Tr(L"Play 中に、選んでいるボタンが出ます")});
        return;
    }
    const UiFocus& s = ui->State();
    auto name = [&](int id) -> std::wstring {
        const game_production::SceneEntity* e = id ? ctx.scene->Find(id) : nullptr;
        return e ? Utf8ToWide(e->data.name) : editor::Tr(L"(なし)");
    };
    out.push_back({editor::Tr(L"選んでいるボタン"), name(s.focus), s.focus != 0});
    out.push_back({editor::Tr(L"そのボタンの窓"), name(s.window)});
    out.push_back({editor::Tr(L"操作"), s.keyboard ? editor::Tr(L"キー ・ パッド (選んだボタンを見せる)") : editor::Tr(L"マウス")});
    out.push_back({editor::Tr(L"出ている窓"), editor::TrF(L"{} 個 (戻るで閉じられるもの {} 個)", s.shown.size(), s.visits.size())});
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::DebugInfoInfo kDebug{editor::TrKey(L"ゲーム UI"), 40, game_production::DebugTab::Game, &UiLines};
    registry.Contribute(game_production::kDebugInfoPoint, kDebug);
    // The UI over the game view (the exported game registers its own: UiScreenDraw.cpp).
    registry.Contribute(game_production::kScreenDrawPoint, ScreenDrawPart());
    // The UI view: a tab next to the scene and the game.
    static const game_production::ViewPanelInfo kView{"ui", TrKey(L"UI"), &CreateUiView};
    registry.Contribute(game_production::kViewPanelPoint, kView);
    // The 階層's 追加 ▸ UI (a canvas after the selection; the rest under the selected one: on its canvas).
    static const game_production::CreateItemInfo kItems[] = {
        {.label = TrKey(L"キャンバス"), .folder = L"UI", .make = &MakeCanvas},
        {.label = TrKey(L"ボタン"), .folder = L"UI", .make = &MakeButton, .child = true},
        {.label = TrKey(L"文字"), .folder = L"UI", .make = &MakeText, .child = true},
        {.label = TrKey(L"画像"), .folder = L"UI", .make = &MakeImage, .child = true},
        {.label = TrKey(L"バー"), .folder = L"UI", .make = &MakeBar, .child = true},
    };
    for (const auto& item : kItems) registry.Contribute(game_production::kCreateItemPoint, item);
    // Their pictures in the 階層.
    static const game_production::ComponentLook kLooks[] = {
        {.type = &reflect::TypeOf<UiCanvas>, .icon = "square"},
        {.type = &reflect::TypeOf<UiImage>, .icon = "image"},
        {.type = &reflect::TypeOf<UiRect>, .adjust = &KeepPlace},
    };
    for (const auto& look : kLooks) registry.Contribute(game_production::kComponentLookPoint, look);
}

}  // namespace rm::game_ui
