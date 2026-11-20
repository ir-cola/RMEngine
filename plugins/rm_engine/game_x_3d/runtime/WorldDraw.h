#pragma once
// ゲーム制作 × 3D - 他のプラグインがゲームの 3D の世界に自分で描くもの (パーティクル, 天候)。
// シーンビュー・ゲームビュー (エディターの Play)・書き出したゲームが、それぞれ寄せられた物ごとに
// 描き手を 1 つ作り、1 フレームに 1 回、自分の画面に何を描くか尋ねる (space3d::ExtraDraw:
// 透ける部分のあとで、深度テストあり)。
//
//   // コネクターの runtime/Register.cpp で:
//   static const game_x_3d::WorldDrawInfo kDraw{&CreateParticleDraw};
//   registry.Contribute(game_x_3d::kWorldDrawPoint, kDraw);
//
// 描き手はフレームの間に必要なものを持ち続ける (画面ごとに 1 つ: シーンビューの画面とそのカメラの
// プレビューは、それぞれ別に持つ)。GPU のものは `gfx` の上に作る (gfx.Generation() が変わったら
// 作り直す)。

#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "platform/Gfx.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rm::game_x_3d {

struct SkyLook;

// RuntimeRegistry の口: 中身は WorldDrawInfo (static)。
inline constexpr char kWorldDrawPoint[] = "rm_engine.game_x_3d.world_draw";

struct WorldDrawFrame {
    const game_production::Scene& scene;
    platform::Gfx& gfx;
    // エンティティが世界のどこにあるか (親の Transform も含めた Transform)。null: Transform が無い
    // (または切ってある: 何も描かない)。
    std::function<const space3d::Mat4*(int entity)> world;
    float dt = 0;          // この画面の前のフレームからの秒数 (0: 進んでいない - 止まっている)
    bool playing = false;  // ゲームが動いている (エディターの Play、書き出したゲーム): 全部が動く
    // 動いていない (エディター): 選ばれているもの - 描き手が見やすいように動かして見せてよいもの。
    std::span<const int> selection;
    // アセットのバイト列 (エディター: そのファイル。ゲーム: 書き出しが入れたもの)。
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    // メッセージに使うアセットの名前 (エディター: ファイルの名前)。null: id を使う。
    std::function<std::string(const AssetRef&)> name;
    // プラグインが差し出しているもの (描き手は他のプラグインにもっと尋ねてよい: パーティクルならコライダーの形)。
    const RuntimeRegistry* registry = nullptr;
    // デバッグパネルに出す、この画面がどれか: "scene" / "game" / "preview" (null: 書き出したゲームのもの)。
    const char* picture = nullptr;
    // この画面が動く、ゲームの時間 (空のもの: エディターでは Play まで 0)。
    double seconds = 0;
    // この画面が見せているシーンの 空 (その光: 天候の雨粒はこれで照らされる)。null: 無い。
    const SkyLook* sky = nullptr;
    // 空と天候をこの画面に見せる (シーンビュー自身のカメラ: その 空 ボタン)。
    bool atmosphere = true;
    // この画面でエンティティを描くモデル (null: 無い - 印や箱)、とレンダラーの簡単な形 (箱, 球 ...)。
    std::function<const space3d::GpuMesh*(int entity)> model;
    std::function<const space3d::GpuMesh*(space3d::Shape)> shape;
};

// 描き手が自分でフレームに足すもの (WorldDrawer::PrepareFrame): 描き手自身の span への参照
// (次の PrepareFrame まで持つ)。使われるのは最初の描き手の 向きの地図 と面の触れ方だけ。
struct FrameAdditions {
    std::span<const Vec3> mapDirections;
    std::span<const space3d::MapShape> mapShapes;
    std::span<const uint8_t> mapMovingOnly;  // RenderFrame::mapMovingOnly
    const space3d::SurfaceTouch* surface = nullptr;
    std::span<const space3d::ExtraDraw> solids;  // RenderFrame::solids (天候: the snow lying)
    Color fog{0, 0, 0, 0};                       // RenderFrame::fog (天候: 土砂降り。a = 0: 無し)
};

class WorldDrawer {
public:
    virtual ~WorldDrawer() = default;
    // 1 フレームに 1 回、Prepare の前に: フレームに足すもの (天候: コライダーの 向きの地図、
    // 全部の面の 濡れ、積もった雪。RenderFrame::mapDirections / mapShapes / surface / solids)。
    virtual void PrepareFrame(const WorldDrawFrame& f, FrameAdditions& out) {
        (void)f;
        (void)out;
    }
    // 1 フレームに 1 回、画面を描く前に: そこに何を描くか (`out` に入れる)。
    virtual void Prepare(const WorldDrawFrame& f, std::vector<space3d::ExtraDraw>& out) = 0;
    // 自分で動くものがある: エディターは次のフレームにもビューを描き直す。
    virtual bool Animating() const { return false; }
};

struct WorldDrawInfo {
    std::unique_ptr<WorldDrawer> (*create)() = nullptr;
};

}  // namespace rm::game_x_3d
