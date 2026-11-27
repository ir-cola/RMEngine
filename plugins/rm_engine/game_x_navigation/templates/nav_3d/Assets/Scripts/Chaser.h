// 見本のスクリプト: タグ "Player" の物を追いかける (部品「ナビの動く者」が壁を回って歩く)。
// A sample script: chase the entity tagged "Player" (the NavAgent component walks around the walls).
#pragma once

#include "RM.h"

RM_SCRIPT(Label="プレイヤーを追いかける")
struct Chaser {
    RM_PROPERTY(Edit, Label="行き先を変える間 (秒)", Tooltip="この間ごとに、プレイヤーの今いる所を行き先にします", Min=0.05) float interval = 0.5f;
    RM_TRANSIENT() float timer = 0;
    RM_TRANSIENT() bool told = false;

    void BeginPlay(rm::Entity self);
    void Tick(rm::Entity self, float dt);
    void OnEvent(rm::Entity self, const rm::Event& e);
};
