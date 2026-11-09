#include "editor/widgets/Ornament.h"

#include <cmath>
#include <numbers>

namespace rm::editor::ornament {

ui::Path Sakura(Vec2 center, float radius, float degreesCcw) {
    // One petal pointing up, traced from the picture the user gave (the tips at distance 1): a
    // rounded point near the centre, widest a little past half way, two tips with a V between them.
    ui::Path path;
    constexpr float kDeg = std::numbers::pi_v<float> / 180.f;
    for (int i = 0; i < 5; ++i) {
        const float a = (degreesCcw + 72.f * static_cast<float>(i)) * kDeg;
        const float cs = std::cos(a), sn = std::sin(a);
        // Turned counter-clockwise on screen (y points down): up (0, -1) goes to the left.
        auto at = [&](float x, float y) { return Vec2{center.x + radius * (x * cs + y * sn), center.y + radius * (y * cs - x * sn)}; };
        path.Begin(at(0, -0.095f));
        path.Cubic(at(0.17f, -0.17f), at(0.285f, -0.40f), at(0.285f, -0.58f));
        path.Cubic(at(0.285f, -0.78f), at(0.19f, -0.92f), at(0.09f, -0.996f));
        path.Line(at(0, -0.84f));  // the notch
        path.Line(at(-0.09f, -0.996f));
        path.Cubic(at(-0.19f, -0.92f), at(-0.285f, -0.78f), at(-0.285f, -0.58f));
        path.Cubic(at(-0.285f, -0.40f), at(-0.17f, -0.17f), at(0, -0.095f));
    }
    return path;
}

}  // namespace rm::editor::ornament
