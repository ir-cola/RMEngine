#include "rm_engine/animation3d/runtime/Thinning.h"

#include <cmath>

namespace rm::animation3d {

int PoseInterval(float screenFraction, bool thin) {
    if (!(screenFraction > 0) || !std::isfinite(screenFraction)) return 0;
    if (!thin || screenFraction >= kThinNear) return 1;
    return screenFraction >= kThinMid ? 2 : 4;
}

bool PoseThisFrame(int interval, unsigned long long frame, unsigned phase) {
    if (interval <= 0) return false;
    if (interval == 1) return true;
    return (frame + phase) % static_cast<unsigned long long>(interval) == 0;
}

}  // namespace rm::animation3d
