#include "Shooter.h"

#include "RM3D.h"
#include "RMPhysics.h"

void Shooter::BeginPlay(rm::Entity self) {
    (void)self;
    rm::Log::Info("Space で弾を撃ちます");
}

void Shooter::Tick(rm::Entity self, float dt) {
    (void)dt;
    if (!rm::Input::Pressed(rm::Key::Space)) return;
    const rm::Entity b = rm::Spawn(bullet);
    if (!b.Valid()) {
        rm::Log::Warn("「弾」にプレハブが選ばれていません");
        return;
    }
    // 弾が始まる位置と、そのあとの速さ (弾はすぐ体を持つ: SetVelocity がそれを作る)。
    rm::Transform3D(b).SetPosition(rm::Transform3D(self).WorldPosition());
    rm::Physics::SetVelocity(b, {0, 0, -speed});
}
