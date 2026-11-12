#include "rm_engine/space3d/runtime/DrawOrder.h"

#include <algorithm>
#include <numeric>

namespace rm::space3d {

void SortDraws(std::span<const DrawKey> keys, std::vector<uint32_t>& order) {
    order.resize(keys.size());
    std::iota(order.begin(), order.end(), 0u);
    // Most frames have long runs already in order (things listed by kind): checked first, as cheap.
    if (std::is_sorted(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; })) return;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });
}

size_t CountRuns(std::span<const DrawKey> keys, std::span<const uint32_t> order) {
    size_t runs = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (i == 0 || !(keys[order[i]] == keys[order[i - 1]])) ++runs;
    }
    return runs;
}

}  // namespace rm::space3d
