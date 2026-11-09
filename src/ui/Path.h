#pragma once
// A shape made of straight lines and cubic curves: one or more closed figures, in DIPs.
// Plain data (built by pure code, tested); ui::Painter::FillPath draws it.

#include "base/Math.h"

#include <cstdint>
#include <vector>

namespace rm::ui {

struct Path {
    enum class Op : uint8_t { Begin, Line, Cubic };
    struct Step {
        Op op;
        Vec2 a, b, c;  // Begin / Line: a. Cubic: a and b are the control points, c the end.
    };
    std::vector<Step> steps;

    void Begin(Vec2 p) { steps.push_back({Op::Begin, p, {}, {}}); }  // a new figure (the last one is closed)
    void Line(Vec2 p) { steps.push_back({Op::Line, p, {}, {}}); }
    void Cubic(Vec2 c1, Vec2 c2, Vec2 end) { steps.push_back({Op::Cubic, c1, c2, end}); }
};

}  // namespace rm::ui
