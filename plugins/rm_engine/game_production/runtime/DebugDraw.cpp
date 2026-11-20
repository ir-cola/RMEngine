#include "rm_engine/game_production/runtime/DebugDraw.h"

#include <algorithm>

namespace rm::game_production {

void DebugDraw::BeginFrame(double dt) {
    clock_ += std::max(dt, 0.0);
    std::erase_if(lines_, [&](const DebugLine& l) { return l.oneFrame || l.until < clock_; });
}

bool DebugDraw::Add(DebugSpace space, Vec3 a, Vec3 b, Color color, double seconds) {
    if (lines_.size() >= kMaxLines) {
        ++dropped_;
        if (!noticeTold_) noticeDue_ = true;
        return false;
    }
    const bool oneFrame = !(seconds > 0);  // (NaN も)
    lines_.push_back({a, b, color, space, oneFrame, oneFrame ? clock_ : clock_ + seconds});
    return true;
}

bool DebugDraw::TakeOverflowNotice() {
    if (!noticeDue_) return false;
    noticeDue_ = false;
    noticeTold_ = true;
    return true;
}

void DebugDraw::Clear() {
    lines_.clear();
    clock_ = 0;
    dropped_ = 0;
    noticeDue_ = noticeTold_ = false;
}

}  // namespace rm::game_production
