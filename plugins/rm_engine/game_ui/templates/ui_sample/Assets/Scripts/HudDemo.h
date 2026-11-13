// 見本のスクリプト: スコアが増えて HP が減っていく。HP が 0 になるとタイトルへ戻る。
// スコアと HP はグローバル変数 (ゲームの設定の「グローバル変数」: score / hp) なので、シーンを切り替えても残ります。
// A sample script: the score goes up and the HP down; at 0 HP back to the title. score / hp are global
// variables, so they stay when the scene is switched.
#pragma once

#include "RM.h"

RM_SCRIPT(Label="スコアと HP の見本")
struct HudDemo {
    RM_PROPERTY(Edit, Label="スコアの文字", Tooltip="文字 (UI) のエンティティ") rm::EntityRef scoreText;
    RM_PROPERTY(Edit, Label="HP のバー", Tooltip="バー (UI) のエンティティ") rm::EntityRef hpBar;
    RM_PROPERTY(Edit, Label="1 秒に増えるスコア", Min=0) int scorePerSecond = 10;
    RM_PROPERTY(Edit, Label="1 秒に減る HP", Tooltip="HP は 0〜1", Min=0) float hpPerSecond = 0.1f;
    RM_TRANSIENT() float carry = 0;

    void BeginPlay(rm::Entity self);
    void Tick(rm::Entity self, float dt);
};
