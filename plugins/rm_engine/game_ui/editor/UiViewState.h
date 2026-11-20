#pragma once
// UI ビューの見方: 目安の入り切り。パネルの配置の隣に保存する
// (UserSettings/Layout.json: ゲームの "viewPanels" -> "ui")。プロジェクトには入れない。

#include "base/Math.h"
#include "core/reflect/Reflect.h"

namespace rm::game_ui {

RM_STRUCT()
struct UiViewState {
    RM_PROPERTY() bool safeArea = true;  // セーフエリアの目安
    RM_PROPERTY() bool anchors = true;   // 選んだもののアンカー
    RM_PROPERTY(Min=0.1, Max=10) float zoom = 1;  // 1: ゲームの画面がパネルに収まる
    RM_PROPERTY() Vec2 pan{0, 0};                  // 画面を真ん中から動かした DIP
    RM_PROPERTY() bool snapGuides = true;          // ほかのものの辺と真ん中へ (補助線)
    RM_PROPERTY() bool snapGrid = false;           // 格子へ (格子)
    RM_PROPERTY(Min=1, Max=1000) float gridStep = 10;  // 格子の間隔 (キャンバスの単位)
    RM_PROPERTY() bool otherCanvases = true;       // 作っているキャンバス: ほかの (切っていない) キャンバスも見せる
};

}  // namespace rm::game_ui
