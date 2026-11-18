#pragma once
// 2D 空間 - an atlas (a sprite sheet): one image cut into a grid of cells of the same size, numbered
// left to right, then top to bottom (0 = top left). A sprite draws one cell; an animation steps through
// cells. Pure - tested in tests/TestSpace2d.cpp.

#include <cstdint>
#include <span>

namespace rm::space2d {

// A rectangle of an image's pixels (from its top left). w or h 0: none.
struct PixelRect {
    uint32_t x = 0, y = 0, w = 0, h = 0;
    bool operator==(const PixelRect&) const = default;
};

// Cell `index` of an image `width` x `height` cut into `columns` x `rows` (each at least 1). An index
// past the last (or below 0) wraps around. The last column / row takes what does not divide evenly
// away (never past the image's edge).
PixelRect CellOf(uint32_t width, uint32_t height, int columns, int rows, int index);

// An animation's cell after `seconds` playing `framesPerSecond`: the cells in `frames` in turn (empty:
// 0 .. cellCount - 1). `loop` false stops on the last one.
int AnimationCell(std::span<const int> frames, int cellCount, float framesPerSecond, double seconds, bool loop, int startStep = 0);
// The place in the sequence (0 .. its length - 1) the same way: `startStep` steps in, then `seconds` more.
int AnimationStep(std::span<const int> frames, int cellCount, float framesPerSecond, double seconds, bool loop, int startStep = 0);

}  // namespace rm::space2d
