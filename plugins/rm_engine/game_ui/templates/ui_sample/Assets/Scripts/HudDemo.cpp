#include "HudDemo.h"

#include "RMUI.h"

#include <string>

namespace {
void Show(const HudDemo& h) {
    rm::UI::SetText(h.scoreText, "スコア " + std::to_string(static_cast<int>(Global::score)));
    rm::UI::SetBar(h.hpBar, static_cast<float>(Global::hp));
}
}  // namespace

void HudDemo::BeginPlay(rm::Entity self) {
    (void)self;
    Global::score = 0;
    Global::hp = 1.0f;
    carry = 0;
    Show(*this);
}

void HudDemo::Tick(rm::Entity self, float dt) {
    (void)self;
    carry += static_cast<float>(scorePerSecond) * dt;
    const int whole = static_cast<int>(carry);
    Global::score += whole;
    carry -= static_cast<float>(whole);
    Global::hp = static_cast<float>(Global::hp) - hpPerSecond * dt;
    if (static_cast<float>(Global::hp) <= 0) {
        Global::hp = 0.0f;
        rm::Log::Info("HP が 0 になりました。スコア " + std::to_string(static_cast<int>(Global::score)) + " でタイトルへ");
        rm::Game::LoadScene("Title");
    }
    Show(*this);
}
