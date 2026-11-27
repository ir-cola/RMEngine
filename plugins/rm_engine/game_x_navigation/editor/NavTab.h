#pragma once
// 「ナビ」タブ (ゲームのソフトのパネル: ゲーム制作の ViewPanelInfo)。設計 docs/NAVIGATION.md 6-1 (見本 A: 2026-11-26 ユーザー決定)。
//
//   ┌ ナビ ─────────────────────────────────────────── [全体を見る] ┐
//   │ ┌ 地図 (真上から) ─────────────────┐ ┌ 範囲 (ナビメッシュの一覧と状態) ┐ │
//   │ │ [選ぶ]       歩ける面 (エリアの色)     │ │ [焼く] [全部を焼く] 進み具合 [やめる]│ │
//   │ │ [経路を試す]  縁・範囲の箱・エリア     │ │ エリアの種類 (＋ 足す / 消す)        │ │
//   │ │   ●始め ───╮                       │ │ 範囲の欄・型・選んだエリアの値        │ │
//   │ │            ╰── ●終わり  18.4 m      │ │                                    │ │
//   │ └────────────────────────────┘ └──────────────────────┘ │
//
// 地図の絵は、3D 空間のシーンビュー (口 kSceneViewPoint の空間 "3d") を 1 つ作り、真上から描かせる (気候のタブと同じ)。
// 焼くのは作業スレッド (editor::Worker): 進み具合とやめる。焼くこと自体は Undo に積まない (ファイルを作るだけ)。
// 初めて焼いて部品の「焼いた物」が空から埋まるときだけ、その欄の変更を Undo に積む。型とエリアの編集は Undo に積む。
// 見え方 (どこを見ているか・道具・選んだ範囲) はユーザーのもの: パネルの配置と一緒に保存し、プロジェクトには入れない。

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_x_navigation {

// ナビのタブの見え方 (UserSettings に残す。インスペクターには出さない)。
RM_STRUCT()
struct NavTabState {
    RM_PROPERTY() int tool = 0;     // 0: 選ぶ / 1: 経路を試す
    RM_PROPERTY() int volume = 0;   // 選んだ範囲 (部品「ナビメッシュ」のエンティティ)
    RM_PROPERTY() int area = 0;     // 選んだエリアの種類 (NavAreaKind::id)
    RM_PROPERTY() int type = 0;     // 選んだエージェントの型 (NavAgentType::id。0: 選んだ範囲の型)
    RM_PROPERTY() Vec2 center{0, 0};  // 地図の真ん中 (ワールドの x, z)
    RM_PROPERTY() float halfWidth = 0;  // 地図の幅の半分 (m)。0: まだ (最初にシーン全体を見る)
    RM_PROPERTY() bool fill = true;     // シーンビューで焼いた面を塗る
};

std::unique_ptr<game_production::SceneView> CreateNavTab();

// 新しいエンティティ「ナビメッシュ」/「ナビのエリア」/「ナビの動く者」/「つなぎの線」/「ナビの障害物」(Transform つき)。追加 ▸ ナビ と、タブの「ナビメッシュを置く」。
int MakeVolume(game_production::Scene& scene, int parent, size_t index);
int MakeArea(game_production::Scene& scene, int parent, size_t index);
int MakeAgent(game_production::Scene& scene, int parent, size_t index);
int MakeLink(game_production::Scene& scene, int parent, size_t index);
int MakeObstacle(game_production::Scene& scene, int parent, size_t index);

}  // namespace rm::game_x_navigation
