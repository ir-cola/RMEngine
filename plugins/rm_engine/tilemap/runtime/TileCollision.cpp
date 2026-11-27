#include "rm_engine/tilemap/runtime/TileCollision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace rm::tilemap {

namespace {

uint32_t TileKey(int source, int column, int row) { return (static_cast<uint32_t>(source) << 16) | (static_cast<uint32_t>(row) << 8) | static_cast<uint32_t>(column); }

uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
uint64_t MixFloat(uint64_t h, float f) {
    uint32_t u = 0;
    static_assert(sizeof u == sizeof f);
    std::memcpy(&u, &f, sizeof u);
    return Mix(h, u);
}

// 反転 (Tiled と同じ順: 斜め → 左右 → 上下) を、マスの中の点 (Y が上) に当てる。斜めは画像の向き
// (Y が下) で x と y を入れ替えること。
Vec2 Flip(Vec2 p, TileValue flips) {
    if (flips & kFlipDiagonal) p = {1 - p.y, 1 - p.x};
    if (flips & kFlipX) p.x = 1 - p.x;
    if (flips & kFlipY) p.y = 1 - p.y;
    return p;
}

// 「全部」と半分の形を、マスの 2 x 2 の細かい格子 (左下・右下・左上・右上) にしたもの。他の形: なし。
std::optional<std::array<bool, 4>> Quarters(TileShape s, TileValue flips) {
    std::array<bool, 4> q{};  // [y * 2 + x]
    switch (s) {
        case TileShape::Full: q = {true, true, true, true}; break;
        case TileShape::Top: q = {false, false, true, true}; break;
        case TileShape::Bottom: q = {true, true, false, false}; break;
        case TileShape::Left: q = {true, false, true, false}; break;
        case TileShape::Right: q = {false, true, false, true}; break;
        default: return std::nullopt;
    }
    if (flips == 0) return q;
    std::array<bool, 4> out{};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            if (!q[static_cast<size_t>(y * 2 + x)]) continue;
            const Vec2 c = Flip({0.25f + 0.5f * static_cast<float>(x), 0.25f + 0.5f * static_cast<float>(y)}, flips);
            out[static_cast<size_t>((c.y > 0.5f ? 1 : 0) * 2 + (c.x > 0.5f ? 1 : 0))] = true;
        }
    }
    return out;
}

}  // namespace

CollisionTable::CollisionTable(const Tileset& set) {
    uint64_t h = 1469598103934665603ull;
    for (const TileInfo& i : set.tiles) {
        if (i.shape == TileShape::None) continue;
        if (i.shape == TileShape::Custom && ConvexHull(i.points).empty()) continue;  // (点が足りない: 当たらない)
        shapes_[TileKey(i.source, i.column, i.row)] = i;
        h = Mix(Mix(Mix(Mix(h, static_cast<uint64_t>(i.source)), static_cast<uint64_t>(i.column)), static_cast<uint64_t>(i.row)), static_cast<uint64_t>(i.shape));
        for (const Vec2& p : i.points) h = MixFloat(MixFloat(h, p.x), p.y);
    }
    fingerprint_ = h;
}

const TileInfo* CollisionTable::Find(TileValue v) const {
    if (IsEmpty(v)) return nullptr;
    const auto it = shapes_.find(TileKey(SourceOf(v), ColumnOf(v), RowOf(v)));
    return it == shapes_.end() ? nullptr : &it->second;
}

std::vector<Vec2> ConvexHull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return {};
    auto cross = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<Vec2> h(pts.size() * 2);
    size_t k = 0;
    for (const Vec2& p : pts) {  // (下の縁)
        while (k >= 2 && cross(h[k - 2], h[k - 1], p) <= 0) --k;
        h[k++] = p;
    }
    for (size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {  // (上の縁)
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    if (h.size() < 3) return {};
    double area = 0;
    for (size_t i = 0; i < h.size(); ++i) area += cross({0, 0}, h[i], h[(i + 1) % h.size()]);
    if (std::abs(area) < 1e-6) return {};
    return h;
}

std::vector<Vec2> ShapePolygon(const TileInfo& info, TileValue flips) {
    std::vector<Vec2> p;
    switch (info.shape) {
        case TileShape::None: return {};
        case TileShape::Full: p = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; break;
        case TileShape::Top: p = {{0, 0.5f}, {1, 0.5f}, {1, 1}, {0, 1}}; break;
        case TileShape::Bottom: p = {{0, 0}, {1, 0}, {1, 0.5f}, {0, 0.5f}}; break;
        case TileShape::Left: p = {{0, 0}, {0.5f, 0}, {0.5f, 1}, {0, 1}}; break;
        case TileShape::Right: p = {{0.5f, 0}, {1, 0}, {1, 1}, {0.5f, 1}}; break;
        case TileShape::SlopeBottomLeft: p = {{0, 0}, {1, 0}, {0, 1}}; break;
        case TileShape::SlopeBottomRight: p = {{0, 0}, {1, 0}, {1, 1}}; break;
        case TileShape::SlopeTopLeft: p = {{0, 0}, {1, 1}, {0, 1}}; break;
        case TileShape::SlopeTopRight: p = {{1, 0}, {1, 1}, {0, 1}}; break;
        case TileShape::Custom:
            for (const Vec2& q : info.points) p.push_back({std::clamp(q.x, 0.f, 1.f), std::clamp(q.y, 0.f, 1.f)});
            break;
    }
    for (Vec2& q : p) q = Flip(q, flips);
    return ConvexHull(std::move(p));
}

ChunkCollision CollideChunk(std::span<const TileValue> cells, Cell chunk, Vec2 cellSize, const CollisionTable& table) {
    ChunkCollision out;
    if (table.Empty() || cells.size() < static_cast<size_t>(kChunkCells)) return out;
    const Vec2 size = UsableCellSize(cellSize);
    constexpr int kFine = kChunkSize * 2;  // 細かい格子の一辺 (マスの半分ごと)
    std::vector<uint8_t> fine(static_cast<size_t>(kFine) * kFine, 0);  // 1: 塗った・2: 使った
    const Vec2 origin = CellMin({chunk.x * kChunkSize, chunk.y * kChunkSize}, size);
    for (int i = 0; i < kChunkCells; ++i) {
        const TileValue v = cells[static_cast<size_t>(i)];
        const TileInfo* info = table.Find(v);
        if (!info) continue;
        const int cx = i % kChunkSize, cy = i / kChunkSize;
        if (const auto q = Quarters(info->shape, FlipsOf(v))) {
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    if ((*q)[static_cast<size_t>(y * 2 + x)]) fine[static_cast<size_t>((cy * 2 + y) * kFine + cx * 2 + x)] = 1;
                }
            }
            continue;
        }
        std::vector<Vec2> poly = ShapePolygon(*info, FlipsOf(v));
        if (poly.empty()) continue;
        for (Vec2& p : poly) p = {origin.x + (static_cast<float>(cx) + p.x) * size.x, origin.y + (static_cast<float>(cy) + p.y) * size.y};
        out.polygons.push_back(std::move(poly));
    }
    // 貪欲な長方形: 下の行から、まだ使っていない塗った所の横の帯を取り、上へ同じ帯が続く限り伸ばす。
    const float hx = size.x * 0.5f, hy = size.y * 0.5f;
    auto at = [&](int x, int y) -> uint8_t& { return fine[static_cast<size_t>(y * kFine + x)]; };
    for (int y = 0; y < kFine; ++y) {
        for (int x = 0; x < kFine; ++x) {
            if (at(x, y) != 1) continue;
            int x1 = x;
            while (x1 + 1 < kFine && at(x1 + 1, y) == 1) ++x1;
            int y1 = y;
            for (;;) {
                if (y1 + 1 >= kFine) break;
                bool row = true;
                for (int k = x; k <= x1 && row; ++k) row = at(k, y1 + 1) == 1;
                if (!row) break;
                ++y1;
            }
            for (int yy = y; yy <= y1; ++yy) {
                for (int xx = x; xx <= x1; ++xx) at(xx, yy) = 2;
            }
            out.boxes.push_back({{origin.x + static_cast<float>(x) * hx, origin.y + static_cast<float>(y) * hy},
                                 {origin.x + static_cast<float>(x1 + 1) * hx, origin.y + static_cast<float>(y1 + 1) * hy}});
        }
    }
    return out;
}

}  // namespace rm::tilemap
