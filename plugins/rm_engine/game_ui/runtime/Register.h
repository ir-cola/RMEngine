#pragma once
// ゲーム UI のランタイムの部分: 書き出したゲームにも要るもの (plugins/RuntimePlugins.cpp に並べる。
// エディターのローダーは RegisterEditor の前にこれを呼ぶ) - UI の部品の型、ゲームが動くあいだ UI を
// 動かすサービス、スクリプトの部分。画面への描画は UiScreenDraw.h のもの (ゲーム用はそこで登録し、
// ゲームのビュー用はエディターの部分が登録する)。

#include "core/RuntimeRegistry.h"

namespace rm::game_ui {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_ui
