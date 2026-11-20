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
#include "rm_engine/game_x_3d/editor/SceneView3D.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

namespace {

// A Transform's place in the world: its parents' Transforms, then its own (shown under it, read-only).
void DeriveWorld(std::span<const void* const> chain, void* out) {
    space3d::Mat4 world;
    for (const void* p : chain) world = space3d::WorldMatrix(world, *static_cast<const space3d::Transform*>(p));
    // Near its own angles: a thing at the top level shows the same numbers as its Transform.
    const Vec3 hint = static_cast<const space3d::Transform*>(chain.back())->rotation;
    *static_cast<space3d::WorldPlacement*>(out) = space3d::Decompose(world, hint);
}

// World values typed in the inspector -> the Transform that gives them under its parents.
void UnderiveWorld(std::span<const void* const> chain, const void* derived, void* component) {
    space3d::Mat4 parent;
    for (size_t i = 0; i + 1 < chain.size(); ++i) parent = space3d::WorldMatrix(parent, *static_cast<const space3d::Transform*>(chain[i]));
    auto& t = *static_cast<space3d::Transform*>(component);
    t = space3d::FromWorld(t, parent, *static_cast<const space3d::WorldPlacement*>(derived));
}

// 追加 ▸ 3D オブジェクト / カメラ / ライト: an entity with a 3D Transform (whatever the game's space is)
// and what makes it that - names in the language in use.
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
    // (a little back and up, looking down at the middle: what the 3D template's camera does)
    const int id = MakePlaced(scene, editor::TrU8("カメラ"), parent, index, {0, 3, 8}, {-18, 0, 0});
    AddComponentTo<Camera>(scene, id);
    return id;
}

int MakeLight(Scene& scene, int parent, size_t index) {
    const int id = MakePlaced(scene, editor::TrU8("光"), parent, index, {0, 6, 0}, {-50, -30, 0});  // (shining down at a slant)
    AddComponentTo<Light>(scene, id);
    return id;
}

int MakeAmbient(Scene& scene, int parent, size_t index) {
    const int id = MakePlaced(scene, editor::TrU8("環境光"), parent, index);
    AddComponentTo<AmbientLight>(scene, id);
    return id;
}

// The 空's ⋮ 新しい空のシェーダーを作る: a .rmsky in Assets/Skies to start from (SkyShader.h's NewSkyCode),
// chosen as the sky (one undo entry) and opened in the program Windows has for it.
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
    // (The component types and the systems are the runtime part's: runtime/Register.cpp. Here only how
    // the editor shows them.)
    using game_production::ComponentLook;
    using game_production::kComponentLookPoint;
    // A Transform is shown as it is (ローカル: in the parent) or in the world (ワールド: through every
    // parent), switched above its fields in the inspector.
    static const ComponentLook kTransform{.type = &reflect::TypeOf<space3d::Transform>,
                                          .derivedType = &reflect::TypeOf<space3d::WorldPlacement>,
                                          .derive = &DeriveWorld,
                                          .underive = &UnderiveWorld,
                                          .ownLabel = editor::TrKey(L"ローカル"),
                                          .derivedLabel = editor::TrKey(L"ワールド"),
                                          .icon = "cube"};
    registry.Contribute(kComponentLookPoint, kTransform);
    // カメラ and 光 - each with its own picture in the 階層.
    static const ComponentLook kCamera{.type = &reflect::TypeOf<Camera>, .icon = "camera"};
    registry.Contribute(kComponentLookPoint, kCamera);
    static const ComponentLook kLight{.type = &reflect::TypeOf<Light>, .icon = "light"};
    registry.Contribute(kComponentLookPoint, kLight);
    static const ComponentLook kAmbient{.type = &reflect::TypeOf<AmbientLight>, .icon = "light"};
    registry.Contribute(kComponentLookPoint, kAmbient);
    // ... and are seen in 3D in the scene view.
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
    // The scene's 空: its ⋮ makes a sky shader to start from.
    static const game_production::SceneSettingLook kSky{.type = &reflect::TypeOf<Sky>, .menu = &SkyMenu};
    registry.Contribute(game_production::kSceneSettingLookPoint, kSky);
    // An exported game gets its models and pictures baked (the game's own form: ExportBake.cpp).
    RegisterExportBake(registry);
}

}  // namespace rm::game_x_3d
