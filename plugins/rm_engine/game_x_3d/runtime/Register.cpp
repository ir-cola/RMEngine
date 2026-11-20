#include "rm_engine/game_x_3d/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/CameraCycle.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/ScriptDebug3D.h"
#include "rm_engine/game_x_3d/runtime/ScriptSky3D.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // 3D のゲームのエンティティは 3D の空間に位置を持つ: ゲームの空間が 3D の間、新しい
    // エンティティはすべて持つ。置いた Prefab があるのもここ (その値は置いたものの値のまま)。
    static const ComponentInfo kTransform{.type = &reflect::TypeOf<space3d::Transform>, .onNewEntities = true, .placement = true, .space = "3d", .folder = "3D"};
    registry.Contribute(kComponentPoint, kTransform);
    // ... そして 3D モデルとして描かれることもある (手で足す: 新しいエンティティは、持つまでは箱)。
    static const ComponentInfo kMesh{.type = &reflect::TypeOf<MeshRenderer>, .folder = "3D"};
    registry.Contribute(kComponentPoint, kMesh);
    // カメラ (ゲームが見るもの: ゲーム設定の 最初のカメラ)、光、環境光。
    static const ComponentInfo kCamera{.type = &reflect::TypeOf<Camera>, .camera = true, .folder = "3D"};
    registry.Contribute(kComponentPoint, kCamera);
    static const ComponentInfo kLight{.type = &reflect::TypeOf<Light>, .folder = "3D"};
    registry.Contribute(kComponentPoint, kLight);
    static const ComponentInfo kAmbient{.type = &reflect::TypeOf<AmbientLight>, .folder = "3D"};
    registry.Contribute(kComponentPoint, kAmbient);
    // 回り続ける: ゲームが動く間そのエンティティを回す (複数は足し合わさる)。それをするシステム。
    static const ComponentInfo kSpin{.type = &reflect::TypeOf<Spin>, .multiple = true, .folder = "3D"};
    registry.Contribute(kComponentPoint, kSpin);
    static const game_production::SystemInfo kSpinSystem{"回り続ける", game_production::SystemPhase::Frame, 0, &RunSpin};
    registry.Contribute(game_production::kSystemPoint, kSpinSystem);
    // カメラを順に切り替える: ゲームが動く間ゲームのカメラを切り替えるシステム (SystemContext::camera)。
    static const ComponentInfo kCycle{.type = &reflect::TypeOf<CameraCycle>, .folder = "3D"};
    registry.Contribute(kComponentPoint, kCycle);
    static const game_production::SystemInfo kCycleSystem{"カメラを順に切り替える", game_production::SystemPhase::Frame, 0, &RunCameraCycle};
    registry.Contribute(game_production::kSystemPoint, kCycleSystem);
    // シーンの 空 (シーンの設定: 3D のゲームで出る)。と、地球の空の 時刻 を進めるシステム。
    static const game_production::SceneSettingInfo kSky{.type = &reflect::TypeOf<Sky>, .space = "3d"};
    registry.Contribute(game_production::kSceneSettingPoint, kSky);
    static const game_production::SystemInfo kSkyClock{"空の時刻", game_production::SystemPhase::Frame, 0, &RunSkyClock};
    registry.Contribute(game_production::kSystemPoint, kSkyClock);
    // スクリプト: 3D の Transform (sdk/RM3D.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptTransform3DPart());
    registry.Contribute(game_production::kScriptApiPoint, ScriptTransform3DPart2());
    // ... とデバッグ描画 (rm::Debug: Play 中にエディターが見せる線。ゲームでは何も出ない)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptDebug3DPart());
    // ... と空 (rm::Sky: 天気と風。すぐに、またはなめらかに変える)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptSky3DPart());
    // エンティティの位置 (3D のゲームの音は、そこから聞こえる: PlayServices.h)。
    static const game_production::PlaceInfo kPlace{"3d", &Pose3D, &KeepWorld3D};
    registry.Contribute(game_production::kPlacePoint, kPlace);
}

}  // namespace rm::game_x_3d
