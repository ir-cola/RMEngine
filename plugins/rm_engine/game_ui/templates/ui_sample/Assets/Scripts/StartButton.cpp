#include "StartButton.h"

void StartButton::BeginPlay(rm::Entity self) {
    (void)self;
    time = 0;
}

void StartButton::Tick(rm::Entity self, float dt) {
    time += dt;
    if (after > 0 && time >= after) {
        after = 0;  // (once)
        OnClick(self);
    }
}

void StartButton::OnClick(rm::Entity self) {
    rm::Log::Info("「" + self.Name() + "」が押されました。シーン「" + next + "」へ");
    rm::Game::LoadScene(next);  // (switched at the end of this frame)
}
