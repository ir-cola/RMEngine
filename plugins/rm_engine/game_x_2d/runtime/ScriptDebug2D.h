#pragma once
// The 2D debug drawing part of the scripts' API (sdk/RM2D.h: rm::Debug::Line / Rect / Circle). Shapes
// become lines (z = 0) in game production's DebugDraw (ScriptWorld::debug); with no buffer - an
// exported game - every call returns at once. Offered at kScriptApiPoint by RegisterRuntime.

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_2d {

const game_production::ScriptApiPart& ScriptDebug2DPart();

// A circle is made of this many lines.
inline constexpr int kDebugCircleSegments = 32;

}  // namespace rm::game_x_2d
