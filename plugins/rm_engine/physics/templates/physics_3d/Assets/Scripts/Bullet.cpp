#include "Bullet.h"

void Bullet::Tick(rm::Entity self, float dt) {
    time += dt;
    if (time >= life) self.Destroy();
}

void Bullet::OnHit(rm::Entity self, rm::Entity other) {
    rm::Log::Info("弾が「" + other.Name() + "」に当たって消えました");
    self.Destroy();
}
