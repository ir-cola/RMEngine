#pragma once
// 気候のタブ (ゲームのソフトのパネル。シーンとゲームの隣: ゲーム制作の ViewPanelInfo)。気候を、シーンを
// 真上から見た地図にペンで塗って決める。同じタブで天候も変える。
//
//   ┌ [ 気候 | 天候 ]                                             [全体を見る] ┐
//   │ ┌ 地図: シーンを真上から見た絵 ────────────┐ ┌ ペン | 消しゴム ─────┐ │
//   │ │  塗った所は種類の色で半透明に              │ │ 筆の半径 (m)  [ 8 ]   │ │
//   │ │  気候エリアの縁も線で                      │ │ マスの大きさ 1 2 4 8 16│ │
//   │ │  左ドラッグ: 塗る / ホイール: 拡大縮小      │ │ 気候の種類   [＋][消す]│ │
//   │ │  右・中ドラッグ: 動かす                    │ │ ■ 温帯  15 ℃ ・ 50 %  │ │
//   │ └────────────────────────────┘ │ 選んだ種類の値 …       │ │
//
// 地図の絵は、3D 空間のシーンビュー (口 kSceneViewPoint の空間 "3d") を自分で 1 つ作り、
// SceneViewHost::overhead で描かせる (気候はゲーム制作 × 3D を知らない)。塗ったものはシーンの設定
// 「気候の地図」(ClimateMap)。1 回のドラッグが Undo の 1 件。「天候」の側はシーンの設定を
// ClimatePages.h の口から並べる (天候 × 気候 が天候を差し出す)。
// 段階 3 (docs/CLIMATE.md): エリアごとの天気を、この地図に塗れるようにする (まだ作らない)。
//
// 見え方 (どこを見ているか・道具・筆の大きさ) はユーザーのもの: パネルの配置と一緒に保存し、
// プロジェクトには入れない (ClimateTabState)。

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::climate {

// 気候のタブの見え方 (UserSettings に残す。インスペクターには出さない)。
RM_STRUCT()
struct ClimateTabState {
    RM_PROPERTY() int mode = 0;      // 0: 気候 (塗る) / 1: 天候
    RM_PROPERTY() bool erase = false;  // 消しゴム
    RM_PROPERTY(Min=0.1, Max=10000) float brush = 8.f;  // 筆の半径 (m)
    RM_PROPERTY() int kind = 1;      // 塗る気候の種類 (ClimateKind::id)
    RM_PROPERTY() Vec2 center{0, 0};  // 地図の真ん中 (ワールドの x, z)
    RM_PROPERTY() float halfWidth = 0;  // 地図の幅の半分 (m)。0: まだ (最初にシーン全体を見る)
};

std::unique_ptr<game_production::SceneView> CreateClimateTab();

}  // namespace rm::climate
