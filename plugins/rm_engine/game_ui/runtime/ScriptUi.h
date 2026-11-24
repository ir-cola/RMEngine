#pragma once
// スクリプトの API (sdk/RMUI.h) のうち、ゲーム UI の部分: 動いているシーンの文字、画像、バー、ボタン、
// UI のウィンドウ。ゲーム制作の kScriptApiPoint で提供する。

#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::game_ui {

const game_production::ScriptApiPart& ScriptUiPart();
// バージョン 1 と同じ表 (その先頭): トゥイーンより前に作ったスクリプトも、これを見つけられる。
const game_production::ScriptApiPart& ScriptUiPartVersion1();
const game_production::ScriptApiPart& ScriptUiPartVersion2();

}  // namespace rm::game_ui
