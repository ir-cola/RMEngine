#pragma once
// 季節テーマパック: 4 つの季節 (スプリング / サマー / フォール / ウィンター)。色はテーマファイル
// (themes/*.json、どのパックのものも核が読む)。絵はこのコードで、テーマファイルが使う名前
// ("spring" ...) で w::kOrnamentPoint の所で核に渡す。核はそれらを内容の後ろ (ウィンドウの背景と、
// 各パネルとバーの上) に描く。
//
//   spring  右に生える桜の木 (上のほうで満開、花びらが吹き飛ぶ)、花 (左下)、
//           花びらの吹雪 (左上)
//   summer  砂の小山にパラソルと砂のお城 (右下)、雲 (右上)、太陽 (左下)
//   fall    上に沿って濃く下に散らばる葉、大きなもみじの葉 (左下)
//   winter  降る雪 (止めたもの)、上の端からのつらら、すべてのパネルの下に積もる雪、
//           雪の結晶 (左下)
// 専用ウィンドウのパネルは軽い絵になる: 春は花びら、夏は雲、冬は雪。

#include "editor/plugin/Software.h"

namespace rm::theme_seasons {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::theme_seasons
