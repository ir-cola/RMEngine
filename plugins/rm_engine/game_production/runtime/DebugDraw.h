#pragma once
// Lines a game's scripts draw to see what they do (rm::Debug::Line / Box / Sphere / Ray in RM3D.h,
// Line / Rect / Circle in RM2D.h - design RME_PLATFORM 4-b). Only the editor shows them (its game
// view and scene view, while playing): an exported game gives its scripts no buffer
// (ScriptWorld::debug stays null), so the calls do nothing there - nothing kept, nothing drawn.
//
// Every shape is kept as lines (the connectors' parts turn boxes, spheres, rectangles and circles into
// them), in the space they were drawn in. A line given for 0 seconds is seen for the frame it was
// drawn in; for N seconds, until N seconds of play have gone by. Pure - tested in tests/TestScripts.cpp.
//
//   each frame (Play):   debug.BeginFrame(dt)   -> what has had its time goes; then the scripts run
//   a script:            debug.Add(...)          -> kept (false: full - kMaxLines)
//   the views:           debug.Lines()           -> drawn (3D: hidden parts faint; 2D: on top)
//   Stop:                debug.Clear()

#include "base/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::game_production {

enum class DebugSpace : uint8_t { Space3D, Space2D };

struct DebugLine {
    Vec3 a, b;  // 2D: z = 0
    Color color;
    DebugSpace space = DebugSpace::Space3D;
    bool oneFrame = true;  // given for 0 seconds: gone at the next frame
    double until = 0;      // otherwise: kept while the clock is at or before this
};

class DebugDraw {
public:
    // More than this at once is dropped (and told once, the first time: TakeOverflowNotice). Lines are
    // cheap to draw (one draw for all of them), so this only stops a script gone wrong from filling
    // the memory (a loop that never stops adding).
    static constexpr size_t kMaxLines = 50'000;

    // A new frame of play, `dt` seconds after the last: lines given for 0 seconds in an earlier frame,
    // and lines whose seconds have gone by, are dropped.
    void BeginFrame(double dt);
    // A line to show for `seconds` (0 or less: this frame only). False when it was dropped (full).
    bool Add(DebugSpace space, Vec3 a, Vec3 b, Color color, double seconds);
    // True once after lines were first dropped (to tell the console once); again only after Clear.
    bool TakeOverflowNotice();
    // Stop: everything goes (and the notice may be told again next time).
    void Clear();

    std::span<const DebugLine> Lines() const { return lines_; }
    size_t Dropped() const { return dropped_; }
    double Clock() const { return clock_; }

private:
    std::vector<DebugLine> lines_;
    double clock_ = 0;
    size_t dropped_ = 0;
    bool noticeDue_ = false, noticeTold_ = false;
};

}  // namespace rm::game_production
