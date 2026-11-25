#pragma once
// ゲーム制作 × 3D - ほかのプラグインが 3D のシーンビューの絵の上に置く小さな操作 (エディターだけ)。
// 止まっているシーンビュー (Play でもゲームビューでもないとき) だけに出る: アニメーターのプレビュー
// (再生 / 一時停止・時間のつまみ)。ゲーム制作 × 3D は中身を知らない: 場所を尋ね、そこへのクリックを
// シーンに渡さず (選択が外れない)、ギズモとカメラのプレビューの後に描かせるだけ。
//
//   // コネクターの editor/Connector.cpp で:
//   static const game_x_3d::SceneOverlayInfo kOverlay{&PreviewArea, &DrawPreviewBar};
//   registry.Contribute(game_x_3d::kSceneOverlayPoint, kOverlay);

#include "rm_engine/game_production/editor/SceneView.h"
#include "ui/Context.h"

#include <optional>

namespace rm::game_x_3d {

// PluginRegistry の口: 中身は SceneOverlayInfo (static)。
inline constexpr char kSceneOverlayPoint[] = "rm_engine.game_x_3d.scene_overlay";

struct SceneOverlayInfo {
    // シーンビューの絵 `view` の中の、今出す場所 (nullopt: 今は出さない)。そこへのマウスはシーンに届かない。
    std::optional<ui::Rect> (*area)(const ui::Rect& view, const game_production::SceneViewHost& host) = nullptr;
    // `at` (area が返したもの) に描く。w:: の部品で。
    void (*draw)(ui::Context& c, const ui::Rect& at, game_production::SceneViewHost& host) = nullptr;
};

}  // namespace rm::game_x_3d
