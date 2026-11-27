#include "PlayerMove.h"

#include "RM3D.h"
#include "RMNav.h"

void PlayerMove::Tick(rm::Entity self, float dt) {
    rm::Vec3 dir{0, 0, 0};
    if (rm::Input::Down(rm::Key::Left) || rm::Input::Down(rm::Key::A)) dir.x -= 1;
    if (rm::Input::Down(rm::Key::Right) || rm::Input::Down(rm::Key::D)) dir.x += 1;
    if (rm::Input::Down(rm::Key::Up) || rm::Input::Down(rm::Key::W)) dir.z += 1;
    if (rm::Input::Down(rm::Key::Down) || rm::Input::Down(rm::Key::S)) dir.z -= 1;
    if (dir.x == 0 && dir.z == 0) return;
    const rm::Transform3D t(self);
    const rm::Vec3 next = t.WorldPosition() + rm::Normalize(dir) * (speed * dt);
    t.SetWorldPosition(rm::Nav::ClosestPoint(next));  // (ナビメッシュの外へは出ない)
}
