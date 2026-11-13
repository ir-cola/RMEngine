#pragma once
// 物理's editor part: the colliders' and characters' shapes drawn as lines over the scene in the
// scene views (3D and 2D; their toolbar's 形 switches between the selected things and all). The
// components, the system and the scripts' part are the runtime part's (runtime/Register.cpp).

#include "editor/plugin/Software.h"

namespace rm::physics {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::physics
