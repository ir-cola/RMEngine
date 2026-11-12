#include "ArrowMover.h"

#include "RM2D.h"
#include "RM3D.h"

void ArrowMover::BeginPlay(rm::Entity self) {
    rm::Log::Info("「" + self.Name() + "」を矢印キーで動かせます (Space で回る)");
}

void ArrowMover::Tick(rm::Entity self, float dt) {
    ++frames;
    const float x = (rm::Input::Down(rm::Key::Right) ? 1.0f : 0.0f) - (rm::Input::Down(rm::Key::Left) ? 1.0f : 0.0f);
    const float y = (rm::Input::Down(rm::Key::Up) ? 1.0f : 0.0f) - (rm::Input::Down(rm::Key::Down) ? 1.0f : 0.0f);
    const bool turn = rm::Input::Down(rm::Key::Space);

    // A 3D entity: on the floor (up = farther away). A 2D one: on the screen (up = up).
    if (rm::Transform3D t(self); t.Valid()) {
        rm::Vec3 p = t.Position();
        p.x += x * speed * dt;
        p.z -= y * speed * dt;
        t.SetPosition(p);
        if (turn) {
            rm::Vec3 r = t.Rotation();
            r.y += spin * dt;
            t.SetRotation(r);
        }
    } else if (rm::Transform2D t2(self); t2.Valid()) {
        rm::Vec2 p = t2.Position();
        p.x += x * speed * dt;
        p.y += y * speed * dt;
        t2.SetPosition(p);
        if (turn) t2.SetRotation(t2.Rotation() + spin * dt);
    }
}

void ArrowMover::EndPlay(rm::Entity self) {
    rm::Log::Info("「" + self.Name() + "」: " + std::to_string(frames) + " コマ動きました");
}
