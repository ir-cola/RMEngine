#pragma once
// ただのピクセルとしての画像 (platform/Image.h のデコード先で、3D のテクスチャの元)。
// 標準ライブラリだけ: Windows のヘッダーを見てはいけないコード (純粋なコードとそのテスト) も、
// 画像を持てる。

#include <cstdint>
#include <vector>

namespace rm::platform {

enum class PixelOrder { Rgba, PremultipliedBgra };

// これより大きい画像は、形を保ったまま、その中に収まるよう縮小する (0 = その方向に制限なし)。
struct Fit {
    uint32_t maxWidth = 0, maxHeight = 0;
};

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;  // 1 ピクセル 4 バイト。行は上から。すき間なし
};

}  // namespace rm::platform
