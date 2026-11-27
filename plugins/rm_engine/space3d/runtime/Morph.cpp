#include "rm_engine/space3d/runtime/Morph.h"

#include <algorithm>
#include <cmath>

namespace rm::space3d {

void MorphVertex(uint32_t index, Vec3& position, Vec3& normal, std::span<const float> weights, std::span<const MorphTarget> targets) {
    Vec3 n = normal;
    bool bent = false;
    for (size_t k = 0; k < targets.size() && k < weights.size(); ++k) {
        const float w = weights[k];
        if (w == 0 || !std::isfinite(w)) continue;
        const MorphTarget& t = targets[k];
        const auto it = std::lower_bound(t.vertices.begin(), t.vertices.end(), index);
        if (it == t.vertices.end() || *it != index) continue;
        const size_t i = static_cast<size_t>(it - t.vertices.begin());
        if (i < t.position.size()) position = {position.x + t.position[i].x * w, position.y + t.position[i].y * w, position.z + t.position[i].z * w};
        if (i < t.normal.size()) {
            n = {n.x + t.normal[i].x * w, n.y + t.normal[i].y * w, n.z + t.normal[i].z * w};
            bent = true;
        }
    }
    if (bent) {
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-12f) normal = {n.x / len, n.y / len, n.z / len};
    }
}

float MorphReach(const MorphTarget& target) {
    float best = 0;
    for (const Vec3& d : target.position) best = std::max(best, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
    return best;
}

int FindMorph(std::span<const MorphTarget> targets, std::string_view name) {
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace rm::space3d
