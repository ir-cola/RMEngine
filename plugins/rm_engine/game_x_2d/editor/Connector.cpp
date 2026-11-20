#include "rm_engine/game_x_2d/editor/Connector.h"

#include "editor/lang/Lang.h"

#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_x_2d/editor/ExportBake2D.h"
#include "rm_engine/game_x_2d/editor/SceneView2D.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::game_x_2d {

namespace {

// Transform 2D のワールドでの場所: 親の Transform 2D、次にそれ自身のもの (その下に表示)。
void DeriveWorld(std::span<const void* const> chain, void* out) {
    space2d::Mat3 world;
    int order = 0;
    for (const void* p : chain) {
        const auto& t = *static_cast<const space2d::Transform2D*>(p);
        world = space2d::WorldMatrix(world, t);
        order += t.order;
    }
    // 自分の角度の近くに: トップレベルのものは、その Transform 2D と同じ数を表示する。
    const float hint = static_cast<const space2d::Transform2D*>(chain.back())->rotation;
    auto& placement = *static_cast<space2d::WorldPlacement2D*>(out);
    placement = space2d::Decompose(world, hint);
    placement.order = order;
}

// インスペクターに入力したワールドの値 -> 親の下でそれを与える Transform 2D。
void UnderiveWorld(std::span<const void* const> chain, const void* derived, void* component) {
    space2d::Mat3 parent;
    int order = 0;
    for (size_t i = 0; i + 1 < chain.size(); ++i) {
        const auto& t = *static_cast<const space2d::Transform2D*>(chain[i]);
        parent = space2d::WorldMatrix(parent, t);
        order += t.order;
    }
    auto& t = *static_cast<space2d::Transform2D*>(component);
    t = space2d::FromWorld(t, parent, order, *static_cast<const space2d::WorldPlacement2D*>(derived));
}

// 追加 ▸ 2D オブジェクト / カメラ: Transform 2D を持つエンティティ (ゲームの空間が何であっても) と、
// それを成り立たせる部品 - 名前は使っている言語で。
using game_production::AddComponentTo;
using game_production::Scene;

template <class T>
int MakeWith(Scene& scene, std::string name, int parent, size_t index) {
    const int id = scene.Create(std::move(name), parent, index);
    AddComponentTo<space2d::Transform2D>(scene, id);
    AddComponentTo<T>(scene, id);
    return id;
}

int MakeSprite(Scene& scene, int parent, size_t index) { return MakeWith<Sprite>(scene, editor::TrU8("スプライト"), parent, index); }
int MakeCamera(Scene& scene, int parent, size_t index) { return MakeWith<Camera2D>(scene, editor::TrU8("カメラ"), parent, index); }

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // (部品の型とシステムはランタイム側のもの: runtime/Register.cpp。ここはエディターがそれをどう
    // 見せるかだけ。)
    using game_production::ComponentLook;
    using game_production::kComponentLookPoint;
    // Transform 2D はそのまま (ローカル) か、ワールドで (ワールド) 表示する。
    static const ComponentLook kTransform{.type = &reflect::TypeOf<space2d::Transform2D>,
                                          .derivedType = &reflect::TypeOf<space2d::WorldPlacement2D>,
                                          .derive = &DeriveWorld,
                                          .underive = &UnderiveWorld,
                                          .ownLabel = editor::TrKey(L"ローカル"),
                                          .derivedLabel = editor::TrKey(L"ワールド"),
                                          .icon = "square"};
    registry.Contribute(kComponentLookPoint, kTransform);
    static const ComponentLook kSprite{.type = &reflect::TypeOf<Sprite>, .icon = "image"};
    registry.Contribute(kComponentLookPoint, kSprite);
    static const ComponentLook kAnimation{.type = &reflect::TypeOf<SpriteAnimation>, .icon = "image"};
    registry.Contribute(kComponentLookPoint, kAnimation);
    static const ComponentLook kCamera{.type = &reflect::TypeOf<Camera2D>, .icon = "camera"};
    registry.Contribute(kComponentLookPoint, kCamera);
    // ... そして 2D シーンビューの平面の上に見える (両方あるときは 3D の後)。
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView2D, "2d", editor::TrKey(L"2D"), /*order*/ 10};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
    // The 階層's 追加 ▸.
    static const game_production::CreateItemInfo kItems[] = {
        {.label = editor::TrKey(L"スプライト"), .folder = editor::TrKey(L"2D オブジェクト"), .make = &MakeSprite},
        {.label = editor::TrKey(L"2D カメラ"), .folder = editor::TrKey(L"カメラ"), .make = &MakeCamera},
    };
    for (const auto& item : kItems) registry.Contribute(game_production::kCreateItemPoint, item);
    // 書き出すゲーム用のスプライトの画像: ファイルとその大きさ、取り込みの設定。
    RegisterExportBake(registry);
}

}  // namespace rm::game_x_2d
