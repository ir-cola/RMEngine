#pragma once
// The runtime part of 物理: the components (剛体, コライダー, コライダー (2D), キャラクター), the system that
// steps the physics (fixed steps) and the scripts' part (RMPhysics.h). Listed in plugins/RuntimePlugins.cpp;
// an exported game that uses it links it by RM_RUNTIME_ENTRY (Register.cpp).

#include "core/RuntimeRegistry.h"

namespace rm::physics {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::physics
