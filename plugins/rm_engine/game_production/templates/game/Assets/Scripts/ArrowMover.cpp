#include "ArrowMover.h"

#include "RM2D.h"
#include "RM3D.h"

void ArrowMover::BeginPlay(rm::Entity self) {
    rm::Log::Info("「" + self.Name() + "」を矢印キー・WASD・ゲームパッドで動かせます (Space / A ボタンで回る)");
}

void ArrowMover::Tick(rm::Entity self, float dt) {
    ++frames;
    // Actions of the game settings' 入力 (not keys): MoveX = ← → / A D / the left stick, MoveY = ↑ ↓ / W S,
    // Jump = Space / the pad's A. Change what drives them there - this code stays as it is.
    const float x = rm::Input::Axis("MoveX");
    const float y = rm::Input::Axis("MoveY");
    const bool turn = rm::Input::Down("Jump");
    if (rm::Input::Pressed("Jump")) rm::Audio::Play(jumpSound);  // (RMAudio.h: once, the moment it is pressed)

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
