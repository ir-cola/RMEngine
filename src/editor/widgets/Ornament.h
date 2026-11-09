#pragma once
// The shapes of theme ornaments (the seasons' pictures: a cherry tree, a sunflower, a maple leaf, a
// snowflake, the sun, clouds, snow on an edge ...). Pure: they only make ui::Paths (tested in
// tests/TestTheme.cpp); w::DrawOrnament / DrawFloatOrnament / the snow in the widgets draw them in
// the theme's colors.
//
// Every figure goes round the same way (clockwise on the screen) and the painter fills by winding,
// so figures that overlap (the circles of a tree's crown) make one silhouette, never holes.
// "Random" scatters come from a fixed seed: the same picture every frame.

#include "ui/Path.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <span>

namespace rm::editor::ornament {

// --- Building blocks --------------------------------------------------------------------------
void AddCircle(ui::Path& p, Vec2 center, float r);
void AddEllipse(ui::Path& p, Vec2 center, float rx, float ry, float degrees);
// A closed polygon, put clockwise whichever way the points were given.
void AddPolygon(ui::Path& p, std::span<const Vec2> points);
// A bar from `a` to `b`, `wa` wide at a and `wb` wide at b (a branch, an arm of a snowflake).
void AddBar(ui::Path& p, Vec2 a, Vec2 b, float wa, float wb);

// Small repeatable random numbers (0 .. 1) from a seed.
struct Random {
    uint32_t state;
    float Next();
};

// --- Spring -----------------------------------------------------------------------------------
// A cherry blossom, filled as one silhouette: five petals, each with a notch at its tip. Before
// turning, one petal points straight up. radius = from the centre to the petals' tips;
// degreesCcw = turned counter-clockwise as seen on screen.
ui::Path Sakura(Vec2 center, float radius, float degreesCcw);
// A cherry tree in full bloom (after the user's picture): a trunk that forks into branches under a
// wide crown, a few petals falling around it. `base` = the foot of the trunk, `height` = to the top.
ui::Path CherryTree(Vec2 base, float height);
// Small petals scattered in `area` (a flurry), each about `size` long.
ui::Path Petals(const ui::Rect& area, int count, float size, uint32_t seed);

// --- Summer -----------------------------------------------------------------------------------
ui::Path Sunflower(Vec2 center, float radius, float degreesCcw);
ui::Path Sun(Vec2 center, float radius);  // a disc and its rays (radius = to the rays' tips)
ui::Path Cloud(Vec2 center, float width);
// The beach along the bottom of `window`, `height` tall at most, its edge gently waving.
ui::Path Sand(const ui::Rect& window, float height);
// A sand castle standing on `base` (the middle of its foot), and the little flag on its top tower.
struct Castle {
    ui::Path walls, flag;
};
Castle SandCastle(Vec2 base, float height);
// A beach parasol stuck in the sand at `base`, leaning: its canopy in stripes of two colors, its pole.
struct Parasol {
    ui::Path stripesA, stripesB, pole;
};
Parasol BeachParasol(Vec2 base, float height, float degreesCcw);

// --- Fall -------------------------------------------------------------------------------------
// A maple leaf (五つの尖った葉先と葉柄): radius = from the centre to the top tip. `points` = how
// finely its edge is drawn (fewer for the small leaves of the canopy).
ui::Path MapleLeaf(Vec2 center, float radius, float degreesCcw, int points = 180);
// Autumn leaves thick along the top of `window`, down to about `depth`, in four colors (one path each).
std::array<ui::Path, 4> Canopy(const ui::Rect& window, float depth, uint32_t seed);
// A wood along the bottom of `window`: trunks and round crowns.
struct Wood {
    ui::Path trunks, crowns;
};
Wood Forest(const ui::Rect& window, float height, uint32_t seed);

// --- Winter -----------------------------------------------------------------------------------
ui::Path Snowflake(Vec2 center, float radius, float degreesCcw);
// Snow lying on the top edge of `r` (a panel, a tab, a dialog): a soft wavy heap at most `depth`
// high above the edge, a little over it, with a few drops hanging. Kept off the rounded corners.
ui::Path SnowCap(const ui::Rect& r, float depth, float cornerRadius);

}  // namespace rm::editor::ornament
