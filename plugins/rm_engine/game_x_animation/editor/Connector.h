#pragma once
// ゲーム制作 × アニメーション のエディターの部分 (plugins/BuiltinPlugins.cpp に並べる): アニメーターの
// クリップの欄を、同じエンティティのモデルのクリップから選ぶドロップダウンにする (ComponentLook::choices) と、
// 止まっているシーンビューの上のプレビューの操作 (再生 / 一時停止・時間のつまみ: ゲーム制作 × 3D の口
// rm_engine.game_x_3d.scene_overlay)。

#include "editor/plugin/Software.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <string>
#include <vector>

namespace rm::game_x_animation {

void RegisterEditor(editor::PluginRegistry& registry);
// エンティティ `entity` の「メッシュ (3D モデル)」のモデルのクリップの名前 (読めない・無い: 空)。アニメーション タブが使う。
std::vector<std::string> ModelClipNames(editor::SoftwareHost& host, const game_production::Scene& scene, int entity);
// 同じモデルの骨の名前 (親が先)。レイヤーのマスクを選ぶ。
std::vector<std::string> ModelBoneNames(editor::SoftwareHost& host, const game_production::Scene& scene, int entity);

}  // namespace rm::game_x_animation
