// 見本のスクリプト: 文字 (UI) に前回のスコア (グローバル変数 score) を出す。
// A sample script: shows the last score (the global variable score) in its own text.
#pragma once

#include "RM.h"

RM_SCRIPT(Label="前回のスコアを出す")
struct ShowScore {
    void BeginPlay(rm::Entity self);
};
