#pragma once
// 気候エリアの形の線 (エディター): シーンビューの枠 (Register.cpp) と、気候のタブの地図 (ClimateTab.cpp)
// が同じ線を描く (憲法 3)。

#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/game_production/runtime/DebugDraw.h"

namespace rm::climate {

// 3D の気候エリアの縁を `inset` m だけ内側に縮めた形の線 (3D の線。縮めきったら何も足さない)。
void ZoneOutline3D(const ClimateField::Zone3D& z, float inset, Color color, game_production::DebugDraw& out);

}  // namespace rm::climate
