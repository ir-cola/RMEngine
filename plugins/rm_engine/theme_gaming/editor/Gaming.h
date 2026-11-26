#pragma once
// ゲーミング テーマ (単体のパック。暗いテーマ・紫のアクセント)。色はテーマファイル (themes/gaming.json)。
// 絵はこのコードで、名前 "gaming" で w::kOrnamentPoint の所で核に渡す (止まった絵):
//   ウィンドウの上の端と下の端に虹色の細い帯、下のほうにネオンの格子 (遠近。地平線は水色に光る)、
//   ドットのかけら、左下にゲームのコントローラー。各パネルの下の縁にも虹色の細い線 (光る縁)。
// 専用ウィンドウのパネルは軽い絵になる: 上の虹色の帯と、薄い格子。
// 形と虹の色は Shapes.h (純粋、テストあり)。

#include "editor/plugin/Software.h"

namespace rm::theme_gaming {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::theme_gaming
