#pragma once
// The 3D debug drawing part of the scripts' API (sdk/RM3D.h: rm::Debug::Line / Box / Sphere / Ray).
// Shapes become lines in game production's DebugDraw (ScriptWorld::debug); with no buffer - an
// exported game - every call returns at once. Offered at kScriptApiPoint by RegisterRuntime.

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_3d {

const game_production::ScriptApiPart& ScriptDebug3DPart();

// A ball's three circles (round X, Y and Z) are made of this many lines each.
inline constexpr int kDebugSphereSegments = 24;

}  // namespace rm::game_x_3d
