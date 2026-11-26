#pragma once
// 探検テーマパック: 宇宙 / 深海 / 火山 (どれも暗いテーマ)。色はテーマファイル (themes/*.json)。絵はこのコードで、
// テーマファイルが使う名前で w::kOrnamentPoint の所で核に渡す (季節テーマパックと同じ作り。止まった絵)。
//
//   space     星 (小さな点と、きらめく星)、星雲 (左上)、輪のある惑星 (右上)、流れ星、左下にロケット
//   deep_sea  上から差しこむ光の筋、マリンスノー、泡 (右)、魚の群れ、光る粒、海の底と海藻 (右下)、左下にクラゲ
//   volcano   下の端の溶岩の照り、右下の火山 (火口の照り・煙・流れ下る溶岩)、舞い上がる火の粉、左下に炎
// 専用ウィンドウのパネルは軽い絵になる: 宇宙は星、深海は泡とマリンスノー、火山は火の粉。
// 形は Shapes.h (純粋、テストあり)。

#include "editor/plugin/Software.h"

namespace rm::theme_explore {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::theme_explore
