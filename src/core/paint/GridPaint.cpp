#include "core/paint/GridPaint.h"

#include <algorithm>
#include <cstdlib>

namespace rm::paint {

std::vector<Cell> LineCells(Cell a, Cell b) {
    std::vector<Cell> out;
    const long long dx = std::llabs(static_cast<long long>(b.x) - a.x), dy = -std::llabs(static_cast<long long>(b.y) - a.y);
    if (dx > static_cast<long long>(kMaxPaintCells) || -dy > static_cast<long long>(kMaxPaintCells)) return {a, b};
    const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
    long long err = dx + dy;
    Cell p = a;
    out.reserve(static_cast<size_t>(std::max(dx, -dy)) + 1);
    for (;;) {
        out.push_back(p);
        if (p == b) break;
        const long long e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            p.x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            p.y += sy;
        }
    }
    return out;
}

long long RectCount(Cell a, Cell b) {
    const long long w = std::llabs(static_cast<long long>(b.x) - a.x) + 1, h = std::llabs(static_cast<long long>(b.y) - a.y) + 1;
    return w * h;
}

std::vector<Cell> RectCells(Cell a, Cell b, bool outline) {
    if (RectCount(a, b) > static_cast<long long>(kMaxPaintCells)) return {};
    const int x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    std::vector<Cell> out;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (outline && y != y0 && y != y1 && x != x0 && x != x1) continue;
            out.push_back({x, y});
        }
    }
    return out;
}

}  // namespace rm::paint
