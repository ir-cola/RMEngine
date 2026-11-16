#pragma once
// ゲーム UI - how UI things move while the game plays (UI-4: docs/UI_EDITOR.md). Pure (no drawing, no
// scene): the UI service steps them each frame and writes UiLive (UiComponents.h); the layout puts it on
// top of UI の配置. Tested in tests/TestGameUi.cpp.
//
//   出る・消える動き   UiTransition: from its values to the thing as it is (出る), or the other way (消える)
//   道に沿って動く     UiPath: a line through points (curved: Catmull-Rom), walked at a speed
//   速さの曲線         Ease: how the time 0..1 becomes the way 0..1 (はねる / 行き過ぎて戻る leave 0..1 on
//                      the way and end at 1)

#include "rm_engine/game_ui/runtime/UiComponents.h"

#include <vector>

namespace rm::game_ui {

// The time `t` (0..1) through the curve: 0 at 0, 1 at 1.
float Ease(UiEase ease, float t);

// Two moves at once (a window coming in while its thing walks its path): moved by both, scaled by both,
// turned by both, faded by both.
UiLive Combine(const UiLive& a, const UiLive& b);

// A transition `time` seconds after it began: 出る (`showing`) - its values at first, the thing as it is
// at the end; 消える - the other way. Before its 待ち: as at its beginning.
UiLive TransitionAt(const UiTransition& t, bool showing, float time);
// It has ended (or does nothing: not used).
bool TransitionDone(const UiTransition& t, float time);

// A path as a line of short straight pieces (curved when smooth), and the length along it to each point.
struct UiPathLine {
    std::vector<Vec2> points;
    std::vector<float> along;  // from the first point
    float Length() const { return along.empty() ? 0.f : along.back(); }
};
UiPathLine PathLine(const UiPath& path);
// Where `distance` along the line is (kept to the line), and which way it goes there (degrees: right 0,
// down 90 - the screen's Y goes down).
struct UiPathPose {
    Vec2 offset{0, 0};
    float angle = 0;
};
UiPathPose PathPoseAt(const UiPathLine& line, float distance);
// One frame of a path (UiPath's transient values: started, playing, travelled, backward), and where the
// thing is on it now.
UiPathPose StepPath(UiPath& path, const UiPathLine& line, float dt);

}  // namespace rm::game_ui
