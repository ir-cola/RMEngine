#pragma once
// スクリプト API のナビゲーションの部分 (sdk/RMNav.h): 動く者の行き先・止まる・状態、道だけを探す・一番近い点・
// でたらめな点・エリアの費用と通れる / 通れない。再生しているシーンのナビの世界 (NavWorld.h) に頼む。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_x_navigation {

const game_production::ScriptApiPart& ScriptNavPart();   // 版 1
const game_production::ScriptApiPart& ScriptNavPart2();  // 版 2 (つなぎの線と扉。RMNav.h の今の物)

}  // namespace rm::game_x_navigation
