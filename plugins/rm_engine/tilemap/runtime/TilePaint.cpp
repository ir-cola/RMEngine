#include "rm_engine/tilemap/runtime/TilePaint.h"

#include "core/paint/GridPaint.h"

#include <algorithm>

namespace rm::tilemap {

TileValue Stamp::At(int i, int j) const {
    if (Empty()) return 0;
    const int x = ((i % width) + width) % width, y = ((j % height) + height) % height;
    return tiles[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}

Stamp StampOfTiles(int source, int column, int row, int width, int height) {
    Stamp s;
    s.width = std::clamp(width, 1, kMaxTileColumn + 1);
    s.height = std::clamp(height, 1, kMaxTileRow + 1);
    s.tiles.reserve(static_cast<size_t>(s.width) * static_cast<size_t>(s.height));
    for (int j = 0; j < s.height; ++j) {
        for (int i = 0; i < s.width; ++i) s.tiles.push_back(MakeTile(source, column + i, row + j));
    }
    return s;
}

Stamp PickStamp(const TileStore& store, Cell a, Cell b) {
    if (paint::RectCount(a, b) > static_cast<long long>(paint::kMaxPaintCells)) return {};
    const int x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    Stamp s;
    s.width = x1 - x0 + 1;
    s.height = y1 - y0 + 1;
    for (int y = y1; y >= y0; --y) {  // (上の行から)
        for (int x = x0; x <= x1; ++x) s.tiles.push_back(store.Get({x, y}));
    }
    return s;
}

std::vector<Cell> ChunksOf(const ChunkSet& set) {
    std::vector<Cell> out;
    out.reserve(set.size());
    for (const auto& [x, y] : set) out.push_back({x, y});
    return out;
}

namespace {

bool SetCell(TileStore& store, Cell c, TileValue v, ChunkSet& touched) {
    if (!store.Set(c, v)) return false;
    const Cell k = ChunkOf(c);
    touched.insert({k.x, k.y});
    return true;
}

}  // namespace

bool PlaceStamp(TileStore& store, Cell at, const Stamp& stamp, ChunkSet& touched) {
    if (stamp.Empty()) return false;
    bool changed = false;
    for (int j = 0; j < stamp.height; ++j) {
        for (int i = 0; i < stamp.width; ++i) {
            const TileValue v = stamp.At(i, j);
            if (IsEmpty(v)) continue;  // (スポイトで取った空のマスは、下を残す)
            changed = SetCell(store, {at.x + i, at.y - j}, v, touched) || changed;
        }
    }
    return changed;
}

bool FillRect(TileStore& store, Cell a, Cell b, const Stamp& stamp, bool outline, ChunkSet& touched) {
    if (stamp.Empty()) return false;
    const std::vector<Cell> cells = paint::RectCells(a, b, outline);
    if (cells.empty()) return false;
    const int left = std::min(a.x, b.x), top = std::max(a.y, b.y);
    bool changed = false;
    for (const Cell& c : cells) {
        const TileValue v = stamp.At(c.x - left, top - c.y);
        if (IsEmpty(v)) continue;
        changed = SetCell(store, c, v, touched) || changed;
    }
    return changed;
}

bool EraseAt(TileStore& store, Cell at, int width, int height, ChunkSet& touched) {
    bool changed = false;
    for (int j = 0; j < std::max(height, 1); ++j) {
        for (int i = 0; i < std::max(width, 1); ++i) changed = SetCell(store, {at.x + i, at.y - j}, 0, touched) || changed;
    }
    return changed;
}

bool EraseRect(TileStore& store, Cell a, Cell b, bool outline, ChunkSet& touched) {
    bool changed = false;
    for (const Cell& c : paint::RectCells(a, b, outline)) changed = SetCell(store, c, 0, touched) || changed;
    return changed;
}

}  // namespace rm::tilemap
