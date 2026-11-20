#pragma once
// 直線と 3 次曲線でできた形: 閉じた図形 1 つ以上。DIP で表す。
// ただのデータ (純粋なコードが作り、テストする)。ui::Painter::FillPath が描く。

#include "base/Math.h"

#include <cstdint>
#include <vector>

namespace rm::ui {

struct Path {
    enum class Op : uint8_t { Begin, Line, Cubic };
    struct Step {
        Op op;
        Vec2 a, b, c;  // Begin / Line: a。Cubic: a と b が制御点、c が終点。
    };
    std::vector<Step> steps;

    void Begin(Vec2 p) { steps.push_back({Op::Begin, p, {}, {}}); }  // 新しい図形 (前のものは閉じる)
    void Line(Vec2 p) { steps.push_back({Op::Line, p, {}, {}}); }
    void Cubic(Vec2 c1, Vec2 c2, Vec2 end) { steps.push_back({Op::Cubic, c1, c2, end}); }
};

}  // namespace rm::ui
