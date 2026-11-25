#include "rm_engine/game_x_3d/editor/Connector.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/app/Worker.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "platform/Shell.h"

#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/SceneSettingLook.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_3d/editor/ExportBake.h"
#include "rm_engine/game_x_3d/editor/MaterialEditor.h"
#include "rm_engine/game_x_3d/editor/SceneView3D.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

namespace {

// Transform のワールドでの位置: 親の Transform を順にたどり、最後に自分のもの
// (その下に表示。読み取り専用)。
void DeriveWorld(std::span<const void* const> chain, void* out) {
    space3d::Mat4 world;
    for (const void* p : chain) world = space3d::WorldMatrix(world, *static_cast<const space3d::Transform*>(p));
    // 自分の角度に近い値: 一番上の階層にあるものは Transform と同じ数字を表示する。
    const Vec3 hint = static_cast<const space3d::Transform*>(chain.back())->rotation;
    *static_cast<space3d::WorldPlacement*>(out) = space3d::Decompose(world, hint);
}

// インスペクターで打ったワールドの値 -> 親の下でその値になる Transform。
void UnderiveWorld(std::span<const void* const> chain, const void* derived, void* component) {
    space3d::Mat4 parent;
    for (size_t i = 0; i + 1 < chain.size(); ++i) parent = space3d::WorldMatrix(parent, *static_cast<const space3d::Transform*>(chain[i]));
    auto& t = *static_cast<space3d::Transform*>(component);
    t = space3d::FromWorld(t, parent, *static_cast<const space3d::WorldPlacement*>(derived));
}

// 追加 ▸ 3D オブジェクト / カメラ / ライト: 3D の Transform を持つエンティティ
// (ゲームの空間が何であっても) と、それを作る部品。名前は使っている言語で出す。
using game_production::AddComponentTo;
using game_production::Scene;

int MakePlaced(Scene& scene, std::string name, int parent, size_t index, Vec3 position = {}, Vec3 rotation = {}) {
    const int id = scene.Create(std::move(name), parent, index);
    if (auto* t = AddComponentTo<space3d::Transform>(scene, id)) {
        t->position = position;
        t->rotation = rotation;
    }
    return id;
}

template <MeshShape S>
int MakeShape(Scene& scene, int parent, size_t index) {
    static constexpr const char* kNames[] = {"", "箱", "球", "円柱", "カプセル", "平面"};
    const int id = MakePlaced(scene, editor::TrU8(kNames[static_cast<size_t>(S)]), parent, index);
    if (auto* m = AddComponentTo<MeshRenderer>(scene, id)) m->shape = S;
    return id;
}

int MakeCamera(Scene& scene, int parent, size_t index) {
    // (少し後ろの上から、真ん中を見下ろす: 3D テンプレートのカメラと同じ)
    const int id = MakePlaced(scene, editor::TrU8("カメラ"), parent, index, {0, 3, -8}, {18, 0, 0});  // (左手系: 手前の -Z から奥を見る)
    AddComponentTo<Camera>(scene, id);
    return id;
}

int MakeLight(Scene& scene, int parent, size_t index) {
    const int id = MakePlaced(scene, editor::TrU8("光"), parent, index, {0, 6, 0}, {50, 30, 0});  // (斜めに照らす)
    AddComponentTo<Light>(scene, id);
    return id;
}

int MakeAmbient(Scene& scene, int parent, size_t index) {
    const int id = MakePlaced(scene, editor::TrU8("環境光"), parent, index);
    AddComponentTo<AmbientLight>(scene, id);
    return id;
}

// 空の ⋮ 新しい空のシェーダーを作る: Assets/Skies に元になる .rmsky を作り (SkyShader.h の NewSkyCode)、
// その空に選び (Undo は 1 つ)、Windows でその種類に割り当てられたプログラムで開く。
void CreateSkyShader(const game_production::SceneSettingMenuHost& h) {
    namespace fs = std::filesystem;
    const fs::path dir = h.host->GetProject().AssetsDir() / L"Skies";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path file;
    for (int n = 1; n < 1000; ++n) {
        file = dir / (n == 1 ? std::wstring(L"NewSky.rmsky") : std::format(L"NewSky {}.rmsky", n));
        if (!fs::exists(file, ec)) break;
    }
    std::string err;
    if (!platform::WriteFileAtomic(file, space3d::NewSkyCode(), &err)) {
        log::Error("空のシェーダーを作れません ({}): {}", PathToUtf8(file.wstring()), err);
        return;
    }
    const AssetRef ref = h.host->AssetOf(file);
    if (ref.Empty()) {
        log::Warn("空のシェーダーを作りましたが、アセットとして読めません: {}", PathToUtf8(file.wstring()));
        return;
    }
    h.edit("新しい空のシェーダーを作る", [ref](void* setting) {
        auto& sky = *static_cast<Sky*>(setting);
        sky.kind = SkyKind::Custom;
        sky.shader = ref;
    });
    log::Info("空のシェーダーを作りました: {}", PathToUtf8(file.wstring()));
    editor::PostToUi([file] {
        if (!platform::OpenFileOutside(file)) log::Error("ファイルを開けませんでした: {}", PathToUtf8(file.wstring()));
    });
}

std::vector<editor::w::Action> SkyMenu(const game_production::SceneSettingMenuHost& h) {
    editor::w::Action make;
    make.label = editor::Tr(L"新しい空のシェーダーを作る");
    make.enabled = !h.play;
    make.run = [h] { CreateSkyShader(h); };
    return {make};
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // (部品の型とシステムはランタイム側のもの: runtime/Register.cpp。ここはエディターでの見せ方だけ。)
    using game_production::ComponentLook;
    using game_production::kComponentLookPoint;
    // Transform はそのまま (ローカル: 親から見た値) か、ワールド (ワールド: 親をすべて通した値) で
    // 表示する。インスペクターの項目の上で切り替える。
    static const ComponentLook kTransform{.type = &reflect::TypeOf<space3d::Transform>,
                                          .derivedType = &reflect::TypeOf<space3d::WorldPlacement>,
                                          .derive = &DeriveWorld,
                                          .underive = &UnderiveWorld,
                                          .ownLabel = editor::TrKey(L"ローカル"),
                                          .derivedLabel = editor::TrKey(L"ワールド"),
                                          .icon = "cube"};
    registry.Contribute(kComponentLookPoint, kTransform);
    // カメラと光 - どちらも 階層 に専用の絵が出る。
    static const ComponentLook kCamera{.type = &reflect::TypeOf<Camera>, .icon = "camera"};
    registry.Contribute(kComponentLookPoint, kCamera);
    static const ComponentLook kLight{.type = &reflect::TypeOf<Light>, .icon = "light"};
    registry.Contribute(kComponentLookPoint, kLight);
    static const ComponentLook kAmbient{.type = &reflect::TypeOf<AmbientLight>, .icon = "light"};
    registry.Contribute(kComponentLookPoint, kAmbient);
    // ... シーンビューでは 3D で見える。
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView3D, "3d", editor::TrKey(L"3D"), /*order*/ 0};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
    // The 階層's 追加 ▸.
    using editor::TrKey;
    static const game_production::CreateItemInfo kItems[] = {
        {.label = TrKey(L"箱"), .folder = TrKey(L"3D オブジェクト"), .make = &MakeShape<MeshShape::Box>, .made = "box"},
        {.label = TrKey(L"球"), .folder = TrKey(L"3D オブジェクト"), .make = &MakeShape<MeshShape::Sphere>, .made = "sphere"},
        {.label = TrKey(L"円柱"), .folder = TrKey(L"3D オブジェクト"), .make = &MakeShape<MeshShape::Cylinder>, .made = "cylinder"},
        {.label = TrKey(L"カプセル"), .folder = TrKey(L"3D オブジェクト"), .make = &MakeShape<MeshShape::Capsule>, .made = "capsule"},
        {.label = TrKey(L"平面"), .folder = TrKey(L"3D オブジェクト"), .make = &MakeShape<MeshShape::Plane>, .made = "plane"},
        {.label = TrKey(L"3D カメラ"), .folder = TrKey(L"カメラ"), .make = &MakeCamera},
        {.label = TrKey(L"光"), .folder = TrKey(L"ライト"), .make = &MakeLight},
        {.label = TrKey(L"環境光"), .folder = TrKey(L"ライト"), .make = &MakeAmbient},
    };
    for (const auto& item : kItems) registry.Contribute(game_production::kCreateItemPoint, item);
    // シーンの 空: その ⋮ で元になる空のシェーダーを作る。
    static const game_production::SceneSettingLook kSky{.type = &reflect::TypeOf<Sky>, .menu = &SkyMenu};
    registry.Contribute(game_production::kSceneSettingLookPoint, kSky);
    // 書き出したゲームには、モデルと画像を焼き込む (ゲーム用の形: ExportBake.cpp)。
    RegisterExportBake(registry);
    // マテリアル (.rmmat): アセットのパネルで作り、インスペクターで編集し、球のサムネイルで見せる。
    RegisterMaterialEditor(registry);
}

}  // namespace rm::game_x_3d
