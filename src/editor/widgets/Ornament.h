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

// The path moved and scaled (the same in both directions) to sit centred in `box` as large as it
// fits: the corner silhouettes of every season fill the same box, whatever their own shape.
ui::Path FitInto(const ui::Path& path, const ui::Rect& box);

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
// A cherry tree in full bloom standing along the whole right edge of `window` (its trunk mostly
// outside it), its branches reaching up to `depth` into the window at several heights, heavy with
// blossom; the left edge of its blossom is about `depth` from the window's right edge.
ui::Path CherryEdge(const ui::Rect& window, float depth);
// Petals blown off that tree to the left: thick near it (`fromX` = its left edge), thinning out and
// drifting down a little as they fly across the window.
ui::Path PetalStream(const ui::Rect& window, float fromX, int count, float size, uint32_t seed);
// Small petals scattered in `area` (a flurry), each about `size` long.
ui::Path Petals(const ui::Rect& area, int count, float size, uint32_t seed);

// --- Summer -----------------------------------------------------------------------------------
// The sun: a round disc and twelve pointed rays around it, long and short in turn (radius = to the
// long rays' tips), turned `degreesCcw`.
ui::Path Sun(Vec2 center, float radius, float degreesCcw);
ui::Path Cloud(Vec2 center, float width);
// A sand castle standing on `base` (the middle of its foot), made by hand: a wall and three towers
// that lean and bulge a little, round lumps on their tops, a heap of sand at its foot (so, filled
// together with the beach, it grows out of the sand).
ui::Path SandCastle(Vec2 base, float height);
// A beach parasol stuck in the sand at `base`, leaning: its canopy (a scalloped dome) and its pole.
ui::Path BeachParasol(Vec2 base, float height, float degreesCcw);
// The summer silhouette for the bottom-right corner (like spring's cherry tree): a low mound of sand
// with the parasol stuck in it and the sand castle beside it. `base` = the middle of the mound's
// foot (on the window's bottom edge), `height` = to the top of the parasol.
ui::Path BeachScene(Vec2 base, float height);

// --- Fall -------------------------------------------------------------------------------------
// A maple leaf (五つの尖った葉先と葉柄): radius = from the centre to the top tip. `points` = how
// finely its edge is drawn (fewer for the small leaves of the canopy).
ui::Path MapleLeaf(Vec2 center, float radius, float degreesCcw, int points = 180);
// Autumn leaves thick along the top of `window` and thinning out down to about `depth`, in four
// colors (one path each). `perPixel` = leaves per pixel of width.
std::array<ui::Path, 4> Canopy(const ui::Rect& window, float depth, float perPixel, uint32_t seed);

// --- Winter -----------------------------------------------------------------------------------
ui::Path Snowflake(Vec2 center, float radius, float degreesCcw);
// Falling snow, held still: round flakes of about `size` scattered over `area`.
ui::Path Snowfall(const ui::Rect& area, int count, float size, uint32_t seed);
// Snow piled up at the bottom of `r` (a panel, a window): a soft wavy drift rising at most `depth`
// from the bottom edge, inside it, sloping down over the last pixels at both ends (off the rounded
// corners). Its swells are measured from r's left edge in pixels, so a box that grows or moves keeps
// the same drift (only its right end follows).
ui::Path SnowDrift(const ui::Rect& r, float depth, float cornerRadius);
// A few icicles hanging from the top edge of `window`, here and there: a small group of one to three
// under a lump of snow every 260 .. 640 pixels. The groups are laid out from the window's left edge
// and every group that starts inside it is made (one past the right edge is simply cut off there),
// so resizing the window never makes one pop in or out. Each icicle tapers to a sharp tip with
// small ripples along it (`ice`), a thin shine down its left side (`shine`); `snow` = the lumps.
struct IcicleShapes {
    ui::Path snow, ice, shine;
};
IcicleShapes Icicles(const ui::Rect& window, float maxLength, uint32_t seed);

}  // namespace rm::editor::ornament
