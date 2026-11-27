// 見本のスクリプト: 始めの場所のまわりを、でたらめな所へ歩いては次へ (着いたら NavArrived が届く)。
// A sample script: wander to random walkable spots around the starting place (NavArrived arrives on each arrival).
#pragma once

#include "RM.h"

RM_SCRIPT(Label="うろうろ歩く")
struct Wanderer {
    RM_PROPERTY(Edit, Label="歩く広さ (m)", Tooltip="始めの場所からこの距離までの所へ歩きます", Min=0.5) float radius = 8;
    RM_TRANSIENT() rm::Vec3 home{0, 0, 0};
    RM_TRANSIENT() bool started = false;

    void Tick(rm::Entity self, float dt);
    void OnEvent(rm::Entity self, const rm::Event& e);
};
