// 見本のスクリプト: Space で弾 (プレハブ) を前 (-Z) に撃つ。弾にはコライダーと剛体が付いていて、
// 何かに当たると Bullet の OnHit で消えます。
// A sample script: Space shoots a bullet (a prefab) forward (-Z). The bullet has a collider and a
// rigidbody; when it hits something, Bullet's OnHit destroys it.
#pragma once

#include "RM.h"

RM_SCRIPT(Label="弾を撃つ")
struct Shooter {
    RM_PROPERTY(Edit, Label="弾", Tooltip="撃つ弾のプレハブ (コライダーと剛体の付いたもの)") rm::PrefabRef bullet;
    RM_PROPERTY(Edit, Label="弾の速さ", Tooltip="1 秒に進む距離", Min=0) float speed = 15;

    void BeginPlay(rm::Entity self);
    void Tick(rm::Entity self, float dt);
};
