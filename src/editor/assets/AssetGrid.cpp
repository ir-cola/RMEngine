#include "editor/assets/AssetGrid.h"

#include "editor/tree/TreeModel.h"

#include <algorithm>
#include <cmath>

namespace rm::editor {

AssetGridMetrics MeasureAssetGrid(float width, float icon) {
    AssetGridMetrics m;
    m.icon = std::max(8.f, icon);
    m.tileW = m.icon + 20;                       // 名前が絵より少し広くても入る
    m.tileH = 6 + m.icon + 6 + kAssetNameLine * 2 + 4;
    const float room = width - kAssetGridPad * 2;
    m.columns = std::max(1, static_cast<int>(std::floor((room + m.gap) / (m.tileW + m.gap))));
    return m;
}

ui::Rect AssetTileRect(const AssetGridMetrics& m, float x0, float y0, size_t i) {
    const size_t cols = static_cast<size_t>(std::max(1, m.columns));
    const float col = static_cast<float>(i % cols), row = static_cast<float>(i / cols);
    return {x0 + kAssetGridPad + col * (m.tileW + m.gap), y0 + kAssetGridPad + row * (m.tileH + m.gap), m.tileW, m.tileH};
}

float AssetGridHeight(const AssetGridMetrics& m, size_t count) {
    const size_t cols = static_cast<size_t>(std::max(1, m.columns));
    const size_t rows = (count + cols - 1) / cols;
    return kAssetGridPad * 2 + static_cast<float>(rows) * m.tileH + static_cast<float>(rows > 0 ? rows - 1 : 0) * m.gap;
}

int AssetTileAt(const AssetGridMetrics& m, float x0, float y0, size_t count, float x, float y) {
    const float rx = x - x0 - kAssetGridPad, ry = y - y0 - kAssetGridPad;
    if (rx < 0 || ry < 0) return -1;
    const int col = static_cast<int>(rx / (m.tileW + m.gap)), row = static_cast<int>(ry / (m.tileH + m.gap));
    if (col >= m.columns) return -1;
    if (rx - static_cast<float>(col) * (m.tileW + m.gap) > m.tileW) return -1;  // 間
    if (ry - static_cast<float>(row) * (m.tileH + m.gap) > m.tileH) return -1;
    const size_t i = static_cast<size_t>(row) * static_cast<size_t>(m.columns) + static_cast<size_t>(col);
    return i < count ? static_cast<int>(i) : -1;
}

std::vector<size_t> AssetsInFolder(std::span<const asset::Entry> entries, std::span<const size_t> order, std::string_view folder) {
    std::vector<size_t> out;
    for (const size_t i : order) {
        if (i < entries.size() && asset::ParentOf(entries[i].path) == folder) out.push_back(i);
    }
    return out;
}

std::vector<size_t> AssetsMatching(std::span<const asset::Entry> entries, std::span<const size_t> order, std::string_view filter) {
    std::vector<size_t> out;
    if (filter.empty()) return out;
    for (const size_t i : order) {
        if (i < entries.size() && tree::NameMatches(asset::FileNameOf(entries[i].path), filter)) out.push_back(i);
    }
    return out;
}

std::vector<std::string> AssetFolderSteps(std::string_view folder) {
    std::vector<std::string> out;
    size_t start = 0;
    while (!folder.empty() && start <= folder.size()) {
        const size_t slash = folder.find('/', start);
        const size_t end = slash == std::string_view::npos ? folder.size() : slash;
        out.emplace_back(folder.substr(0, end));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return out;
}

}  // namespace rm::editor
