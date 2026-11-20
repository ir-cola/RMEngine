#include "rm_engine/game_x_2d/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/ScriptDebug2D.h"
#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"
#include "rm_engine/game_x_2d/runtime/Spin2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

namespace rm::game_x_2d {

void RegisterRuntime(RuntimeRegistry& registry) {
    using game_production::ComponentInfo;
    using game_production::kComponentPoint;
    // 2D のゲームのエンティティは平面上に場所を持つ: ゲームの空間が 2D のあいだ、新しいエンティティには
    // これが付く (3D のあいだは 3D の Transform)。置いた Prefab の場所もこれ。
    static const ComponentInfo kTransform{.type = &reflect::TypeOf<space2d::Transform2D>, .onNewEntities = true, .placement = true, .space = "2d", .folder = "2D"};
    registry.Contribute(kComponentPoint, kTransform);
    // ... 絵 (スプライト) として描ける (手で足す)。画像のセル (アトラス) を順に切り替えられる。
    static const ComponentInfo kSprite{.type = &reflect::TypeOf<Sprite>, .folder = "2D"};
    registry.Contribute(kComponentPoint, kSprite);
    static const ComponentInfo kAnimation{.type = &reflect::TypeOf<SpriteAnimation>, .folder = "2D"};
    registry.Contribute(kComponentPoint, kAnimation);
    static const game_production::SystemInfo kAnimationSystem{"スプライトのアニメーション", game_production::SystemPhase::Frame, 0,
                                                              &RunSpriteAnimation};
    registry.Contribute(game_production::kSystemPoint, kAnimationSystem);
    // ... カメラ: ゲームが見るもの (ゲーム設定の 最初のカメラ がひとつを指す)。
    static const ComponentInfo kCamera{.type = &reflect::TypeOf<Camera2D>, .camera = true, .folder = "2D"};
    registry.Contribute(kComponentPoint, kCamera);
    // 回り続ける (2D): ゲームが動く間エンティティを回す (複数あれば合計される)。それを動かすシステム。
    static const ComponentInfo kSpin{.type = &reflect::TypeOf<Spin2D>, .multiple = true, .folder = "2D"};
    registry.Contribute(kComponentPoint, kSpin);
    static const game_production::SystemInfo kSpinSystem{"回り続ける (2D)", game_production::SystemPhase::Frame, 0, &RunSpin2D};
    registry.Contribute(game_production::kSystemPoint, kSpinSystem);
    // スクリプト: Transform 2D (sdk/RM2D.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptTransform2DPart());
    registry.Contribute(game_production::kScriptApiPoint, ScriptTransform2DPart2());
    registry.Contribute(game_production::kScriptApiPoint, ScriptSpriteAnimationPart());
    // ... とデバッグ描画 (rm::Debug: Play 中にエディターが出す線。ゲームでは何もしない)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptDebug2DPart());
    // エンティティの場所 (2D のゲームの音はそこから聞こえる: PlayServices.h)。
    static const game_production::PlaceInfo kPlace{"2d", &Pose2D, &KeepWorld2D};
    registry.Contribute(game_production::kPlacePoint, kPlace);
}

}  // namespace rm::game_x_2d
