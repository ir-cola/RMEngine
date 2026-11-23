#include "ControlValue.h"

#include "RMUI.h"

#include <cstdio>
#include <string>

namespace {

// 入力欄は入っている文字、スライダーは値、トグルはオン / オフ。
std::string ValueOf(rm::Entity self) {
    const std::string text = rm::UI::Text(self);
    if (!text.empty()) return "「" + text + "」";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", rm::UI::Value(self));
    return buf;
}

}  // namespace

void ControlValue::BeginPlay(rm::Entity self) {
    rm::Log::Info("入力の部品の見本: " + self.Name() + " = " + ValueOf(self));
}

void ControlValue::OnEvent(rm::Entity self, const rm::Event& e) {
    if (e.Is(rm::UI::kChanged)) rm::Log::Info("入力の部品の見本: " + self.Name() + " が変わりました = " + ValueOf(self));
    if (e.Is(rm::UI::kSubmitted)) rm::Log::Info("入力の部品の見本: " + self.Name() + " で Enter「" + std::string(e.text) + "」");
}
