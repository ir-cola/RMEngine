#pragma once
// 気候のタブ = 気候ビュー (ゲームのソフトのパネル。シーンとゲームの隣: ゲーム制作の ViewPanelInfo)。気候を、
// シーンを真上から見た地図に描いて決め、気温・湿り気・雲・雨を色で見る。同じタブで天候 (雲と雨雲) も描く。
//
//   ┌ [ 気候 | 天候 ]  見る [塗り | 気温 | 湿り気 | 雲 | 雨]               [全体を見る] ┐
//   │ ┌ 地図: シーンを真上から見た絵 ────────────┐ ┌ 気候: 筆の半径・マスの大きさ ┐ │
//   │ │ [筆]       塗った所は種類の色 (天候: 雲 /   │ │       気候の種類・選んだ形 … │ │
//   │ │ [消しゴム]   雨雲の色) で                  │ │ 天候: 層 [ 雲 | 雨雲 ]       │ │
//   │ │ [ペン]     ペンで囲んだ形の縁・エリアの縁  │ │       雲の量 / 雨の濃さ・ぼかし│ │
//   │ │ [選ぶ]     見る = 気温 …: 値の色と目盛り   │ │       シーン全体の天気 (降水 │ │
//   │ │            カーソルの所の値               │ │       量など: インスペクター) │ │
//   │ └────────────────────────────┘ └──────────────┘ │
//   左上の道具はシーンビューの左の列と同じ正方形のボタン (w::ToolSquare)。気候と天候で同じ道具。
//
// 地図の絵は、3D 空間のシーンビュー (口 kSceneViewPoint の空間 "3d") を自分で 1 つ作り、
// SceneViewHost::overhead で描かせる (気候はゲーム制作 × 3D を知らない)。塗ったものはシーンの設定
// 「気候の地図」(ClimateMap)。1 回のドラッグ・1 つの形が Undo の 1 件。「天候」の側は地図の雲と雨雲の層を
// 描き、シーン全体の天気をシーンの設定として ClimatePages.h の口から並べる (天候 × 気候 が天候を差し出す)。
// 雨はシーン全体 (天候の降水量) と、一部だけ (描いた雨雲) の 2 通り。設計: docs/CLIMATE.md の「気候ビュー」。
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
    RM_PROPERTY() int tool = 0;      // 0: 筆 / 1: 消しゴム / 2: ペン (囲む) / 3: 選ぶ (形を直す)。気候と天候で同じ
    RM_PROPERTY() int weatherLayer = 1;  // 天候の側で描く層: 0 雲 / 1 雨雲 (気候の側は気候の層)
    RM_PROPERTY() int view = 0;      // 見る: 0 塗り / 1 気温 / 2 湿り気 / 3 雲 / 4 雨
    RM_PROPERTY(Min=0.1, Max=10000) float brush = 8.f;  // 筆の半径 (m)
    RM_PROPERTY() int kind = 1;      // 塗る気候の種類 (ClimateKind::id)
    RM_PROPERTY(Min=0, Max=100) float clouds = 80.f;  // 塗る雲の量 (%)
    RM_PROPERTY(Min=0, Max=100) float rain = 60.f;    // 塗る雨雲の濃さ (降る量 %)
    RM_PROPERTY(Min=0, Max=100) float soft = 100.f;   // ペンで囲む雨雲のぼかし (%)
    RM_PROPERTY() Vec2 center{0, 0};  // 地図の真ん中 (ワールドの x, z)
    RM_PROPERTY() float halfWidth = 0;  // 地図の幅の半分 (m)。0: まだ (最初にシーン全体を見る)
};

std::unique_ptr<game_production::SceneView> CreateClimateTab();

}  // namespace rm::climate
