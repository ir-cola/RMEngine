#include "ShowScore.h"

#include "RMUI.h"

#include <string>

void ShowScore::BeginPlay(rm::Entity self) {
    rm::UI::SetText(self, "前回のスコア " + std::to_string(static_cast<int>(Global::score)));
}
