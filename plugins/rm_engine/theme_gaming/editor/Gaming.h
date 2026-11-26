#pragma once
// ゲーミング テーマ (単体のパック。暗いテーマ・紫のアクセント)。色はテーマファイル (themes/gaming.json)。
// 絵はこのコードで、名前 "gaming" で w::kOrnamentPoint の所で核に渡す。動く絵 (2026-11-26。約 30 fps。
// editor/widgets/MovingBackdrop.h: GPU のシェーダーで光を描く):
//   紫の空に縞のある太陽 (縞がゆっくり流れる) と、縁がネオンで光る遠い山、手前へ流れ続けるネオンの格子の床
//   (光がにじむ。色が奥から手前へ移る)、色がゆっくり変わる地平線の照り、漂う色とりどりの光の粒、
//   ウィンドウの縁をぐるりと流れる虹。各パネルの縁にも、同じ時計で流れる虹の線 (内へ光がにじむ)。
// 専用ウィンドウのパネルは軽い絵になる (太陽と山を出さない)。
// 絵の中身は Scenes.h (シェーダーと粒)、動きと虹の色は Motion.h (純粋、テストあり)。

#include "editor/plugin/Software.h"

namespace rm::theme_gaming {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::theme_gaming
