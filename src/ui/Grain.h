#pragma once
// 細かい粒の模様 (砂のざらざら): 一辺 `size` ピクセルの絵に、明るい粒と暗い粒を散らしたもの。純粋
// (tests/TestUi*.cpp)。Painter::FillGrain が、これを繰り返す (タイルにする) ブラシで面を塗る。
// 絵の端は上下・左右でつながる (粒は格子のマスごとに決まり、端を越えると反対側から続く)。

#include "ui/Types.h"

#include <cstdint>
#include <vector>

namespace rm::ui {

struct GrainLook {
    Color light{1, 1, 1, 0.35f};    // 明るい粒 (アルファ = 濃さ)
    Color dark{0, 0, 0, 0.2f};      // 暗い粒
    float lightShare = 0.12f;       // 明るい粒になるピクセルの割合 (0 .. 1)
    float darkShare = 0.12f;        // 暗い粒になるピクセルの割合
    float clumps = 0.03f;           // 2 x 2 の大きめの粒になる割合 (暗い)
    uint32_t seed = 1;
};

// `size` x `size` の BGRA (アルファを掛けた色: D2D の PREMULTIPLIED)。1 ピクセル 1 つの uint32_t
// (メモリの上で B, G, R, A の順)。
std::vector<uint32_t> GrainPixels(int size, const GrainLook& look);

}  // namespace rm::ui
