#include "rm_engine/space2d/runtime/Atlas.h"

#include <algorithm>
#include <cmath>

namespace rm::space2d {

PixelRect CellOf(uint32_t width, uint32_t height, int columns, int rows, int index) {
    const int cols = std::max(columns, 1), rws = std::max(rows, 1);
    const int count = cols * rws;
    const int i = ((index % count) + count) % count;
    const uint32_t cw = width / static_cast<uint32_t>(cols), ch = height / static_cast<uint32_t>(rws);
    if (cw == 0 || ch == 0) return {0, 0, width, height};  // セルがピクセルより多い: 画像全体
    const uint32_t col = static_cast<uint32_t>(i % cols), row = static_cast<uint32_t>(i / cols);
    return {col * cw, row * ch, cw, ch};
}

int AnimationStep(std::span<const int> frames, int cellCount, float framesPerSecond, double seconds, bool loop, int startStep) {
    const int n = frames.empty() ? std::max(cellCount, 1) : static_cast<int>(frames.size());
    const double steps = std::max(0.0, seconds) * std::max(0.f, framesPerSecond);
    long long k = static_cast<long long>(std::floor(steps)) + std::max(0, startStep);
    k = loop ? k % n : std::min<long long>(k, n - 1);
    return static_cast<int>(k);
}

int AnimationCell(std::span<const int> frames, int cellCount, float framesPerSecond, double seconds, bool loop, int startStep) {
    const int at = AnimationStep(frames, cellCount, framesPerSecond, seconds, loop, startStep);
    return frames.empty() ? at : frames[static_cast<size_t>(at)];
}

}  // namespace rm::space2d
