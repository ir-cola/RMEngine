#pragma once
// ゲーム制作の「デバッグ」パネル: what is going on, to look at while making the game - how often the scene
// view and the game view are drawn (FPS), where the mouse is, what is held on the keyboard and the game pads,
// the input actions while playing, how many entities / components the scene has, the monitors and the
// GPU, and what plugins add (パーティクル: how many particles are alive).
//
//   GameSoftware   measures (FrameMeter on each view's drawing and on Play's frames), fills a DebugView
//                  and calls DebugPanel::Draw when the panel is shown
//   plugins        add sections (DebugInfoInfo at kDebugInfoPoint): lines asked each time it is drawn

#include "base/Math.h"
#include "platform/SystemInfo.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include "editor/plugin/Software.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rm::platform {
class Gfx;
}  // namespace rm::platform
namespace rm::editor {
class PluginRegistry;
}
namespace rm::ui {
class Context;
struct Rect;
}  // namespace rm::ui

namespace rm::game_production {

class Scene;
class SystemRunner;
struct ScriptWorld;
struct GameSettings;

// How often something is drawn (or run): the frames of the last second. Pure - tests/TestDebugPanel.cpp.
class FrameMeter {
public:
    // One frame at `now` (seconds) that took `workMs` of this program's time.
    void Frame(double now, double workMs = 0) {
        marks_.push_back({now, workMs});
        while (!marks_.empty() && now - marks_.front().at > 1.0) marks_.pop_front();
    }
    struct Summary {
        int frames = 0;      // in the last second (0: not drawn - nothing changed)
        double fps = 0;      // frames per second over them
        double meanMs = 0;   // the time between two of them, on average
        double worstMs = 0;  // ... the longest
        double workMs = 0;   // the time each took, on average
    };
    Summary Over(double now) const {
        Summary s;
        const Mark* last = nullptr;
        double first = 0, gaps = 0, work = 0;
        for (const Mark& m : marks_) {
            if (now - m.at > 1.0) continue;
            if (last) {
                const double gap = (m.at - last->at) * 1000.0;
                gaps += gap;
                if (gap > s.worstMs) s.worstMs = gap;
            } else {
                first = m.at;
            }
            work += m.work;
            ++s.frames;
            last = &m;
        }
        if (s.frames == 0) return s;
        s.workMs = work / s.frames;
        if (s.frames >= 2) {
            s.meanMs = gaps / (s.frames - 1);
            const double span = last->at - first;
            s.fps = span > 0 ? (s.frames - 1) / span : 0;
        } else {
            s.fps = 1;  // one frame in the last second
        }
        return s;
    }

private:
    struct Mark {
        double at, work;
    };
    std::deque<Mark> marks_;
};

// A plugin's section of the panel. PluginRegistry point: items are DebugInfoInfo (static). `collect`
// is called each time the panel is drawn (often: keep it cheap); its lines are shown as they are
// (Japanese through Tr, as any text on screen).
inline constexpr char kDebugInfoPoint[] = "rm_engine.game_production.debug_info";
struct DebugInfoLine {
    std::wstring label;
    std::wstring value;
    bool lit = false;  // shown in the accent (something that wants to be seen)
};
// What a section may look at: the scene shown (the copy that plays, while playing) and the game.
struct DebugContext {
    Scene* scene = nullptr;
    bool playing = false;
    const ScriptWorld* world = nullptr;  // while playing (its services: 音, the game UI ...)
};
// The デバッグ window's tabs.
enum class DebugTab { Basic = 0, Controller = 1, Pc = 2, Performance = 3, Game = 4 };
struct DebugInfoInfo {
    const wchar_t* title = L"";  // TrKey(L"...")
    int order = 0;
    DebugTab tab = DebugTab::Game;
    void (*collect)(const DebugContext& ctx, std::vector<DebugInfoLine>& out) = nullptr;
};

// What a view tells of its last drawing (the 3D / 2D views: what it drew, its camera). Told only while
// the panel is shown (ViewReportsWanted: the panel asks each time it is drawn); one per view.
struct ViewReport {
    std::wstring title;  // "シーンビュー (3D)" ... (Tr'd by the view)
    std::vector<DebugInfoLine> lines;
    double at = 0;
};
void WantViewReports();
bool ViewReportsWanted();
void ReportView(const void* view, ViewReport report);
std::vector<ViewReport> RecentViews(double maxAge = 1.0);

// The last frames for the graph: the interval between two (ms) and the work of each (ms), oldest first.
struct FrameGraph {
    static constexpr size_t kSamples = 240;
    std::vector<float> intervals, work;
    double last = 0;
    void Push(double now, double workMs) {
        if (last > 0) {
            intervals.push_back(static_cast<float>(std::min(250.0, (now - last) * 1000.0)));
            work.push_back(static_cast<float>(workMs));
            if (intervals.size() > kSamples) intervals.erase(intervals.begin());
            if (work.size() > kSamples) work.erase(work.begin());
        }
        last = now;
    }
};

// How long the parts of one Play frame took (ms).
struct PlayTimes {
    double scripts = 0, systems = 0, hooks = 0, services = 0, total = 0;
};

// What the game software tells the panel each time it is drawn.
struct DebugView {
    const editor::PluginRegistry* registry = nullptr;
    platform::Gfx* gfx = nullptr;
    const Scene* scene = nullptr;  // the scene shown (the copy that plays, while playing)
    const GameSettings* settings = nullptr;
    bool playing = false, paused = false;
    const ScriptWorld* world = nullptr;     // while playing
    const SystemRunner* runner = nullptr;   // while playing
    FrameMeter::Summary sceneView, gameView, update;
    bool sceneViewOpen = false, gameViewOpen = false;  // (a view not in the layout is never drawn)
    std::optional<Vec2> sceneMouse;  // over the scene view (DIPs from its top left)
    std::optional<Vec2> gameMouse;   // over the game screen (the game settings' 画面)
    size_t scriptTypes = 0;          // the scripts the built DLL has
    // While playing: how long the last frame's parts took, type by type and system by system (measured
    // while the panel is shown), and how many fixed steps it ran.
    PlayTimes playTimes;
    std::vector<ScriptRunner::ScriptTime> scriptTimes;
    std::vector<SystemRunner::SystemTime> systemTimes;
    int fixedSteps = 0;
    FrameGraph playGraph, sceneGraph;  // Play's frames; the scene view's drawing
};

// The デバッグ window (a software's tool window: no Windows frame): five folder tabs over a sheet with a
// strip to move it by and its ×. Pure layout - the shape, the hit test and the drawing use the same.
inline constexpr int kDebugTabs = 5;
inline constexpr float kDebugTabH = 32, kDebugTabGap = 4, kDebugTabRadius = 9, kDebugBodyRadius = 12;
struct DebugWindowLayout {
    ui::Rect tabs[kDebugTabs];
    ui::Rect body, close, grip, content;
};
DebugWindowLayout LayoutDebugWindow(float w, float h);
std::vector<editor::ToolRoundRect> DebugWindowShape(float w, float h);
editor::ToolHit HitDebugWindow(const DebugWindowLayout& l, float x, float y);

class DebugPanel {
public:
    // The whole window (its tabs, its sheet, its ×: `close`) - and what the chosen tab shows, in `r`.
    void DrawWindow(ui::Context& c, float width, float height, const DebugView& v, const std::function<void()>& close);
    void Draw(ui::Context& c, const ui::Rect& r, const DebugView& v);

private:
    int tab_ = 0;
    std::map<std::wstring, bool> open_;  // the sections folded or open, by title
    PadWatcher pads_;
    std::array<PadState, kPlayers> padState_{};
    double systemAt_ = -1e9;  // when the monitors, the GPU, the CPU and the memory were last read
    struct System;
    std::shared_ptr<System> system_;
    platform::CpuMeter cpuMeter_;  // how busy the CPU was between two reads (a second apart)

};

}  // namespace rm::game_production
