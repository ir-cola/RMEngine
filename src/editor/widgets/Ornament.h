#pragma once
// The shapes of theme ornaments (a picture a theme puts faintly in a corner of the window, e.g. the
// cherry blossom of 桜). Pure: they only make a ui::Path (tested in tests/TestTheme.cpp);
// w::DrawOrnament draws the one of the theme in use.

#include "ui/Path.h"

namespace rm::editor::ornament {

// A cherry blossom, filled as one silhouette: five petals, each with a notch at its tip. Before
// turning, one petal points straight up. radius = from the centre to the petals' tips;
// degreesCcw = turned counter-clockwise as seen on screen.
ui::Path Sakura(Vec2 center, float radius, float degreesCcw);

}  // namespace rm::editor::ornament
