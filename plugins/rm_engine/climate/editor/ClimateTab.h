#pragma once
// 気候のタブ = 気候ビュー (ゲームのソフトのパネル。シーンとゲームの隣: ゲーム制作の ViewPanelInfo)。気候を、
// シーンを真上から見た地図に描いて決め、気温・湿り気・雲を色で見る。同じタブで天候も変える。
//
//   ┌ [ 気候 | 天候 ]  見る [塗り | 気温 | 湿り気 | 雲]                    [全体を見る] ┐
//   │ ┌ 地図: シーンを真上から見た絵 ────────────┐ ┌ 筆 | 消しゴム | ペン | 選ぶ ┐ │
//   │ │  塗った所は種類の色 (雲の層: 雲の色) で      │ │ 層  [ 気候 | 雲 ]           │ │
//   │ │  ペンで囲んだ形の縁・気候エリアの縁         │ │ 筆の半径 (m)  [ 8 ]         │ │
//   │ │  見る = 気温 / 湿り気 / 雲: 値の色と目盛り   │ │ マスの大きさ 1 2 4 8 16     │ │
//   │ │  カーソルの所の値                          │ │ 気候の種類 (雲: 雲の量)      │ │
//   │ └────────────────────────────┘ │ 選んだ形・選んだ種類の値 …   │ │
//
// 地図の絵は、3D 空間のシーンビュー (口 kSceneViewPoint の空間 "3d") を自分で 1 つ作り、
// SceneViewHost::overhead で描かせる (気候はゲーム制作 × 3D を知らない)。塗ったものはシーンの設定
// 「気候の地図」(ClimateMap)。1 回のドラッグ・1 つの形が Undo の 1 件。「天候」の側はシーンの設定を
// ClimatePages.h の口から並べる (天候 × 気候 が天候を差し出す)。設計: docs/CLIMATE.md の「気候ビュー」。
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
    RM_PROPERTY() int mode = 0;      // 0: 気候 (描く) / 1: 天候
    RM_PROPERTY() int tool = 0;      // 0: 筆 / 1: 消しゴム / 2: ペン (囲む) / 3: 選ぶ (形を直す)
    RM_PROPERTY() int layer = 0;     // 描く層: 0 気候 / 1 雲 (MapLayer)
    RM_PROPERTY() int view = 0;      // 見る: 0 塗り / 1 気温 / 2 湿り気 / 3 雲
    RM_PROPERTY(Min=0.1, Max=10000) float brush = 8.f;  // 筆の半径 (m)
    RM_PROPERTY() int kind = 1;      // 塗る気候の種類 (ClimateKind::id)
    RM_PROPERTY(Min=0, Max=100) float clouds = 80.f;  // 塗る雲の量 (%)
    RM_PROPERTY() Vec2 center{0, 0};  // 地図の真ん中 (ワールドの x, z)
    RM_PROPERTY() float halfWidth = 0;  // 地図の幅の半分 (m)。0: まだ (最初にシーン全体を見る)
};

std::unique_ptr<game_production::SceneView> CreateClimateTab();

}  // namespace rm::climate
