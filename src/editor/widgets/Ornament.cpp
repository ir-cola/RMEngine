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

ui::Path CherryTree(Vec2 base, float h) {
    ui::Path p;
    auto at = [&](float x, float y) { return Vec2{base.x + x * h, base.y - y * h}; };  // y = up, in heights
    // The trunk, a little bent, flaring into roots at the foot.
    const Vec2 trunk[] = {at(-0.13f, 0.f),   at(-0.07f, 0.03f), at(-0.05f, 0.14f), at(-0.065f, 0.28f), at(-0.04f, 0.38f),
                          at(0.045f, 0.40f), at(0.06f, 0.28f),  at(0.045f, 0.15f), at(0.07f, 0.03f),   at(0.14f, 0.f)};
    AddPolygon(p, trunk);
    // The branches: from the fork up and out into the crown (the left one low, as in the picture).
    AddBar(p, at(-0.02f, 0.36f), at(-0.24f, 0.58f), 0.07f * h, 0.022f * h);
    AddBar(p, at(0.02f, 0.37f), at(0.24f, 0.58f), 0.065f * h, 0.02f * h);
    AddBar(p, at(0.f, 0.38f), at(0.04f, 0.70f), 0.055f * h, 0.018f * h);
    AddBar(p, at(-0.045f, 0.24f), at(-0.38f, 0.40f), 0.05f * h, 0.015f * h);
    // The crown: a wide, flat-topped cloud of blossom made of many small puffs (fluffy, not bubbly),
    // heavier on the left and hanging low there.
    Random rnd{0xC0FFEEu};
    const Vec2 crown = at(-0.03f, 0.64f);
    const float hw = 0.62f * h, hh = 0.21f * h;
    for (int i = 0; i < 110; ++i) {
        const float a = rnd.Next() * 2.f * kPi, d = std::sqrt(rnd.Next());
        const Vec2 c{crown.x + std::cos(a) * d * hw, crown.y + std::sin(a) * d * hh};
        AddCircle(p, c, (0.045f + 0.05f * rnd.Next()) * h * (1.f - 0.3f * d));
    }
    for (int i = 0; i < 26; ++i) {
        const float t = rnd.Next();
        AddCircle(p, at(-0.66f + 0.40f * t + 0.04f * rnd.Next(), 0.40f + 0.12f * std::sin(t * kPi) + 0.06f * rnd.Next()),
                  (0.04f + 0.035f * rnd.Next()) * h);
    }
    // A few petals falling around it.
    for (int i = 0; i < 14; ++i) {
        const Vec2 c = at(-0.75f + 1.5f * rnd.Next(), 0.04f + 0.36f * rnd.Next());
        AddEllipse(p, c, 0.012f * h, 0.007f * h, rnd.Next() * 180.f);
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

ui::Path Sunflower(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    // Fourteen long petals with gaps between their tips, around a big round heart (so it reads as a
    // flower, not a cog), on a stem with a leaf.
    for (int i = 0; i < 14; ++i) {
        const float a = degreesCcw + static_cast<float>(i) * (360.f / 14.f);
        AddEllipse(p, Add(center, Scale(Rotate({0, -0.68f}, a), r)), 0.115f * r, 0.31f * r, a);
    }
    AddCircle(p, center, 0.40f * r);
    auto at = [&](float x, float y) { return Add(center, Scale(Rotate({x, y}, degreesCcw), r)); };
    AddBar(p, at(0, 0.3f), at(0.08f, 1.45f), 0.11f * r, 0.08f * r);
    AddEllipse(p, at(0.30f, 1.05f), 0.24f * r, 0.09f * r, degreesCcw + 30.f);
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

float ShoreY(const ui::Rect& win, float width, float height, float x) {
    // From (right - width, bottom) to (right, bottom - height), bowed a little towards the sea.
    const float t = std::clamp((x - (win.x + win.w - width)) / std::max(width, 1.f), 0.f, 1.f);
    const float bow = 0.10f * std::sin(t * kPi);
    return win.y + win.h - height * std::min(1.f, t + bow);
}

Beach BeachCorner(const ui::Rect& win, float width, float height) {
    Beach b;
    const float right = win.x + win.w, bottom = win.y + win.h;
    std::vector<Vec2> sand{{right - width, bottom}};
    std::vector<Vec2> foamTop, foamUnder;
    const int steps = 40;
    for (int i = 0; i <= steps; ++i) {
        const float x = right - width + width * static_cast<float>(i) / static_cast<float>(steps);
        const float y = ShoreY(win, width, height, x);
        sand.push_back({x, y});
        // The foam: a thin, slightly uneven band just on the sea's side of the shore.
        const float f = 3.f + 1.5f * std::sin(x * 0.09f);
        foamTop.push_back({x - f * 0.4f, y - f});
        foamUnder.push_back({x, y + 1.f});
    }
    sand.push_back({right, bottom});
    AddPolygon(b.sand, sand);
    foamTop.insert(foamTop.end(), foamUnder.rbegin(), foamUnder.rend());
    AddPolygon(b.foam, foamTop);
    return b;
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

Parasol BeachParasol(Vec2 base, float h, float degreesCcw) {
    Parasol s;
    auto at = [&](float x, float y) { return Add(base, Rotate({x * h, -y * h}, degreesCcw)); };
    AddBar(s.pole, at(0, 0), at(0, 1.02f), 0.035f * h, 0.03f * h);
    // The canopy: a flat dome cut into eight wedges, two colors in turn.
    const Vec2 top = at(0, 1.0f);
    const float R = 0.62f, rise = 0.32f;
    for (int i = 0; i < 8; ++i) {
        std::vector<Vec2> wedge{top};
        for (int k = 0; k <= 4; ++k) {
            const float t = (static_cast<float>(i) + static_cast<float>(k) / 4.f) / 8.f;  // 0 .. 1, left to right
            const float ang = kPi * (1.f - t);                                          // 180 .. 0 degrees
            // A dome: wide and low, its rim scalloped between the ribs.
            const float sag = 0.04f * std::sin(static_cast<float>(k) / 4.f * kPi);
            wedge.push_back(at(std::cos(ang) * R, 1.0f - rise + std::sin(ang) * rise * 0.15f - sag));
        }
        AddPolygon(i % 2 == 0 ? s.stripesA : s.stripesB, wedge);
    }
    // The cap on the top.
    AddCircle(s.pole, top, 0.035f * h);
    return s;
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

ui::Path Icicles(const ui::Rect& win, float maxLength, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    const float top = win.y, right = win.x + win.w;
    // A small group every 260 .. 640 pixels; within it, one to three icicles of different lengths.
    for (float gx = win.x + 60.f + 160.f * rnd.Next(); gx < right - 20.f; gx += 260.f + 380.f * rnd.Next()) {
        const int count = 1 + static_cast<int>(rnd.Next() * 3.f) % 3;
        const float groupW = 6.f + 9.f * static_cast<float>(count);
        AddEllipse(p, {gx + groupW * 0.5f - 4.f, top + 1.f}, groupW * 0.5f + 4.f, 4.5f, 0);  // the lump of snow
        for (int i = 0; i < count; ++i) {
            const float x = gx + 9.f * static_cast<float>(i) + 3.f * rnd.Next();
            const float len = maxLength * (i == count / 2 ? 0.7f + 0.3f * rnd.Next() : 0.3f + 0.35f * rnd.Next());
            const float half = 2.4f + 2.2f * rnd.Next() * (len / maxLength + 0.3f);
            const float lean = (rnd.Next() - 0.5f) * 2.f;  // the tip a little to one side
            const float y0 = top + 2.f;
            p.Begin({x - half, y0});
            p.Line({x + half, y0});
            p.Cubic({x + half * 0.8f, y0 + len * 0.35f}, {x + lean + half * 0.25f, y0 + len * 0.8f}, {x + lean, y0 + len});
            p.Cubic({x + lean - half * 0.25f, y0 + len * 0.8f}, {x - half * 0.8f, y0 + len * 0.35f}, {x - half, y0});
        }
    }
    return p;
}

}  // namespace rm::editor::ornament
