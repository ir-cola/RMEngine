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

// 各行が作るもの: 見せられる状態 (大きさ、色、言葉) - 名前は使っている言語で。
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
    // そのラベル: それを埋める文字。
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

// UI の配置のアンカーをインスペクターで変えた: そのものが今の場所に残るように 位置 と 大きさ を
// 計算し直す (ゲーム設定の 画面 に配置したとおりに)。配置されない (切ってある) もの: 入力のまま。
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

// デバッグ ▸ ゲーム UI: どのボタンが、どのウィンドウで、キーかマウスで選ばれているか。
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
    // ゲームのビューの上の UI (書き出したゲームは自分のものを登録する: UiScreenDraw.cpp)。
    registry.Contribute(game_production::kScreenDrawPoint, ScreenDrawPart());
    // UI ビュー: シーンとゲームの隣のタブ。
    static const game_production::ViewPanelInfo kView{"ui", TrKey(L"UI ビュー"), &CreateUiView};
    registry.Contribute(game_production::kViewPanelPoint, kView);
    // 階層の 追加 ▸ UI (キャンバスは選択の後ろに、それ以外は選択中のものの下 = そのキャンバスの中に)。
    static const game_production::CreateItemInfo kItems[] = {
        {.label = TrKey(L"キャンバス"), .folder = L"UI", .make = &MakeCanvas},
        {.label = TrKey(L"ボタン"), .folder = L"UI", .make = &MakeButton, .child = true},
        {.label = TrKey(L"文字"), .folder = L"UI", .make = &MakeText, .child = true},
        {.label = TrKey(L"画像"), .folder = L"UI", .make = &MakeImage, .child = true},
        {.label = TrKey(L"バー"), .folder = L"UI", .make = &MakeBar, .child = true},
    };
    for (const auto& item : kItems) registry.Contribute(game_production::kCreateItemPoint, item);
    // 階層に出すそれらの絵。
    static const game_production::ComponentLook kLooks[] = {
        {.type = &reflect::TypeOf<UiCanvas>, .icon = "square"},
        {.type = &reflect::TypeOf<UiImage>, .icon = "image"},
        {.type = &reflect::TypeOf<UiRect>, .adjust = &KeepPlace},
    };
    for (const auto& look : kLooks) registry.Contribute(game_production::kComponentLookPoint, look);
}

}  // namespace rm::game_ui
