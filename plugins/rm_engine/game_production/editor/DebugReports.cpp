// What the views tell the デバッグ window (DebugPanel.h: ViewReport), and the window's layout - apart from
// its drawing: the views and the tests use them.

#include "rm_engine/game_production/editor/DebugPanel.h"

#include "platform/Shell.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace rm::game_production {

// --- What the views tell -------------------------------------------------------------------------------

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
            it = Reports().erase(it);  // (a view gone long ago)
            continue;
        }
        if (now - it->second.at <= maxAge) out.push_back(it->second);
        ++it;
    }
    std::sort(out.begin(), out.end(), [](const ViewReport& a, const ViewReport& b) { return a.title < b.title; });
    return out;
}

// --- The window's layout (pure: the shape, the hit test and the drawing use it) -------------------------

using ui::Rect;

DebugWindowLayout LayoutDebugWindow(float w, float h) {
    DebugWindowLayout l;
    const float tabW = std::clamp((w - kDebugTabGap * (kDebugTabs - 1)) / kDebugTabs, 60.f, 150.f);
    for (int i = 0; i < kDebugTabs; ++i) l.tabs[i] = {(tabW + kDebugTabGap) * static_cast<float>(i), 0, tabW, kDebugTabH};
    l.body = {0, kDebugTabH, w, std::max(0.f, h - kDebugTabH)};
    l.close = {l.body.Right() - 34, l.body.y + 6, 26, 24};
    l.grip = {l.body.x, l.body.y, std::max(0.f, l.close.x - l.body.x - 4), 34};
    l.content = {l.body.x + 4, l.grip.Bottom(), std::max(0.f, l.body.w - 8), std::max(0.f, l.body.Bottom() - l.grip.Bottom() - 8)};
    return l;
}

std::vector<editor::ToolRoundRect> DebugWindowShape(float w, float h) {
    const DebugWindowLayout l = LayoutDebugWindow(w, h);
    std::vector<editor::ToolRoundRect> parts{{l.body, kDebugBodyRadius}};
    // (each tab reaches well into the sheet: its own round bottom corners stay hidden there)
    for (const Rect& t : l.tabs) parts.push_back({{t.x, t.y, t.w, t.h + kDebugBodyRadius * 2}, kDebugTabRadius});
    return parts;
}

editor::ToolHit HitDebugWindow(const DebugWindowLayout& l, float x, float y) {
    using editor::ToolHit;
    constexpr float kEdge = 6;
    if (l.close.Contains(x, y)) return ToolHit::Client;
    for (const Rect& t : l.tabs) {
        if (t.Contains(x, y)) return ToolHit::Client;
    }
    if (y < l.body.y) return ToolHit::Nowhere;  // (between and right of the tabs: not the window)
    const bool left = x < l.body.x + kEdge, right = x >= l.body.Right() - kEdge, bottom = y >= l.body.Bottom() - kEdge;
    const bool top = y < l.body.y + 4 && x > l.tabs[kDebugTabs - 1].Right();
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
