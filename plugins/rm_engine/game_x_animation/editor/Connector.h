#pragma once
// ゲーム制作 × アニメーション のエディターの部分 (plugins/BuiltinPlugins.cpp に並べる): アニメーターの
// クリップの欄を、同じエンティティのモデルのクリップから選ぶドロップダウンにする (ComponentLook::choices) と、
// 止まっているシーンビューの上のプレビューの操作 (再生 / 一時停止・時間のつまみ: ゲーム制作 × 3D の口
// rm_engine.game_x_3d.scene_overlay)。

#include "editor/plugin/Software.h"

namespace rm::game_x_animation {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_animation
