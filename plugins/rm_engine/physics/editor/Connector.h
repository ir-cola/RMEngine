#pragma once
// 物理 のエディター部分: コライダーとキャラクターの形を、シーンビュー (3D と 2D) のシーンの上に線で
// 描く (そのツールバーの 形 で、選んだものとすべてを切り替える)。部品、システム、スクリプト向けの部分は
// ランタイム部分 (runtime/Register.cpp) のもの。

#include "editor/plugin/Software.h"

namespace rm::physics {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::physics
