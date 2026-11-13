// 見本のスクリプト: 弾。何かに当たったら消える (OnHit)。何にも当たらなければ「消えるまで」の秒数で消える。
// A sample script: a bullet. It goes when it hits something (OnHit), or after "消えるまで" seconds.
#pragma once

#include "RM.h"

RM_SCRIPT(Label="弾")
struct Bullet {
    RM_PROPERTY(Edit, Label="消えるまで (秒)", Tooltip="何にも当たらなかったときに消えるまでの秒数", Min=0) float life = 3;
    RM_TRANSIENT() float time = 0;

    void Tick(rm::Entity self, float dt);
    void OnHit(rm::Entity self, rm::Entity other);
};
