#pragma once
// UI ビュー (ゲームソフトのパネル: ゲーム制作の ViewPanelInfo): ゲーム設定の 画面 としてのゲームの
// 画面と、その上にゲームが描くとおりに描いた UI (同じ配置、同じ描き方)、それを作る道具:
//
//   左クリック          マウスの下の UI のもの (一番手前) を選ぶ
//   もののドラッグ      動かす (その 位置)
//   角の四角            大きさを変える (反対側の角は動かない)
//   ドラッグ中の Esc    元の場所へ戻す
//
// 選んだものには、そのアンカー (親の中で固定される所を示す小さな印) が、キャンバスには縁が出る。
// ゲーム設定のセーフエリアは目安になる (帯の セーフエリア)。ドラッグ 1 回につき Undo は 1 件。
// 切ってあるキャンバスも、その中のものを選んでいるあいだは見せる (作っているウィンドウ)。

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_ui {

std::unique_ptr<game_production::SceneView> CreateUiView();

}  // namespace rm::game_ui
