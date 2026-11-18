// What the views tell the デバッグ panel (DebugPanel.h: ViewReport). Apart from the panel (no drawing):
// the views and the tests use it.

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

}  // namespace rm::game_production
