#include "rm_engine/climate/runtime/Climate.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <iterator>
#include <limits>
#include <mutex>
#include <numeric>
#include <string>
#include <unordered_map>

namespace rm::climate {

namespace {

float Finite(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

float Length3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

// 3D の Transform の列 c (軸) の長さ。
float AxisLength(const space3d::Mat4& m, int c) { return Length3(m.m[c][0], m.m[c][1], m.m[c][2]); }

}  // namespace

float ZoneDepth(const ZoneBody& body, Vec3 p) {
    const Vec3 h = body.half;
    switch (body.kind) {
        case ZoneBody::Kind::Box: return std::min({h.x - std::fabs(p.x), h.y - std::fabs(p.y), h.z - std::fabs(p.z)});
        case ZoneBody::Kind::Cylinder: return std::min(h.x - std::sqrt(p.x * p.x + p.z * p.z), h.y - std::fabs(p.y));
        case ZoneBody::Kind::Rect: return std::min(h.x - std::fabs(p.x), h.y - std::fabs(p.y));
        case ZoneBody::Kind::Circle: return h.x - std::sqrt(p.x * p.x + p.y * p.y);
    }
    return -1.f;
}

float ZoneWeight(float depth, float border) {
    if (!std::isfinite(depth) || depth < 0) return 0.f;
    if (!(border > 0)) return 1.f;
    const float t = std::clamp(depth / border, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

ClimateHere Blend(ClimateHere outside, std::span<const ZoneEffect> zones) {
    std::vector<size_t> order(zones.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return zones[a].priority < zones[b].priority; });
    ClimateHere c = outside;
    for (const size_t i : order) {
        const ZoneEffect& z = zones[i];
        const float w = std::isfinite(z.weight) ? std::clamp(z.weight, 0.f, 1.f) : 0.f;
        if (!(w > 0)) continue;
        if (z.setTemperature) c.temperature += (Finite(z.temperature, c.temperature) - c.temperature) * w;
        if (z.setHumidity) c.humidity += (Finite(z.humidity, c.humidity) - c.humidity) * w;
    }
    c.humidity = std::clamp(c.humidity, 0.f, 100.f);
    return c;
}

float CooledByHeight(float temperature, float height, const ClimateSettings& s) {
    const float lapse = std::isfinite(s.lapse) ? std::max(s.lapse, 0.f) : 0.f;
    const float above = Finite(height, 0.f) - Finite(s.baseHeight, 0.f);
    return temperature - above / 100.f * lapse;
}

ZoneBody BodyOf(const ClimateZone& zone, const space3d::Mat4& world) {
    ZoneBody b;
    const float sx = AxisLength(world, 0), sy = AxisLength(world, 1), sz = AxisLength(world, 2);
    const Vec3 size{std::max(Finite(zone.size.x, 0.f), 0.f), std::max(Finite(zone.size.y, 0.f), 0.f), std::max(Finite(zone.size.z, 0.f), 0.f)};
    if (zone.shape == ZoneShape::Cylinder) {
        b.kind = ZoneBody::Kind::Cylinder;
        b.half = {size.x * 0.5f * (sx + sz) * 0.5f, size.y * 0.5f * sy, 0};  // (横の大きさが違うときは平均の丸)
    } else {
        b.kind = ZoneBody::Kind::Box;
        b.half = {size.x * 0.5f * sx, size.y * 0.5f * sy, size.z * 0.5f * sz};
    }
    return b;
}

ZoneBody BodyOf(const ClimateZone2D& zone, const space2d::Mat3& world) {
    ZoneBody b;
    const float sx = std::sqrt(world.a * world.a + world.b * world.b), sy = std::sqrt(world.c * world.c + world.d * world.d);
    const Vec2 size{std::max(Finite(zone.size.x, 0.f), 0.f), std::max(Finite(zone.size.y, 0.f), 0.f)};
    if (zone.shape == ZoneShape2D::Circle) {
        b.kind = ZoneBody::Kind::Circle;
        b.half = {size.x * 0.5f * (sx + sy) * 0.5f, 0, 0};
    } else {
        b.kind = ZoneBody::Kind::Rect;
        b.half = {size.x * 0.5f * sx, size.y * 0.5f * sy, 0};
    }
    return b;
}

Vec3 LocalOf(const space3d::Mat4& world, Vec3 point) {
    const Vec3 d{point.x - world.m[3][0], point.y - world.m[3][1], point.z - world.m[3][2]};
    float out[3] = {0, 0, 0};
    for (int c = 0; c < 3; ++c) {
        const float len = AxisLength(world, c);
        if (len > 1e-12f) out[c] = (d.x * world.m[c][0] + d.y * world.m[c][1] + d.z * world.m[c][2]) / len;
    }
    return {out[0], out[1], out[2]};
}

Vec3 LocalOf(const space2d::Mat3& world, Vec2 point) {
    const float dx = point.x - world.tx, dy = point.y - world.ty;
    const float lx = std::sqrt(world.a * world.a + world.b * world.b), ly = std::sqrt(world.c * world.c + world.d * world.d);
    return {lx > 1e-12f ? (dx * world.a + dy * world.b) / lx : 0.f, ly > 1e-12f ? (dx * world.c + dy * world.d) / ly : 0.f, 0};
}

// --- 気候の地図 --------------------------------------------------------------------------------

namespace {

float CellSizeOf(float s) { return std::isfinite(s) ? std::clamp(s, kMinCellSize, kMaxCellSize) : 4.f; }

int FloorCell(double v) {
    const double f = std::floor(v);
    return f < -1e9 ? -1000000000 : f > 1e9 ? 1000000000 : static_cast<int>(f);
}

size_t CellIndex(int i, int j, int width) { return static_cast<size_t>(j) * static_cast<size_t>(width) + static_cast<size_t>(i); }

}  // namespace

std::vector<ClimateKind> DefaultClimateKinds() {
    std::vector<ClimateKind> k(4);
    k[0] = {1, "温帯", Color::Hex(0x6BBF59), 15.f, 50.f};
    k[1] = {2, "寒冷", Color::Hex(0x8EC9F0), -8.f, 60.f};
    k[2] = {3, "砂漠", Color::Hex(0xE8C26E), 32.f, 10.f};
    k[3] = {4, "熱帯", Color::Hex(0x2E9E6B), 28.f, 85.f};
    return k;
}

Color ClimateKindColor(size_t n) {
    static constexpr uint32_t kColors[] = {0xD9705A, 0x9B7FD4, 0x5FB3B3, 0xC9A227, 0xE58FB8, 0x7A9E4E, 0x5D7FC9, 0xB5835A};
    return Color::Hex(kColors[n % std::size(kColors)]);
}

int PaintGrid::At(int x, int z) const {
    const int64_t i = static_cast<int64_t>(x) - cellX, j = static_cast<int64_t>(z) - cellZ;
    if (i < 0 || j < 0 || i >= width || j >= depth) return 0;
    const size_t at = CellIndex(static_cast<int>(i), static_cast<int>(j), width);
    return at < ids.size() ? ids[at] : 0;
}

std::string EncodeRuns(std::span<const int> ids) {
    std::string out;
    for (size_t i = 0; i < ids.size();) {
        size_t n = 1;
        while (i + n < ids.size() && ids[i + n] == ids[i]) ++n;
        if (!out.empty()) out += ',';
        out += std::to_string(ids[i]);
        if (n > 1) out += '*' + std::to_string(n);
        i += n;
    }
    return out;
}

std::vector<int> DecodeRuns(std::string_view text, size_t count) {
    std::vector<int> out;
    out.reserve(count);
    size_t pos = 0;
    while (pos < text.size() && out.size() < count) {
        size_t end = text.find(',', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view token = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t star = token.find('*');
        const std::string_view idText = token.substr(0, star);
        int id = 0;
        long long n = 1;
        if (std::from_chars(idText.data(), idText.data() + idText.size(), id).ec != std::errc{} || id < 0) id = 0;
        if (star != std::string_view::npos) {
            const std::string_view nText = token.substr(star + 1);
            if (std::from_chars(nText.data(), nText.data() + nText.size(), n).ec != std::errc{} || n < 0) n = 0;
        }
        const size_t take = std::min(static_cast<size_t>(n), count - out.size());
        out.insert(out.end(), take, id);
    }
    out.resize(count, 0);
    return out;
}

namespace {

// 層 `layer` の筆の塗りが入っている地図のフィールド。
struct LayerFields {
    int* x;
    int* z;
    int* width;
    int* depth;
    std::string* cells;
};
LayerFields FieldsOf(ClimateMap& m, MapLayer layer) {
    if (layer == MapLayer::Clouds) return {&m.cloudX, &m.cloudZ, &m.cloudWidth, &m.cloudDepth, &m.cloudCells};
    if (layer == MapLayer::Rain) return {&m.rainX, &m.rainZ, &m.rainWidth, &m.rainDepth, &m.rainCells};
    return {&m.cellX, &m.cellZ, &m.width, &m.depth, &m.cells};
}

// 長方形をワールドのマス x0 .. x1, z0 .. z1 が入るように広げる (一辺 kMaxPaintCells まで)。その中で塗れる
// 所に x0 .. z1 を縮める。塗れる所が無ければ false。
bool GrowTo(PaintGrid& g, int& x0, int& z0, int& x1, int& z1, int mx, int mz) {
    if (g.Empty()) {
        x0 = std::max(x0, mx - kMaxPaintCells / 2), x1 = std::min(x1, x0 + kMaxPaintCells - 1);
        z0 = std::max(z0, mz - kMaxPaintCells / 2), z1 = std::min(z1, z0 + kMaxPaintCells - 1);
    } else {
        x0 = std::max(x0, g.cellX + g.width - kMaxPaintCells), x1 = std::min(x1, g.cellX + kMaxPaintCells - 1);
        z0 = std::max(z0, g.cellZ + g.depth - kMaxPaintCells), z1 = std::min(z1, g.cellZ + kMaxPaintCells - 1);
    }
    if (x0 > x1 || z0 > z1) return false;
    const int nx0 = g.Empty() ? x0 : std::min(g.cellX, x0), nz0 = g.Empty() ? z0 : std::min(g.cellZ, z0);
    const int nx1 = g.Empty() ? x1 : std::max(g.cellX + g.width - 1, x1), nz1 = g.Empty() ? z1 : std::max(g.cellZ + g.depth - 1, z1);
    if (g.Empty() || nx0 != g.cellX || nz0 != g.cellZ || nx1 - nx0 + 1 != g.width || nz1 - nz0 + 1 != g.depth) {
        PaintGrid grown;
        grown.cellX = nx0;
        grown.cellZ = nz0;
        grown.width = nx1 - nx0 + 1;
        grown.depth = nz1 - nz0 + 1;
        grown.ids.assign(static_cast<size_t>(grown.width) * static_cast<size_t>(grown.depth), 0);
        for (int j = 0; j < g.depth; ++j) {
            for (int i = 0; i < g.width; ++i) grown.ids[CellIndex(i + g.cellX - nx0, j + g.cellZ - nz0, grown.width)] = g.ids[CellIndex(i, j, g.width)];
        }
        g = std::move(grown);
    }
    return true;
}

bool SetCell(PaintGrid& g, int x, int z, int id) {
    const int64_t i = static_cast<int64_t>(x) - g.cellX, j = static_cast<int64_t>(z) - g.cellZ;
    if (i < 0 || j < 0 || i >= g.width || j >= g.depth) return false;
    int& cell = g.ids[CellIndex(static_cast<int>(i), static_cast<int>(j), g.width)];
    if (cell == id) return false;
    cell = id;
    return true;
}

}  // namespace

bool operator==(const ClimateShape& a, const ClimateShape& b) {
    return a.layer == b.layer && a.kind == b.kind && a.clouds == b.clouds && a.rain == b.rain && a.soft == b.soft && a.points == b.points;
}

PaintGrid PaintOf(const ClimateMap& map, MapLayer layer) {
    const LayerFields f = FieldsOf(const_cast<ClimateMap&>(map), layer);
    PaintGrid g;
    if (*f.width <= 0 || *f.depth <= 0 || *f.width > kMaxPaintCells || *f.depth > kMaxPaintCells) return g;
    g.cellX = *f.x;
    g.cellZ = *f.z;
    g.width = *f.width;
    g.depth = *f.depth;
    g.ids = DecodeRuns(*f.cells, static_cast<size_t>(g.width) * static_cast<size_t>(g.depth));
    return g;
}

void SetPaint(ClimateMap& map, const PaintGrid& g, MapLayer layer) {
    const LayerFields f = FieldsOf(map, layer);
    int x0 = INT_MAX, z0 = INT_MAX, x1 = INT_MIN, z1 = INT_MIN;
    for (int j = 0; j < g.depth; ++j) {
        for (int i = 0; i < g.width; ++i) {
            const size_t at = CellIndex(i, j, g.width);
            if (at >= g.ids.size() || g.ids[at] == 0) continue;
            x0 = std::min(x0, i), x1 = std::max(x1, i), z0 = std::min(z0, j), z1 = std::max(z1, j);
        }
    }
    if (x0 > x1) {
        *f.x = *f.z = *f.width = *f.depth = 0;
        f.cells->clear();
        return;
    }
    std::vector<int> ids;
    ids.reserve(static_cast<size_t>(x1 - x0 + 1) * static_cast<size_t>(z1 - z0 + 1));
    for (int j = z0; j <= z1; ++j) {
        for (int i = x0; i <= x1; ++i) ids.push_back(g.ids[CellIndex(i, j, g.width)]);
    }
    *f.x = g.cellX + x0;
    *f.z = g.cellZ + z0;
    *f.width = x1 - x0 + 1;
    *f.depth = z1 - z0 + 1;
    *f.cells = EncodeRuns(ids);
}

int CloudCellOf(float clouds) { return static_cast<int>(std::lround(std::clamp(Finite(clouds, 0.f), 0.f, 100.f))) + 1; }
float CloudsOfCell(int cell) { return cell > 0 ? static_cast<float>(std::min(cell, 101) - 1) : 0.f; }

bool PaintCircle(PaintGrid& g, float cellSize, Vec2 center, float radius, int id) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y)) return false;
    const float cs = CellSizeOf(cellSize);
    const float r = std::isfinite(radius) ? std::max(radius, 0.f) : 0.f;
    const int mx = FloorCell(center.x / cs), mz = FloorCell(center.y / cs);  // 真ん中のマス
    int x0 = std::min(FloorCell((center.x - r) / cs), mx), x1 = std::max(FloorCell((center.x + r) / cs), mx);
    int z0 = std::min(FloorCell((center.y - r) / cs), mz), z1 = std::max(FloorCell((center.y + r) / cs), mz);
    id = std::max(id, 0);
    if (id == 0) {  // 消す: 今の長方形の中だけ
        if (g.Empty()) return false;
        x0 = std::max(x0, g.cellX), z0 = std::max(z0, g.cellZ);
        x1 = std::min(x1, g.cellX + g.width - 1), z1 = std::min(z1, g.cellZ + g.depth - 1);
    } else if (!GrowTo(g, x0, z0, x1, z1, mx, mz)) {
        return false;
    }
    bool changed = false;
    const double r2 = static_cast<double>(r) * r;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = (x + 0.5) * cs - center.x, dz = (z + 0.5) * cs - center.y;
            if (dx * dx + dz * dz > r2 && !(x == mx && z == mz)) continue;
            changed = SetCell(g, x, z, id) || changed;
        }
    }
    return changed;
}

bool InsideShape(std::span<const Vec2> pts, Vec2 p) {
    if (pts.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const Vec2 a = pts[i], b = pts[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

namespace {

// 形 `pts` の中に真ん中が入るマス (偶奇の規則) を、行 z ごとに x の範囲 [a, b] で `f(z, a, b)` に渡す
// (x0 .. x1, z0 .. z1 の中だけ)。
template <class F>
void ForInside(std::span<const Vec2> pts, float cs, int x0, int z0, int x1, int z1, F&& f) {
    std::vector<double> cross;
    for (int z = z0; z <= z1; ++z) {
        const double cz = (z + 0.5) * cs;
        cross.clear();
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            const Vec2 a = pts[i], b = pts[j];
            if ((a.y > cz) != (b.y > cz)) cross.push_back(a.x + (cz - a.y) * (static_cast<double>(b.x) - a.x) / (static_cast<double>(b.y) - a.y));
        }
        std::sort(cross.begin(), cross.end());
        for (size_t k = 0; k + 1 < cross.size(); k += 2) {
            // 真ん中 (x + 0.5) * cs が [cross[k], cross[k + 1]) にあるマス。
            const int a = std::max(x0, FloorCell(std::ceil(cross[k] / cs - 0.5))), b = std::min(x1, FloorCell(std::ceil(cross[k + 1] / cs - 0.5)) - 1);
            if (a <= b) f(z, a, b);
        }
    }
}

// 形の点の範囲 (ワールド)。点に数でないものがあれば false。
bool ShapeBounds(std::span<const Vec2> pts, float& lx, float& lz, float& hx, float& hz) {
    lx = std::numeric_limits<float>::max(), lz = lx, hx = -lx, hz = -lx;
    for (const Vec2& p : pts) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        lx = std::min(lx, p.x), hx = std::max(hx, p.x), lz = std::min(lz, p.y), hz = std::max(hz, p.y);
    }
    return true;
}

// 縁からの割合 t (0: 縁 .. 1: 濃さのまま) -> 濃さに掛ける値。なめらかな段 (smoothstep)。
float SoftFade(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// 2 乗した距離の表 `d2` (w x h。0: 形の外 / それ以外: 形の中) を、形の外の一番近いマスまでの
// 距離の 2 乗 (マスの単位) にする。行ごとに横の距離を出してから、列ごとに放物線の下側の包み
// (Felzenszwalb と Huttenlocher の距離変換) で縦を足す。どちらもマスの数に比例する手間。
void DistanceTransform(std::vector<float>& d2, int w, int h) {
    const float kFar = static_cast<float>(w + h) * static_cast<float>(w + h);
    for (int j = 0; j < h; ++j) {  // 横: 左から右、右から左で、同じ行の一番近い外のマス
        float* row = d2.data() + static_cast<size_t>(j) * static_cast<size_t>(w);
        float run = kFar;
        for (int i = 0; i < w; ++i) run = row[i] == 0 ? 0 : run + 1, row[i] = run;
        run = kFar;
        for (int i = w - 1; i >= 0; --i) run = row[i] == 0 ? 0 : run + 1, row[i] = std::min(row[i], run);
        for (int i = 0; i < w; ++i) row[i] = row[i] >= kFar ? kFar : row[i] * row[i];
    }
    std::vector<float> f(static_cast<size_t>(h)), z(static_cast<size_t>(h) + 1);
    std::vector<int> v(static_cast<size_t>(h));
    for (int i = 0; i < w; ++i) {  // 縦: d(q) = min_p ((q - p)^2 + f(p))
        for (int j = 0; j < h; ++j) f[j] = d2[static_cast<size_t>(j) * w + i];
        int k = 0;
        v[0] = 0;
        z[0] = -std::numeric_limits<float>::infinity(), z[1] = std::numeric_limits<float>::infinity();
        const auto cut = [&](int q, int p) { return ((f[q] + static_cast<float>(q) * q) - (f[p] + static_cast<float>(p) * p)) / (2.f * static_cast<float>(q - p)); };
        for (int q = 1; q < h; ++q) {
            float s = cut(q, v[k]);
            while (s <= z[k]) s = cut(q, v[--k]);  // (z[0] は -無限: k は 0 より下がらない)
            ++k;
            v[k] = q;
            z[k] = s;
            z[k + 1] = std::numeric_limits<float>::infinity();
        }
        k = 0;
        for (int q = 0; q < h; ++q) {
            while (z[k + 1] < static_cast<float>(q)) ++k;
            const float dq = static_cast<float>(q - v[k]);
            d2[static_cast<size_t>(q) * w + i] = dq * dq + f[v[k]];
        }
    }
}

}  // namespace

bool FillShape(PaintGrid& g, float cellSize, std::span<const Vec2> pts, int id) {
    if (pts.size() < 3) return false;
    const float cs = CellSizeOf(cellSize);
    float lx = std::numeric_limits<float>::max(), lz = lx, hx = -lx, hz = -lx;
    for (const Vec2& p : pts) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        lx = std::min(lx, p.x), hx = std::max(hx, p.x), lz = std::min(lz, p.y), hz = std::max(hz, p.y);
    }
    int x0 = FloorCell(lx / cs), x1 = FloorCell(hx / cs), z0 = FloorCell(lz / cs), z1 = FloorCell(hz / cs);
    id = std::max(id, 0);
    if (id == 0) {
        if (g.Empty()) return false;
        x0 = std::max(x0, g.cellX), z0 = std::max(z0, g.cellZ);
        x1 = std::min(x1, g.cellX + g.width - 1), z1 = std::min(z1, g.cellZ + g.depth - 1);
        if (x0 > x1 || z0 > z1) return false;
    } else if (!GrowTo(g, x0, z0, x1, z1, static_cast<int>((int64_t{x0} + x1) / 2), static_cast<int>((int64_t{z0} + z1) / 2))) {
        return false;
    }
    // 行ごとに、マスの真ん中の高さで辺と交わる所を出し、その間 (偶奇) のマスを塗る。
    bool changed = false;
    ForInside(pts, cs, x0, z0, x1, z1, [&](int z, int a, int b) {
        for (int x = a; x <= b; ++x) changed = SetCell(g, x, z, id) || changed;
    });
    return changed;
}

bool FillShapeSoft(PaintGrid& g, float cellSize, std::span<const Vec2> pts, float amount, float soft) {
    if (pts.size() < 3 || !std::isfinite(amount)) return false;
    amount = std::clamp(amount, 0.f, 100.f);
    soft = std::isfinite(soft) ? std::clamp(soft, 0.f, 1.f) : 1.f;
    if (CloudCellOf(amount) < 2) return false;  // (1 % より薄い: 塗らない)
    const float cs = CellSizeOf(cellSize);
    float lx, lz, hx, hz;
    if (!ShapeBounds(pts, lx, lz, hx, hz)) return false;
    int x0 = FloorCell(lx / cs), x1 = FloorCell(hx / cs), z0 = FloorCell(lz / cs), z1 = FloorCell(hz / cs);
    if (!GrowTo(g, x0, z0, x1, z1, static_cast<int>((int64_t{x0} + x1) / 2), static_cast<int>((int64_t{z0} + z1) / 2))) return false;
    // 中のマスに印 (まわりに 1 マスの外を足す) -> 外までの距離。
    const int w = x1 - x0 + 3, h = z1 - z0 + 3;
    std::vector<float> d2(static_cast<size_t>(w) * static_cast<size_t>(h), 0.f);
    bool any = false;
    ForInside(pts, cs, x0, z0, x1, z1, [&](int z, int a, int b) {
        for (int x = a; x <= b; ++x) d2[static_cast<size_t>(z - z0 + 1) * w + static_cast<size_t>(x - x0 + 1)] = 1.f;
        any = true;
    });
    if (!any) return false;
    DistanceTransform(d2, w, h);
    // 縁からの距離 = 外のマスの真ん中までの距離 - 半マス (縁のマスは半マス)。
    const float most = std::sqrt(*std::max_element(d2.begin(), d2.end())) - 0.5f;
    const float full = most * soft;  // (ここより内側は amount のまま)
    bool changed = false;
    for (int j = 1; j + 1 < h; ++j) {
        for (int i = 1; i + 1 < w; ++i) {
            const float dd = d2[static_cast<size_t>(j) * w + i];
            if (dd <= 0) continue;
            const float t = full > 0 ? std::clamp((std::sqrt(dd) - 0.5f) / full, 0.f, 1.f) : 1.f;
            const int id = CloudCellOf(amount * SoftFade(t));
            if (id < 2) continue;
            const int x = x0 + i - 1, z = z0 + j - 1;
            if (id > g.At(x, z)) changed = SetCell(g, x, z, id) || changed;
        }
    }
    return changed;
}

bool PaintCircleSoft(PaintGrid& g, float cellSize, Vec2 center, float radius, float amount, float soft) {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(amount)) return false;
    amount = std::clamp(amount, 0.f, 100.f);
    soft = std::isfinite(soft) ? std::clamp(soft, 0.f, 1.f) : 1.f;
    if (CloudCellOf(amount) < 2) return false;  // (1 % より薄い: 塗らない)
    const float cs = CellSizeOf(cellSize);
    const float r = std::isfinite(radius) ? std::max(radius, 0.f) : 0.f;
    const int mx = FloorCell(center.x / cs), mz = FloorCell(center.y / cs);  // 真ん中のマス (いつも濃さのまま)
    int x0 = std::min(FloorCell((center.x - r) / cs), mx), x1 = std::max(FloorCell((center.x + r) / cs), mx);
    int z0 = std::min(FloorCell((center.y - r) / cs), mz), z1 = std::max(FloorCell((center.y + r) / cs), mz);
    if (!GrowTo(g, x0, z0, x1, z1, mx, mz)) return false;
    const double r2 = static_cast<double>(r) * r;
    const float fade = r * soft;  // (縁からこの幅で薄くなる)
    bool changed = false;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = (x + 0.5) * cs - center.x, dz = (z + 0.5) * cs - center.y;
            const double d2 = dx * dx + dz * dz;
            const bool middle = x == mx && z == mz;
            if (d2 > r2 && !middle) continue;
            const float t = middle || !(fade > 0) ? 1.f : static_cast<float>((r - std::sqrt(d2)) / fade);
            const int id = CloudCellOf(amount * SoftFade(t));
            if (id < 2 || id <= g.At(x, z)) continue;
            changed = SetCell(g, x, z, id) || changed;
        }
    }
    return changed;
}

bool BakeShape(PaintGrid& g, const ClimateMap& map, const ClimateShape& s) {
    if (s.layer != MapLayer::Climate && s.layer != MapLayer::Clouds && s.layer != MapLayer::Rain) return false;
    if (s.points.size() < 3) return false;
    const std::span<const Vec2> pts = std::span(s.points).first(std::min(s.points.size(), kMaxShapePoints));
    if (s.layer == MapLayer::Rain) return FillShapeSoft(g, map.cellSize, pts, s.rain, s.soft / 100.f);
    const int id = ShapeCellOf(map, s);
    return id != 0 && FillShape(g, map.cellSize, pts, id);
}

int ShapeCellOf(const ClimateMap& map, const ClimateShape& s) {
    if (s.layer == MapLayer::Clouds) return CloudCellOf(s.clouds);
    if (s.layer == MapLayer::Rain) return CloudCellOf(s.rain);
    return s.kind > 0 && FindKind(map, s.kind) ? s.kind : 0;
}

PaintGrid BakedOf(const ClimateMap& map, MapLayer layer) {
    PaintGrid g = PaintOf(map, layer);
    for (const ClimateShape& s : map.shapes) {
        if (s.layer == layer) BakeShape(g, map, s);
    }
    return g;
}

PaintGrid Resample(const PaintGrid& g, float from, float to) {
    PaintGrid out;
    if (g.Empty()) return out;
    const double f = CellSizeOf(from), t = CellSizeOf(to);
    const double wx0 = g.cellX * f, wx1 = (static_cast<double>(g.cellX) + g.width) * f;
    const double wz0 = g.cellZ * f, wz1 = (static_cast<double>(g.cellZ) + g.depth) * f;
    out.cellX = FloorCell(wx0 / t);
    out.cellZ = FloorCell(wz0 / t);
    out.width = std::clamp(FloorCell(std::nextafter(wx1, wx0) / t) - out.cellX + 1, 1, kMaxPaintCells);
    out.depth = std::clamp(FloorCell(std::nextafter(wz1, wz0) / t) - out.cellZ + 1, 1, kMaxPaintCells);
    out.ids.assign(static_cast<size_t>(out.width) * static_cast<size_t>(out.depth), 0);
    for (int j = 0; j < out.depth; ++j) {
        for (int i = 0; i < out.width; ++i) {
            const double cx = (static_cast<double>(out.cellX) + i + 0.5) * t, cz = (static_cast<double>(out.cellZ) + j + 0.5) * t;
            out.ids[CellIndex(i, j, out.width)] = g.At(FloorCell(cx / f), FloorCell(cz / f));
        }
    }
    return out;
}

int NewKindId(const ClimateMap& map) {
    int most = 0;
    for (const ClimateKind& k : map.kinds) most = std::max(most, k.id);
    return most + 1;
}

const ClimateKind* FindKind(const ClimateMap& map, int id) {
    for (const ClimateKind& k : map.kinds) {
        if (k.id == id) return &k;
    }
    return nullptr;
}

void RemoveKind(ClimateMap& map, int id) {
    std::erase_if(map.kinds, [id](const ClimateKind& k) { return k.id == id; });
    std::erase_if(map.shapes, [id](const ClimateShape& s) { return s.layer == MapLayer::Climate && s.kind == id; });
    PaintGrid g = PaintOf(map);
    for (int& cell : g.ids) {
        if (cell == id) cell = 0;
    }
    SetPaint(map, g);
}

PaintTables::Table PaintTables::MakeTable(const PaintGrid& g, const std::function<int(int)>& value) {
    Table t;
    t.cellX = g.cellX, t.cellZ = g.cellZ, t.width = g.width, t.depth = g.depth;
    const size_t w = static_cast<size_t>(g.width) + 1;
    t.sums.assign(w * (static_cast<size_t>(g.depth) + 1), 0);
    for (int j = 0; j < g.depth; ++j) {
        int32_t row = 0;
        for (int i = 0; i < g.width; ++i) {
            row += value(g.ids[CellIndex(i, j, g.width)]);
            t.sums[(static_cast<size_t>(j) + 1) * w + static_cast<size_t>(i) + 1] = t.sums[static_cast<size_t>(j) * w + static_cast<size_t>(i) + 1] + row;
        }
    }
    return t;
}

PaintTables::PaintTables(const ClimateMap& map) {
    grid_ = BakedOf(map, MapLayer::Climate);
    cellSize_ = CellSizeOf(map.cellSize);
    half_ = std::isfinite(map.border) ? std::max(map.border, 0.f) * 0.5f : 0.f;
    kindCount_ = map.kinds.size();
    clouds_ = MakeAmount(BakedOf(map, MapLayer::Clouds));
    rain_ = MakeAmount(BakedOf(map, MapLayer::Rain));
    for (const int cell : rain_.grid.ids) mostRain_ = std::max(mostRain_, CloudsOfCell(cell));
    if (grid_.Empty()) return;
    std::vector<char> present;
    for (const int id : grid_.ids) {
        if (id <= 0) continue;
        if (static_cast<size_t>(id) >= present.size()) present.resize(static_cast<size_t>(id) + 1, 0);
        present[static_cast<size_t>(id)] = 1;
    }
    for (size_t k = 0; k < map.kinds.size(); ++k) {
        const ClimateKind& kind = map.kinds[k];
        if (kind.id <= 0 || static_cast<size_t>(kind.id) >= present.size() || !present[static_cast<size_t>(kind.id)]) continue;
        // (同じ番号の種類が 2 つあるときは最初のもの)
        if (std::any_of(kinds_.begin(), kinds_.end(), [&](const Kind& t) { return t.id == kind.id; })) continue;
        Kind t;
        t.index = k;
        t.id = kind.id;
        t.temperature = Finite(kind.temperature, 15.f);
        t.humidity = std::clamp(Finite(kind.humidity, 50.f), 0.f, 100.f);
        const int id = kind.id;
        t.table = MakeTable(grid_, [id](int cell) { return cell == id ? 1 : 0; });
        kinds_.push_back(std::move(t));
    }
}

// 長方形 (0, 0) .. (u, v) (マスの単位。塗った所の長方形の角から) の中の値の和: 表の値の間を
// 双線形に補う (マスの中では塗りが一様なので、これがちょうど面積の分になる)。
double PaintTables::Table::Sum(double u, double v) const {
    u = std::clamp(u, 0.0, static_cast<double>(width));
    v = std::clamp(v, 0.0, static_cast<double>(depth));
    const int i = std::clamp(static_cast<int>(std::floor(u)), 0, width - 1), j = std::clamp(static_cast<int>(std::floor(v)), 0, depth - 1);
    const double fu = u - i, fv = v - j;
    const size_t w = static_cast<size_t>(width) + 1;
    const auto at = [&](int a, int b) { return static_cast<double>(sums[static_cast<size_t>(b) * w + static_cast<size_t>(a)]); };
    return at(i, j) * (1 - fu) * (1 - fv) + at(i + 1, j) * fu * (1 - fv) + at(i, j + 1) * (1 - fu) * fv + at(i + 1, j + 1) * fu * fv;
}

double PaintTables::Table::Area(double u0, double v0, double u1, double v1) const {
    if (sums.empty() || width <= 0 || depth <= 0) return 0;
    u0 -= cellX, u1 -= cellX, v0 -= cellZ, v1 -= cellZ;
    if (u1 <= 0 || v1 <= 0 || u0 >= width || v0 >= depth) return 0;
    return Sum(u1, v1) - Sum(u0, v1) - Sum(u1, v0) + Sum(u0, v0);
}

std::vector<float> PaintTables::Weights(Vec2 xz) const {
    std::vector<float> out(kindCount_, 0.f);
    if (kinds_.empty() || !std::isfinite(xz.x) || !std::isfinite(xz.y)) return out;
    if (!(half_ > 0)) {  // 境目の幅 0: その場所のマスだけ
        const int id = grid_.At(FloorCell(xz.x / cellSize_), FloorCell(xz.y / cellSize_));
        for (const Kind& k : kinds_) {
            if (k.id == id) out[k.index] = 1.f;
        }
        return out;
    }
    const double u0 = (xz.x - half_) / cellSize_, u1 = (xz.x + half_) / cellSize_;
    const double v0 = (xz.y - half_) / cellSize_, v1 = (xz.y + half_) / cellSize_;
    const double box = (u1 - u0) * (v1 - v0);
    for (const Kind& k : kinds_) out[k.index] = static_cast<float>(std::clamp(k.table.Area(u0, v0, u1, v1) / box, 0.0, 1.0));
    return out;
}

PaintTables::AmountLayer PaintTables::MakeAmount(PaintGrid grid) {
    AmountLayer a;
    a.grid = std::move(grid);
    if (a.grid.Empty()) return a;
    a.count = MakeTable(a.grid, [](int cell) { return cell > 0 ? 1 : 0; });
    a.sum = MakeTable(a.grid, [](int cell) { return static_cast<int>(CloudsOfCell(cell)); });
    return a;
}

void PaintTables::AmountAt(const AmountLayer& layer, Vec2 xz, float half, float& weight, float& amount) const {
    weight = 0, amount = 0;
    if (layer.count.sums.empty() || !std::isfinite(xz.x) || !std::isfinite(xz.y)) return;
    const float h = std::isfinite(half) && half >= 0 ? half : half_;
    if (!(h > 0)) {
        const int cell = layer.grid.At(FloorCell(xz.x / cellSize_), FloorCell(xz.y / cellSize_));
        if (cell > 0) weight = 1, amount = CloudsOfCell(cell);
        return;
    }
    const double u0 = (xz.x - h) / cellSize_, u1 = (xz.x + h) / cellSize_;
    const double v0 = (xz.y - h) / cellSize_, v1 = (xz.y + h) / cellSize_;
    const double count = layer.count.Area(u0, v0, u1, v1);
    if (!(count > 1e-9)) return;
    weight = static_cast<float>(std::clamp(count / ((u1 - u0) * (v1 - v0)), 0.0, 1.0));
    amount = static_cast<float>(std::clamp(layer.sum.Area(u0, v0, u1, v1) / count, 0.0, 100.0));
}

void PaintTables::Clouds(Vec2 xz, float half, float& weight, float& amount) const { AmountAt(clouds_, xz, half, weight, amount); }
void PaintTables::Rain(Vec2 xz, float half, float& weight, float& amount) const { AmountAt(rain_, xz, half, weight, amount); }

ClimateHere PaintTables::Blend(ClimateHere outside, Vec2 xz) const {
    ClimateHere c = outside;
    Clouds(xz, -1.f, c.cloudWeight, c.cloudAmount);
    Rain(xz, -1.f, c.rainWeight, c.rainAmount);
    if (kinds_.empty()) return c;
    const std::vector<float> w = Weights(xz);
    double total = 0, t = 0, h = 0;
    for (const Kind& k : kinds_) {
        const double wk = w[k.index];
        total += wk;
        t += wk * k.temperature;
        h += wk * k.humidity;
    }
    if (!(total > 0)) return c;
    total = std::min(total, 1.0);
    c.temperature = static_cast<float>(t + (1 - total) * outside.temperature);
    c.humidity = std::clamp(static_cast<float>(h + (1 - total) * outside.humidity), 0.f, 100.f);
    return c;
}

namespace {

// 表を作った地図 (表に効くものだけ比べる)。
bool SameForTables(const ClimateMap& a, const ClimateMap& b) {
    if (a.cellX != b.cellX || a.cellZ != b.cellZ || a.width != b.width || a.depth != b.depth || a.cellSize != b.cellSize || a.border != b.border ||
        a.kinds.size() != b.kinds.size() || a.cells != b.cells || a.cloudX != b.cloudX || a.cloudZ != b.cloudZ || a.cloudWidth != b.cloudWidth ||
        a.cloudDepth != b.cloudDepth || a.cloudCells != b.cloudCells || a.rainX != b.rainX || a.rainZ != b.rainZ || a.rainWidth != b.rainWidth ||
        a.rainDepth != b.rainDepth || a.rainCells != b.rainCells || a.shapes != b.shapes) {
        return false;
    }
    for (size_t i = 0; i < a.kinds.size(); ++i) {
        const ClimateKind &x = a.kinds[i], &y = b.kinds[i];
        if (x.id != y.id || x.temperature != y.temperature || x.humidity != y.humidity) return false;
    }
    return true;
}

}  // namespace

std::shared_ptr<const PaintTables> PaintTablesOf(const ClimateMap& map) {
    const bool painted = (map.width > 0 && map.depth > 0 && !map.cells.empty()) || (map.cloudWidth > 0 && map.cloudDepth > 0 && !map.cloudCells.empty()) ||
                         (map.rainWidth > 0 && map.rainDepth > 0 && !map.rainCells.empty());
    if (!painted && map.shapes.empty()) return nullptr;
    struct Entry {
        ClimateMap map;
        std::shared_ptr<const PaintTables> tables;
    };
    static std::mutex lock;
    static std::vector<Entry> recent;  // 新しく使ったものが前 (4 つまで)
    std::scoped_lock hold(lock);
    for (size_t i = 0; i < recent.size(); ++i) {
        if (!SameForTables(recent[i].map, map)) continue;
        std::rotate(recent.begin(), recent.begin() + static_cast<std::ptrdiff_t>(i), recent.begin() + static_cast<std::ptrdiff_t>(i) + 1);
        return recent.front().tables;
    }
    auto tables = std::make_shared<const PaintTables>(map);
    if (tables->Empty()) tables = nullptr;
    recent.insert(recent.begin(), Entry{map, tables});
    if (recent.size() > 4) recent.pop_back();
    return tables;
}

// --- シーンから ------------------------------------------------------------------------------

ClimateField::ClimateField(const game_production::Scene& scene) {
    if (const ClimateSettings* s = scene.Setting<ClimateSettings>()) settings_ = *s;
    if (const ClimateMap* m = scene.Setting<ClimateMap>()) paint_ = PaintTablesOf(*m);
    const std::string t3 = reflect::TypeOf<space3d::Transform>().name, t2 = reflect::TypeOf<space2d::Transform2D>().name;
    const std::string z3 = reflect::TypeOf<ClimateZone>().name, z2 = reflect::TypeOf<ClimateZone2D>().name;
    struct Place {
        space3d::Mat4 world3;
        space2d::Mat3 world2;
        bool flat = false, placed = false;
    };
    std::unordered_map<int, Place> places;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const game_production::SceneEntity& e = *t.entity;
        const auto up = places.find(e.data.parent);
        Place p = up != places.end() ? up->second : Place{};
        p.placed = false;
        if (const game_production::Component* c = e.FindComponent(t3); c && c->Known()) {
            p.world3 = space3d::WorldMatrix(p.world3, *static_cast<const space3d::Transform*>(c->value.Get()));
            p.flat = false;
            p.placed = true;
        } else if (const game_production::Component* c2 = e.FindComponent(t2); c2 && c2->Known()) {
            p.world2 = space2d::WorldMatrix(p.world2, *static_cast<const space2d::Transform2D*>(c2->value.Get()));
            p.flat = true;
            p.placed = true;
        }
        places[e.data.id] = p;
        if (!t.activeInHierarchy || !p.placed) continue;  // (切ってあるエリア・場所の無いエリアは使わない)
        for (const game_production::Component& comp : e.components) {
            if (!comp.Known()) continue;
            if (!p.flat && comp.type == z3) zones3_.push_back({e.data.id, p.world3, *static_cast<const ClimateZone*>(comp.value.Get())});
            if (p.flat && comp.type == z2) zones2_.push_back({e.data.id, p.world2, *static_cast<const ClimateZone2D*>(comp.value.Get())});
        }
    }
}

ClimateHere ClimateField::At(Vec3 position) const {
    std::vector<ZoneEffect> effects;
    effects.reserve(zones3_.size());
    for (const Zone3D& z : zones3_) {
        const float w = ZoneWeight(ZoneDepth(BodyOf(z.zone, z.world), LocalOf(z.world, position)), z.zone.border);
        if (w > 0) effects.push_back({w, z.zone.priority, z.zone.setTemperature, z.zone.setHumidity, z.zone.temperature, z.zone.humidity});
    }
    ClimateHere outside{settings_.temperature, settings_.humidity};
    if (paint_) outside = paint_->Blend(outside, {position.x, position.z});  // (塗った地図が下の層)
    ClimateHere c = Blend(outside, effects);  // (気候エリアは雲を変えない: 塗った雲のまま)
    c.temperature = CooledByHeight(c.temperature, position.y, settings_);
    return c;
}

void ClimateField::CloudsAt(Vec2 xz, float half, float& weight, float& amount) const {
    weight = 0, amount = 0;
    if (paint_) paint_->Clouds(xz, half, weight, amount);
}

void ClimateField::RainAt(Vec2 xz, float half, float& weight, float& amount) const {
    weight = 0, amount = 0;
    if (paint_) paint_->Rain(xz, half, weight, amount);
}

ClimateHere ClimateField::At2D(Vec2 position) const {
    std::vector<ZoneEffect> effects;
    effects.reserve(zones2_.size());
    for (const Zone2D& z : zones2_) {
        const float w = ZoneWeight(ZoneDepth(BodyOf(z.zone, z.world), LocalOf(z.world, position)), z.zone.border);
        if (w > 0) effects.push_back({w, z.zone.priority, z.zone.setTemperature, z.zone.setHumidity, z.zone.temperature, z.zone.humidity});
    }
    return Blend({settings_.temperature, settings_.humidity}, effects);
}

}  // namespace rm::climate
