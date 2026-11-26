#include "rm_engine/animation/runtime/Blend.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace rm::animation {

namespace {

bool Finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

double Cross(Vec2 o, Vec2 a, Vec2 b) {
    return (static_cast<double>(a.x) - o.x) * (static_cast<double>(b.y) - o.y) - (static_cast<double>(a.y) - o.y) * (static_cast<double>(b.x) - o.x);
}

// 点 p が三角形 (a, b, c) の外接円の中か。
bool InCircumcircle(Vec2 a, Vec2 b, Vec2 c, Vec2 p) {
    const double ax = a.x - p.x, ay = a.y - p.y, bx = b.x - p.x, by = b.y - p.y, cx = c.x - p.x, cy = c.y - p.y;
    const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) + (cx * cx + cy * cy) * (ax * by - bx * ay);
    return Cross(a, b, c) > 0 ? det > 1e-12 : det < -1e-12;
}

// 線分 ab の上の、p に一番近い所の割合 (0: a .. 1: b)。
float ClosestOnSegment(Vec2 a, Vec2 b, Vec2 p) {
    const double dx = static_cast<double>(b.x) - a.x, dy = static_cast<double>(b.y) - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0) return 0.f;
    const double t = ((static_cast<double>(p.x) - a.x) * dx + (static_cast<double>(p.y) - a.y) * dy) / len2;
    return static_cast<float>(std::clamp(t, 0.0, 1.0));
}

}  // namespace

void BlendWeights1D(std::span<const float> points, float x, std::vector<float>& out) {
    out.assign(points.size(), 0.f);
    if (points.empty()) return;
    if (!std::isfinite(x)) x = 0;
    std::vector<size_t> order(points.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const float pa = std::isfinite(points[a]) ? points[a] : 0.f, pb = std::isfinite(points[b]) ? points[b] : 0.f;
        return pa < pb;
    });
    auto at = [&](size_t k) { return std::isfinite(points[order[k]]) ? points[order[k]] : 0.f; };
    if (x <= at(0)) {
        out[order[0]] = 1;
        return;
    }
    if (x >= at(order.size() - 1)) {
        // (同じ値が並ぶときは前の点: 一番大きい値の最初のもの)
        size_t k = order.size() - 1;
        while (k > 0 && at(k - 1) == at(k)) --k;
        out[order[k]] = 1;
        return;
    }
    for (size_t k = 0; k + 1 < order.size(); ++k) {
        const float a = at(k), b = at(k + 1);
        if (x < a || x > b) continue;
        if (b - a <= 0) {
            out[order[k]] = 1;
            return;
        }
        const float t = (x - a) / (b - a);
        out[order[k]] = 1 - t;
        out[order[k + 1]] += t;
        return;
    }
}

std::vector<std::array<int, 3>> Triangulate(std::span<const Vec2> points) {
    std::vector<std::array<int, 3>> tris;
    const int n = static_cast<int>(points.size());
    if (n < 3) return tris;
    // 全部を囲む大きな三角形から始め、点を 1 つずつ足す (外接円に入った三角形を消し、できた穴の縁と点で張り直す)。
    float lo_x = std::numeric_limits<float>::max(), lo_y = lo_x, hi_x = -lo_x, hi_y = -lo_x;
    for (const Vec2& p : points) {
        if (!Finite(p)) return tris;
        lo_x = std::min(lo_x, p.x), lo_y = std::min(lo_y, p.y), hi_x = std::max(hi_x, p.x), hi_y = std::max(hi_y, p.y);
    }
    const float size = std::max({hi_x - lo_x, hi_y - lo_y, 1e-3f}) * 20.f;
    const Vec2 mid{(lo_x + hi_x) * 0.5f, (lo_y + hi_y) * 0.5f};
    std::vector<Vec2> pts(points.begin(), points.end());
    pts.push_back({mid.x - size, mid.y - size});
    pts.push_back({mid.x + size, mid.y - size});
    pts.push_back({mid.x, mid.y + size});
    std::vector<std::array<int, 3>> work{{n, n + 1, n + 2}};
    for (int i = 0; i < n; ++i) {
        const Vec2 p = pts[static_cast<size_t>(i)];
        // 同じ場所の点は足さない (前の点が使われる)。
        bool same = false;
        for (int j = 0; j < i; ++j) same = same || (pts[static_cast<size_t>(j)].x == p.x && pts[static_cast<size_t>(j)].y == p.y);
        if (same) continue;
        std::vector<std::array<int, 2>> edges;
        std::vector<std::array<int, 3>> keep;
        for (const auto& t : work) {
            if (InCircumcircle(pts[static_cast<size_t>(t[0])], pts[static_cast<size_t>(t[1])], pts[static_cast<size_t>(t[2])], p)) {
                for (int e = 0; e < 3; ++e) edges.push_back({t[static_cast<size_t>(e)], t[static_cast<size_t>((e + 1) % 3)]});
            } else {
                keep.push_back(t);
            }
        }
        // 穴の縁 = 1 度だけ出てきた辺。
        for (size_t a = 0; a < edges.size(); ++a) {
            bool shared = false;
            for (size_t b = 0; b < edges.size(); ++b) {
                if (a != b && edges[a][0] == edges[b][1] && edges[a][1] == edges[b][0]) shared = true;
            }
            if (shared) continue;
            std::array<int, 3> t{edges[a][0], edges[a][1], i};
            if (Cross(pts[static_cast<size_t>(t[0])], pts[static_cast<size_t>(t[1])], pts[static_cast<size_t>(t[2])]) < 0) std::swap(t[0], t[1]);
            if (std::fabs(Cross(pts[static_cast<size_t>(t[0])], pts[static_cast<size_t>(t[1])], pts[static_cast<size_t>(t[2])])) > 1e-12) keep.push_back(t);
        }
        work = std::move(keep);
    }
    for (const auto& t : work) {
        if (t[0] < n && t[1] < n && t[2] < n) tris.push_back(t);
    }
    return tris;
}

void BlendWeights2D(std::span<const Vec2> points, std::span<const std::array<int, 3>> triangles, Vec2 p, std::vector<float>& out) {
    out.assign(points.size(), 0.f);
    if (points.empty()) return;
    if (!Finite(p)) p = {0, 0};
    if (points.size() == 1) {
        out[0] = 1;
        return;
    }
    // 三角形の中: 重心座標。
    for (const auto& t : triangles) {
        const Vec2 a = points[static_cast<size_t>(t[0])], b = points[static_cast<size_t>(t[1])], c = points[static_cast<size_t>(t[2])];
        const double area = Cross(a, b, c);
        if (std::fabs(area) < 1e-12) continue;
        const double wa = Cross(b, c, p) / area, wb = Cross(c, a, p) / area, wc = Cross(a, b, p) / area;
        if (wa >= -1e-6 && wb >= -1e-6 && wc >= -1e-6) {
            const double sum = std::max(wa, 0.0) + std::max(wb, 0.0) + std::max(wc, 0.0);
            out[static_cast<size_t>(t[0])] = static_cast<float>(std::max(wa, 0.0) / sum);
            out[static_cast<size_t>(t[1])] = static_cast<float>(std::max(wb, 0.0) / sum);
            out[static_cast<size_t>(t[2])] = static_cast<float>(std::max(wc, 0.0) / sum);
            return;
        }
    }
    // 三角形が無い (点が一直線・2 つだけ): 一番離れた 2 点の向きに写して 1D で。
    if (triangles.empty()) {
        size_t fa = 0, fb = 0;
        double far2 = -1;
        for (size_t a = 0; a < points.size(); ++a) {
            for (size_t b = a + 1; b < points.size(); ++b) {
                const double dx = static_cast<double>(points[b].x) - points[a].x, dy = static_cast<double>(points[b].y) - points[a].y;
                if (dx * dx + dy * dy > far2) far2 = dx * dx + dy * dy, fa = a, fb = b;
            }
        }
        const double dx = static_cast<double>(points[fb].x) - points[fa].x, dy = static_cast<double>(points[fb].y) - points[fa].y;
        const double len = std::sqrt(std::max(far2, 0.0));
        std::vector<float> along(points.size(), 0.f);
        auto project = [&](Vec2 q) { return len > 0 ? static_cast<float>(((q.x - points[fa].x) * dx + (q.y - points[fa].y) * dy) / len) : 0.f; };
        for (size_t k = 0; k < points.size(); ++k) along[k] = Finite(points[k]) ? project(points[k]) : 0.f;
        BlendWeights1D(along, project(p), out);
        return;
    }
    // 外: 一番近い三角形の辺の、一番近い所で 2 点を混ぜる。
    double best = std::numeric_limits<double>::max();
    int bestA = 0, bestB = 0;
    float bestT = 0;
    auto tryEdge = [&](int ia, int ib) {
        const Vec2 a = points[static_cast<size_t>(ia)], b = points[static_cast<size_t>(ib)];
        if (!Finite(a) || !Finite(b)) return;
        const float t = ClosestOnSegment(a, b, p);
        const double qx = a.x + (static_cast<double>(b.x) - a.x) * t, qy = a.y + (static_cast<double>(b.y) - a.y) * t;
        const double d = (qx - p.x) * (qx - p.x) + (qy - p.y) * (qy - p.y);
        if (d < best - 1e-12) best = d, bestA = ia, bestB = ib, bestT = t;
    };
    for (const auto& t : triangles) {
        for (int e = 0; e < 3; ++e) tryEdge(t[static_cast<size_t>(e)], t[static_cast<size_t>((e + 1) % 3)]);
    }
    out[static_cast<size_t>(bestA)] += 1 - bestT;
    out[static_cast<size_t>(bestB)] += bestT;
}

}  // namespace rm::animation
