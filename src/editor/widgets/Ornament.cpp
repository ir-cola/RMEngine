#include "editor/widgets/Ornament.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

namespace rm::editor::ornament {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDeg = kPi / 180.f;

Vec2 Rotate(Vec2 v, float degreesCcw) {
    // Counter-clockwise on screen (y points down): up (0, -1) goes to the left.
    const float a = degreesCcw * kDeg, cs = std::cos(a), sn = std::sin(a);
    return {v.x * cs + v.y * sn, v.y * cs - v.x * sn};
}
Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 Scale(Vec2 a, float s) { return {a.x * s, a.y * s}; }

// Clockwise on the screen (y down) = a positive area in these coordinates.
float SignedArea(std::span<const Vec2> pts) {
    float a = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2 p = pts[i], q = pts[(i + 1) % pts.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

}  // namespace

float Random::Next() {
    // xorshift32: small, fast, the same numbers for the same seed everywhere.
    if (state == 0) state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
}

// --- Building blocks --------------------------------------------------------------------------

ui::Path FitInto(const ui::Path& path, const ui::Rect& box) {
    if (path.steps.empty()) return path;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    auto grow = [&](Vec2 v) { x0 = std::min(x0, v.x), y0 = std::min(y0, v.y), x1 = std::max(x1, v.x), y1 = std::max(y1, v.y); };
    for (const auto& st : path.steps) {
        grow(st.a);
        if (st.op == ui::Path::Op::Cubic) grow(st.b), grow(st.c);
    }
    const float w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return path;
    const float s = std::min(box.w / w, box.h / h);
    const Vec2 from{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}, to{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
    auto move = [&](Vec2 v) { return Vec2{to.x + (v.x - from.x) * s, to.y + (v.y - from.y) * s}; };
    ui::Path out = path;
    for (auto& st : out.steps) {
        st.a = move(st.a);
        st.b = move(st.b);
        st.c = move(st.c);
    }
    return out;
}

void AddEllipse(ui::Path& p, Vec2 c, float rx, float ry, float degrees) {
    // Four cubic curves (the usual 0.5523 handles), clockwise on the screen: right, down, left, up.
    constexpr float k = 0.5522847f;
    auto at = [&](float x, float y) { return Add(c, Rotate({x, y}, degrees)); };
    p.Begin(at(rx, 0));
    p.Cubic(at(rx, ry * k), at(rx * k, ry), at(0, ry));
    p.Cubic(at(-rx * k, ry), at(-rx, ry * k), at(-rx, 0));
    p.Cubic(at(-rx, -ry * k), at(-rx * k, -ry), at(0, -ry));
    p.Cubic(at(rx * k, -ry), at(rx, -ry * k), at(rx, 0));
}

void AddCircle(ui::Path& p, Vec2 c, float r) { AddEllipse(p, c, r, r, 0); }

void AddPolygon(ui::Path& p, std::span<const Vec2> pts) {
    if (pts.size() < 3) return;
    const bool clockwise = SignedArea(pts) >= 0;
    p.Begin(clockwise ? pts.front() : pts.back());
    for (size_t i = 1; i < pts.size(); ++i) p.Line(clockwise ? pts[i] : pts[pts.size() - 1 - i]);
}

void AddBar(ui::Path& p, Vec2 a, Vec2 b, float wa, float wb) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len <= 0) return;
    const Vec2 n{-dy / len, dx / len};  // across the bar
    const Vec2 quad[4] = {Add(a, Scale(n, wa * 0.5f)), Add(b, Scale(n, wb * 0.5f)), Add(b, Scale(n, -wb * 0.5f)),
                          Add(a, Scale(n, -wa * 0.5f))};
    AddPolygon(p, quad);
}

// --- Spring -----------------------------------------------------------------------------------

ui::Path Sakura(Vec2 center, float radius, float degreesCcw) {
    // One petal pointing up, traced from the picture the user gave (the tips at distance 1): a
    // rounded point near the centre, widest a little past half way, two tips with a V between them.
    ui::Path path;
    for (int i = 0; i < 5; ++i) {
        const float a = degreesCcw + 72.f * static_cast<float>(i);
        auto at = [&](float x, float y) { return Add(center, Scale(Rotate({x, y}, a), radius)); };
        path.Begin(at(0, -0.095f));
        path.Cubic(at(0.17f, -0.17f), at(0.285f, -0.40f), at(0.285f, -0.58f));
        path.Cubic(at(0.285f, -0.78f), at(0.19f, -0.92f), at(0.09f, -0.996f));
        path.Line(at(0, -0.84f));  // the notch
        path.Line(at(-0.09f, -0.996f));
        path.Cubic(at(-0.19f, -0.92f), at(-0.285f, -0.78f), at(-0.285f, -0.58f));
        path.Cubic(at(-0.285f, -0.40f), at(-0.17f, -0.17f), at(0, -0.095f));
    }
    return path;
}

ui::Path CherryTree(const ui::Rect& win, float d) {
    ui::Path p;
    Random rnd{0xC0FFEEu};
    const float right = win.x + win.w, top = win.y, bottom = win.y + win.h;
    // The crown: up top, wide, a little heavier on the left, its top just past the window's top.
    const float rx = d, ry = std::min(std::max(0.25f * win.h, 0.45f * d), 0.8f * d);
    const Vec2 crown{right - 0.62f * d, top + 0.8f * ry};
    const Vec2 fork{right - 0.52f * d, crown.y + ry + 0.1f * d};
    const float foot = right - 0.42f * d;
    // The trunk: a column from the bottom edge up to the fork, bending a little, flaring into roots
    // at its foot.
    constexpr int kSteps = 24;
    std::vector<Vec2> trunk(2 * (kSteps + 1));
    for (int k = 0; k <= kSteps; ++k) {
        const float s = static_cast<float>(k) / kSteps;
        const float x = foot + (fork.x - foot) * s + 0.06f * d * std::sin(kPi * s);
        const float y = bottom + 4.f + (fork.y - bottom - 4.f) * s;
        float half = 0.13f * d * (1.f - 0.45f * s);
        if (s < 0.12f) half *= 1.f + 1.6f * ((0.12f - s) / 0.12f) * ((0.12f - s) / 0.12f);
        trunk[static_cast<size_t>(k)] = {x - half, y};
        trunk[trunk.size() - 1 - static_cast<size_t>(k)] = {x + half, y};
    }
    AddPolygon(p, trunk);
    // The branches: from the fork up and out into the crown, each with a slight elbow.
    const Vec2 ends[] = {{crown.x - 0.72f * rx, crown.y + 0.50f * ry}, {crown.x - 0.30f * rx, crown.y + 0.05f * ry},
                         {crown.x + 0.45f * rx, crown.y + 0.30f * ry}, {crown.x - 0.05f * rx, crown.y - 0.45f * ry},
                         {crown.x + 0.25f * rx, crown.y - 0.15f * ry}};
    const float widths[] = {0.085f, 0.08f, 0.075f, 0.06f, 0.055f};
    for (size_t i = 0; i < std::size(ends); ++i) {
        const float wd = widths[i] * d;
        const Vec2 mid{(fork.x + ends[i].x) * 0.5f + 0.05f * d, (fork.y + ends[i].y) * 0.5f + 0.04f * d};
        AddBar(p, fork, mid, wd, wd * 0.7f);
        AddBar(p, mid, ends[i], wd * 0.7f, 0.02f * d);
    }
    // A low branch off the trunk, reaching left under the crown.
    const Vec2 lowFrom{foot - 0.01f * d, fork.y + 0.30f * (bottom - fork.y)};
    const Vec2 lowTo{right - 1.30f * d, fork.y - 0.10f * d};
    const Vec2 lowMid{(lowFrom.x + lowTo.x) * 0.5f, (lowFrom.y + lowTo.y) * 0.5f + 0.06f * d};
    AddBar(p, lowFrom, lowMid, 0.07f * d, 0.045f * d);
    AddBar(p, lowMid, lowTo, 0.045f * d, 0.015f * d);
    // Blossom: a solid middle, big puffs filling the crown, smaller ones round its edge (fluffy, not
    // bubbly), and a clump on the low branch's end.
    AddEllipse(p, crown, 0.72f * rx, 0.72f * ry, 0);
    for (int i = 0; i < 110; ++i) {
        const float a = rnd.Next() * 2.f * kPi, t = std::sqrt(rnd.Next()) * 0.85f;
        AddCircle(p, {crown.x + std::cos(a) * t * rx, crown.y + std::sin(a) * t * ry}, d * (0.10f + 0.08f * rnd.Next()) * (1.f - 0.4f * t));
    }
    for (int i = 0; i < 70; ++i) {
        const float a = rnd.Next() * 2.f * kPi, t = 0.85f + 0.15f * rnd.Next();
        AddCircle(p, {crown.x + std::cos(a) * t * rx, crown.y + std::sin(a) * t * ry}, d * (0.05f + 0.05f * rnd.Next()));
    }
    for (int i = 0; i < 18; ++i) {
        const float a = rnd.Next() * 2.f * kPi, t = std::sqrt(rnd.Next());
        AddCircle(p, {lowTo.x + 0.05f * d + std::cos(a) * t * 0.26f * d, lowTo.y + std::sin(a) * t * 0.14f * d}, d * (0.05f + 0.04f * rnd.Next()));
    }
    // A few petals falling under it.
    for (int i = 0; i < 12; ++i) {
        const Vec2 c{right - d * (0.2f + 1.4f * rnd.Next()), crown.y + ry + 0.1f * d + (bottom - crown.y - ry) * 0.7f * rnd.Next()};
        AddEllipse(p, c, 0.025f * d, 0.015f * d, rnd.Next() * 180.f);
    }
    return p;
}

ui::Path PetalStream(const ui::Rect& win, float fromX, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    const float span = std::max(fromX - win.x, 1.f);
    for (int i = 0; i < count; ++i) {
        // How far it has flown (0 = at the tree): most of them still near it.
        const float u = std::pow(rnd.Next(), 1.8f);
        const float x = fromX + 20.f * rnd.Next() - span * u;
        // Blown out from the crown (the upper half), sinking as it flies.
        const float y = win.y + win.h * (0.04f + 0.42f * rnd.Next()) + win.h * 0.35f * u;
        if (y > win.y + win.h) continue;
        const float s = size * (0.6f + 0.7f * rnd.Next()) * (1.f - 0.3f * u);
        AddEllipse(p, {x, y}, s * 0.5f, s * 0.3f, rnd.Next() * 180.f);
    }
    return p;
}

ui::Path Petals(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        // More of them near the top-left corner, fewer away from it.
        const float u = rnd.Next(), v = rnd.Next();
        const Vec2 c{area.x + area.w * u * u, area.y + area.h * v * (0.4f + 0.6f * u)};
        const float s = size * (0.6f + 0.7f * rnd.Next());
        AddEllipse(p, c, s * 0.5f, s * 0.3f, rnd.Next() * 180.f);
    }
    return p;
}

// --- Summer -----------------------------------------------------------------------------------

ui::Path Sun(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    AddCircle(p, center, 0.50f * r);
    for (int i = 0; i < 12; ++i) {
        const float a = degreesCcw + static_cast<float>(i) * 30.f;
        const float tip = i % 2 == 0 ? 1.f : 0.80f;  // long and short rays in turn
        const Vec2 ray[3] = {Add(center, Scale(Rotate({-0.10f, -0.58f}, a), r)), Add(center, Scale(Rotate({0, -tip}, a), r)),
                             Add(center, Scale(Rotate({0.10f, -0.58f}, a), r))};
        AddPolygon(p, ray);
    }
    return p;
}

ui::Path Cloud(Vec2 c, float w) {
    ui::Path p;
    AddCircle(p, {c.x - 0.30f * w, c.y + 0.02f * w}, 0.17f * w);
    AddCircle(p, {c.x - 0.10f * w, c.y - 0.09f * w}, 0.24f * w);
    AddCircle(p, {c.x + 0.14f * w, c.y - 0.06f * w}, 0.21f * w);
    AddCircle(p, {c.x + 0.33f * w, c.y + 0.04f * w}, 0.15f * w);
    // A flat foot under the puffs.
    const Vec2 foot[4] = {{c.x - 0.36f * w, c.y + 0.04f * w}, {c.x + 0.42f * w, c.y + 0.04f * w}, {c.x + 0.42f * w, c.y + 0.17f * w},
                          {c.x - 0.36f * w, c.y + 0.17f * w}};
    AddPolygon(p, foot);
    AddCircle(p, {c.x - 0.36f * w, c.y + 0.105f * w}, 0.065f * w);
    AddCircle(p, {c.x + 0.42f * w, c.y + 0.105f * w}, 0.065f * w);
    return p;
}

ui::Path SandCastle(Vec2 base, float h) {
    ui::Path p;
    Random rnd{0x5A2Du};
    auto at = [&](float x, float y) { return Vec2{base.x + x * h, base.y - y * h}; };
    auto wobble = [&](float amount) { return (rnd.Next() - 0.5f) * 2.f * amount; };
    // A lump of packed sand: from x0 to x1 at the ground up to `top`, the sides leaning in (wider at
    // the foot) and bulging unevenly, a few round lumps along the top instead of battlements.
    auto lump = [&](float x0, float x1, float top, int bumps, float lean) {
        std::vector<Vec2> pts;
        const float inset = (x1 - x0) * 0.12f;
        const int sides = 5;
        for (int i = 0; i <= sides; ++i) {  // up the left side
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            pts.push_back(at(x0 + inset * t + lean * t + wobble(0.012f), top * t));
        }
        for (int i = 1; i < 6; ++i) {  // across the top, slightly uneven
            const float t = static_cast<float>(i) / 6.f;
            pts.push_back(at(x0 + inset + lean + (x1 - x0 - 2.f * inset) * t, top + wobble(0.018f)));
        }
        for (int i = sides; i >= 0; --i) {  // down the right side
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            pts.push_back(at(x1 - inset * t + lean * t + wobble(0.012f), top * t));
        }
        AddPolygon(p, pts);
        const float w = x1 - x0 - 2.f * inset;
        for (int i = 0; i < bumps; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bumps);
            AddEllipse(p, at(x0 + inset + lean + w * t, top + 0.01f), w / static_cast<float>(bumps) * 0.42f * h,
                       (0.05f + 0.02f * rnd.Next()) * h, wobble(12.f));
        }
    };
    lump(-0.62f, 0.62f, 0.36f, 5, 0.f);       // the wall
    lump(-0.70f, -0.34f, 0.62f, 2, 0.02f);    // the towers, leaning a little
    lump(0.36f, 0.70f, 0.56f, 2, -0.025f);
    lump(-0.19f, 0.19f, 0.86f, 3, 0.015f);    // the keep
    AddEllipse(p, at(0.f, 0.86f + 0.07f), 0.07f * h, 0.06f * h, 0);  // a round knob on its top
    // The heap of sand at its foot (it sits in the beach, not on it).
    AddEllipse(p, at(0.f, 0.02f), 0.86f * h, 0.12f * h, 0);
    return p;
}

ui::Path BeachParasol(Vec2 base, float h, float degreesCcw) {
    ui::Path p;
    auto at = [&](float x, float y) { return Add(base, Rotate({x * h, -y * h}, degreesCcw)); };
    AddBar(p, at(0, -0.05f), at(0, 1.02f), 0.035f * h, 0.03f * h);
    // The canopy: a flat dome, its rim scalloped between eight ribs.
    const Vec2 top = at(0, 1.0f);
    const float R = 0.62f, rise = 0.32f;
    for (int i = 0; i < 8; ++i) {
        std::vector<Vec2> wedge{top};
        for (int k = 0; k <= 4; ++k) {
            const float t = (static_cast<float>(i) + static_cast<float>(k) / 4.f) / 8.f;  // 0 .. 1, left to right
            const float ang = kPi * (1.f - t);                                          // 180 .. 0 degrees
            const float sag = 0.04f * std::sin(static_cast<float>(k) / 4.f * kPi);
            wedge.push_back(at(std::cos(ang) * R, 1.0f - rise + std::sin(ang) * rise * 0.15f - sag));
        }
        AddPolygon(p, wedge);
    }
    AddCircle(p, top, 0.035f * h);
    return p;
}

ui::Path BeachScene(Vec2 base, float h) {
    ui::Path p;
    // The mound: a wide, low hill whose foot is the window's bottom edge (half of it below).
    AddEllipse(p, {base.x, base.y + 0.04f * h}, 0.78f * h, 0.17f * h, 0);
    AddEllipse(p, {base.x + 0.42f * h, base.y + 0.02f * h}, 0.42f * h, 0.12f * h, -4.f);
    // The parasol stuck in it, leaning to the left; the sand castle on the left of the mound.
    ui::Path parasol = BeachParasol({base.x + 0.18f * h, base.y - 0.08f * h}, 0.86f * h, 12.f);
    p.steps.insert(p.steps.end(), parasol.steps.begin(), parasol.steps.end());
    ui::Path castle = SandCastle({base.x - 0.36f * h, base.y - 0.09f * h}, 0.30f * h);
    p.steps.insert(p.steps.end(), castle.steps.begin(), castle.steps.end());
    return p;
}

// --- Fall -------------------------------------------------------------------------------------

ui::Path MapleLeaf(Vec2 center, float r, float degreesCcw, int points) {
    // Five pointed lobes (straight up, two up-and-out, two down-and-out), small teeth on their edges,
    // deep notches between them; the stalk hangs below.
    struct Lobe {
        float angle, length, halfWidth;  // degrees on screen (270 = up)
    };
    static constexpr Lobe kLobes[] = {{270, 1.0f, 36}, {208, 0.86f, 32}, {332, 0.86f, 32}, {150, 0.56f, 28}, {30, 0.56f, 28}};
    std::vector<Vec2> outline;
    points = std::max(points, 24);
    for (int i = 0; i < points; ++i) {
        const float a = 360.f * static_cast<float>(i) / static_cast<float>(points);
        float rad = 0.30f;  // the middle of the leaf
        for (const Lobe& l : kLobes) {
            float d = std::fabs(a - l.angle);
            d = std::min(d, 360.f - d);
            if (d >= l.halfWidth) continue;
            const float t = 1.f - d / l.halfWidth;  // 1 at the tip
            const float teeth = 1.f + 0.07f * std::fabs(std::sin(d * kDeg * 22.f)) * (1.f - t);
            rad = std::max(rad, (0.30f + (l.length - 0.30f) * std::pow(t, 1.25f)) * teeth);
        }
        outline.push_back(Add(center, Rotate({std::cos(a * kDeg) * rad * r, std::sin(a * kDeg) * rad * r}, degreesCcw)));
    }
    ui::Path p;
    AddPolygon(p, outline);
    AddBar(p, Add(center, Rotate({0, 0.1f * r}, degreesCcw)), Add(center, Rotate({0.05f * r, 0.62f * r}, degreesCcw)), 0.06f * r,
           0.035f * r);
    return p;
}

std::array<ui::Path, 4> Canopy(const ui::Rect& win, float depth, float perPixel, uint32_t seed) {
    std::array<ui::Path, 4> out;
    Random rnd{seed};
    // Thick along the very top, thinning out below (a few drift all the way down to `depth`).
    const int count = std::max(12, static_cast<int>(win.w * perPixel));
    for (int i = 0; i < count; ++i) {
        const float x = win.x - 10.f + (win.w + 20.f) * rnd.Next();
        const float v = rnd.Next();
        const float y = win.y - 6.f + depth * std::pow(v, 2.2f);
        const float size = (7.f + 9.f * rnd.Next()) * (1.f - 0.35f * v);
        const int color = static_cast<int>(rnd.Next() * 4.f) % 4;
        const ui::Path leaf = MapleLeaf({x, y}, size, rnd.Next() * 360.f, 40);
        auto& dst = out[static_cast<size_t>(color)].steps;
        dst.insert(dst.end(), leaf.steps.begin(), leaf.steps.end());
    }
    return out;
}

// --- Winter -----------------------------------------------------------------------------------

ui::Path Snowflake(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    for (int i = 0; i < 6; ++i) {
        const float a = degreesCcw + 60.f * static_cast<float>(i);
        auto at = [&](float x, float y) { return Add(center, Scale(Rotate({x, y}, a), r)); };
        AddBar(p, at(0, 0), at(0, -1.f), 0.10f * r, 0.06f * r);
        // Two pairs of side branches on each arm, and a small fork at its tip.
        for (const auto& [along, len] : {std::pair{0.42f, 0.30f}, std::pair{0.68f, 0.22f}}) {
            AddBar(p, at(0, -along), at(-len * 0.75f, -along - len * 0.75f), 0.07f * r, 0.045f * r);
            AddBar(p, at(0, -along), at(len * 0.75f, -along - len * 0.75f), 0.07f * r, 0.045f * r);
        }
        AddCircle(p, at(0, -1.f), 0.06f * r);
    }
    // The heart: a small hexagon.
    std::vector<Vec2> hex;
    for (int i = 0; i < 6; ++i) hex.push_back(Add(center, Scale(Rotate({0, -0.2f}, degreesCcw + 60.f * static_cast<float>(i) + 30.f), r)));
    AddPolygon(p, hex);
    return p;
}

ui::Path Snowfall(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const Vec2 c{area.x + area.w * rnd.Next(), area.y + area.h * rnd.Next()};
        AddCircle(p, c, size * (0.35f + 0.65f * rnd.Next()) * 0.5f);
    }
    return p;
}

ui::Path SnowDrift(const ui::Rect& r, float depth, float corner) {
    ui::Path p;
    // In the box's own coordinates (from its left edge): the same drift wherever it is.
    const float l0 = corner * 0.6f, l1 = r.w - corner * 0.6f;
    if (l1 - l0 < 6.f || depth <= 0) return p;
    const float bottom = r.y + r.h;
    constexpr float kSlope = 26.f;  // the drift slopes down to nothing over this many pixels at each end
    auto top = [&](float l) {
        const float ends = std::min(1.f, std::min(l - l0, l1 - l) / kSlope);
        const float swells = 0.62f + 0.22f * std::sin(l * 0.045f) + 0.16f * std::sin(l * 0.13f + 1.f);
        return Vec2{r.x + l, bottom - depth * std::max(0.f, ends) * swells};
    };
    // Clockwise on the screen: along the drift's top to the right (a point every 4 pixels from the
    // left, so the left part never changes with the width), then back along the bottom.
    p.Begin(top(l0));
    for (int i = 1; l0 + 4.f * static_cast<float>(i) < l1; ++i) p.Line(top(l0 + 4.f * static_cast<float>(i)));
    p.Line(top(l1));
    p.Line({r.x + l1, bottom});
    p.Line({r.x + l0, bottom});
    return p;
}

IcicleShapes Icicles(const ui::Rect& win, float maxLength, uint32_t seed) {
    IcicleShapes out;
    Random rnd{seed};
    const float top = win.y, right = win.x + win.w;
    // A small group every 260 .. 640 pixels from the left edge; every group that starts inside the
    // window is made (the painter cuts one that runs past the edge), so none pops in or out.
    for (float gx = win.x + 60.f + 160.f * rnd.Next(); gx < right; gx += 260.f + 380.f * rnd.Next()) {
        const int count = 1 + static_cast<int>(rnd.Next() * 3.f) % 3;
        const float groupW = 6.f + 10.f * static_cast<float>(count);
        // The lump of snow they hang from, with a smaller one on it.
        AddEllipse(out.snow, {gx + groupW * 0.5f - 4.f, top + 1.f}, groupW * 0.5f + 5.f, 5.f, 0);
        AddEllipse(out.snow, {gx + groupW * 0.35f - 4.f, top + 3.5f}, groupW * 0.22f + 3.f, 3.f, 0);
        for (int i = 0; i < count; ++i) {
            const float x = gx + 10.f * static_cast<float>(i) + 3.f * rnd.Next();
            const float len = maxLength * (i == count / 2 ? 0.7f + 0.3f * rnd.Next() : 0.3f + 0.35f * rnd.Next());
            const float half = 3.2f + 3.0f * rnd.Next() * (len / maxLength + 0.3f);
            const float lean = (rnd.Next() - 0.5f) * 3.f;  // the tip a little to one side
            const float phase = rnd.Next() * 6.28f;
            const float y0 = top + 2.f;
            // Its outline: down the right side and up the left, the width shrinking to a sharp tip
            // with small ripples (where the water froze in rings), the whole bending towards `lean`.
            std::vector<Vec2> pts;
            constexpr int kSteps = 14;
            auto at = [&](float t, float side) {
                const float width = half * std::pow(1.f - t, 0.85f) * (1.f + 0.13f * std::sin(t * 17.f + phase) * (1.f - t));
                return Vec2{x + lean * t * t + side * width, y0 + len * t};
            };
            for (int k = 0; k <= kSteps; ++k) pts.push_back(at(static_cast<float>(k) / kSteps, 1.f));
            for (int k = kSteps - 1; k >= 0; --k) pts.push_back(at(static_cast<float>(k) / kSteps, -1.f));
            AddPolygon(out.ice, pts);
            // The shine: a thin bright stripe down the left side, fading out before the tip.
            std::vector<Vec2> shine;
            for (int k = 1; k <= 9; ++k) {
                const float t = static_cast<float>(k) / 14.f;
                const Vec2 edge = at(t, -1.f);
                shine.push_back({edge.x + half * 0.25f, edge.y});
            }
            for (int k = 9; k >= 1; --k) {
                const float t = static_cast<float>(k) / 14.f;
                const Vec2 edge = at(t, -1.f);
                shine.push_back({edge.x + half * (0.25f + 0.35f * (1.f - t)), edge.y});
            }
            AddPolygon(out.shine, shine);
        }
    }
    return out;
}

}  // namespace rm::editor::ornament
