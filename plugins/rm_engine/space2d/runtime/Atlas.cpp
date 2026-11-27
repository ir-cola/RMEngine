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

GridCount GridCountOf(uint32_t width, uint32_t height, const GridCut& cut) {
    auto count = [](uint32_t size, uint32_t cell, uint32_t margin, uint32_t spacing) {
        if (cell == 0 || size < 2 * static_cast<uint64_t>(margin) + cell) return 0;
        // margin + n * cell + (n - 1) * spacing <= size - margin
        const uint64_t inner = static_cast<uint64_t>(size) - 2 * static_cast<uint64_t>(margin);
        return static_cast<int>(std::min<uint64_t>((inner + spacing) / (static_cast<uint64_t>(cell) + spacing), 1u << 20));
    };
    return {count(width, cut.cellWidth, cut.margin, cut.spacing), count(height, cut.cellHeight, cut.margin, cut.spacing)};
}

PixelRect GridCellOf(uint32_t width, uint32_t height, const GridCut& cut, int column, int row) {
    const GridCount n = GridCountOf(width, height, cut);
    if (column < 0 || row < 0 || column >= n.columns || row >= n.rows) return {};
    const uint32_t x = cut.margin + static_cast<uint32_t>(column) * (cut.cellWidth + cut.spacing);
    const uint32_t y = cut.margin + static_cast<uint32_t>(row) * (cut.cellHeight + cut.spacing);
    return {x, y, cut.cellWidth, cut.cellHeight};
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
