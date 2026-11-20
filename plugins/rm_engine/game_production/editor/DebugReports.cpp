// ビューがデバッグウィンドウに知らせること (DebugPanel.h: ViewReport)と、ウィンドウのレイアウト -
// 描画とは別にしてある: ビューとテストがそれを使う。

#include "rm_engine/game_production/editor/DebugPanel.h"

#include "platform/Shell.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace rm::game_production {

// --- ビューが知らせること -------------------------------------------------------------------------------

namespace {
std::atomic<double> g_reportsUntil{0};
std::mutex g_reportsMutex;
std::unordered_map<const void*, ViewReport>& Reports() {
    static std::unordered_map<const void*, ViewReport> reports;
    return reports;
}
}  // namespace

void WantViewReports() { g_reportsUntil = platform::NowSeconds() + 1.0; }
bool ViewReportsWanted() { return platform::NowSeconds() < g_reportsUntil.load(); }

void ReportView(const void* view, ViewReport report) {
    report.at = platform::NowSeconds();
    std::lock_guard lock(g_reportsMutex);
    Reports()[view] = std::move(report);
}

std::vector<ViewReport> RecentViews(double maxAge) {
    const double now = platform::NowSeconds();
    std::lock_guard lock(g_reportsMutex);
    std::vector<ViewReport> out;
    for (auto it = Reports().begin(); it != Reports().end();) {
        if (now - it->second.at > 60) {
            it = Reports().erase(it);  // (ずっと前になくなったビュー)
            continue;
        }
        if (now - it->second.at <= maxAge) out.push_back(it->second);
        ++it;
    }
    std::sort(out.begin(), out.end(), [](const ViewReport& a, const ViewReport& b) { return a.title < b.title; });
    return out;
}

// --- ウィンドウのレイアウト (純粋: 形・当たり判定・描画が使う) -------------------------------------------

using ui::Rect;

FolderWindowLayout LayoutFolderWindow(float w, float h) {
    FolderWindowLayout l;
    const float tabW = std::clamp((w - kFolderTabGap * (kFolderTabs - 1)) / kFolderTabs, 60.f, 150.f);
    for (int i = 0; i < kFolderTabs; ++i) l.tabs[i] = {(tabW + kFolderTabGap) * static_cast<float>(i), 0, tabW, kFolderTabH};
    l.body = {0, kFolderTabH, w, std::max(0.f, h - kFolderTabH)};
    l.close = {l.body.Right() - 34, l.body.y + 6, 26, 24};
    l.grip = {l.body.x, l.body.y, std::max(0.f, l.close.x - l.body.x - 4), 34};
    l.content = {l.body.x + 4, l.grip.Bottom(), std::max(0.f, l.body.w - 8), std::max(0.f, l.body.Bottom() - l.grip.Bottom() - 8)};
    return l;
}

std::vector<editor::ToolRoundRect> FolderWindowShape(float w, float h) {
    const FolderWindowLayout l = LayoutFolderWindow(w, h);
    std::vector<editor::ToolRoundRect> parts{{l.body, kFolderBodyRadius}};
    // (各タブはシートの奥まで届く: タブ自身の下の丸い角はそこに隠れる)
    for (const Rect& t : l.tabs) parts.push_back({{t.x, t.y, t.w, t.h + kFolderBodyRadius * 2}, kFolderTabRadius});
    return parts;
}

editor::ToolHit HitFolderWindow(const FolderWindowLayout& l, float x, float y) {
    using editor::ToolHit;
    constexpr float kEdge = 6;
    if (l.close.Contains(x, y)) return ToolHit::Client;
    for (const Rect& t : l.tabs) {
        if (t.Contains(x, y)) return ToolHit::Client;
    }
    if (y < l.body.y) return ToolHit::Nowhere;  // (タブの間と右: ウィンドウではない)
    const bool left = x < l.body.x + kEdge, right = x >= l.body.Right() - kEdge, bottom = y >= l.body.Bottom() - kEdge;
    const bool top = y < l.body.y + 4 && x > l.tabs[kFolderTabs - 1].Right();
    if (bottom && left) return ToolHit::BottomLeft;
    if (bottom && right) return ToolHit::BottomRight;
    if (top && right) return ToolHit::TopRight;
    if (bottom) return ToolHit::Bottom;
    if (left) return ToolHit::Left;
    if (right) return ToolHit::Right;
    if (top) return ToolHit::Top;
    if (l.grip.Contains(x, y)) return ToolHit::Caption;
    return ToolHit::Client;
}

}  // namespace rm::game_production
