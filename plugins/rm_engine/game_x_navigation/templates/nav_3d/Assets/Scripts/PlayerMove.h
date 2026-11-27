// 見本のスクリプト: 矢印か WASD で歩く。歩いた先をナビメッシュの上の一番近い点に寄せるので、壁を抜けない。
// A sample script: walk with the arrow keys or WASD. Each step snaps to the closest point on the navmesh, so it
// doesn't go through walls.
#pragma once

#include "RM.h"

RM_SCRIPT(Label="矢印で歩く (ナビメッシュの上)")
struct PlayerMove {
    RM_PROPERTY(Edit, Label="速さ", Tooltip="1 秒に進む距離", Min=0) float speed = 5;

    void Tick(rm::Entity self, float dt);
};
