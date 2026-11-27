#include "rm_engine/tilemap/runtime/TileCell.h"

#include <algorithm>
#include <cmath>

namespace rm::tilemap {

Vec2 UsableCellSize(Vec2 cellSize) {
    auto usable = [](float v) { return std::isfinite(v) && std::abs(v) > 1e-6f ? std::abs(v) : 1.f; };
    return {usable(cellSize.x), usable(cellSize.y)};
}

Cell CellAt(Vec2 local, Vec2 cellSize) {
    const Vec2 s = UsableCellSize(cellSize);
    auto cell = [](float v) {
        const double f = std::floor(static_cast<double>(v));
        return static_cast<int>(std::clamp(f, -1.0e9, 1.0e9));
    };
    return {cell(local.x / s.x), cell(local.y / s.y)};
}

Vec2 CellMin(Cell c, Vec2 cellSize) {
    const Vec2 s = UsableCellSize(cellSize);
    return {static_cast<float>(c.x) * s.x, static_cast<float>(c.y) * s.y};
}
Vec2 CellMax(Cell c, Vec2 cellSize) {
    const Vec2 s = UsableCellSize(cellSize);
    return {static_cast<float>(c.x + 1) * s.x, static_cast<float>(c.y + 1) * s.y};
}
Vec2 CellCenter(Cell c, Vec2 cellSize) {
    const Vec2 s = UsableCellSize(cellSize);
    return {(static_cast<float>(c.x) + 0.5f) * s.x, (static_cast<float>(c.y) + 0.5f) * s.y};
}

CellRange CellsIn(Vec2 mn, Vec2 mx, Vec2 cellSize) {
    const Cell a = CellAt({std::min(mn.x, mx.x), std::min(mn.y, mx.y)}, cellSize);
    const Cell b = CellAt({std::max(mn.x, mx.x), std::max(mn.y, mx.y)}, cellSize);
    return {a, b};
}

}  // namespace rm::tilemap
