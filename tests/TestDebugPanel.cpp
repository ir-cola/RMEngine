// Tests for what the デバッグ panel shows: the frame meter (frames of the last second, the gaps, a view not
// drawn), the PC's monitors and the GPU of a Direct3D device (platform/SystemInfo), the names of the pads'
// controls, and the particles each picture reports.

#include "Test.h"

#include "base/Log.h"
#include "platform/SystemInfo.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "rm_engine/game_x_particles/runtime/ParticleStats.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cmath>

#pragma comment(lib, "d3d11.lib")

using namespace rm;

RM_TEST(DebugPanel_FrameMeter) {
    game_production::FrameMeter m;
    CHECK(m.Over(0).frames == 0);
    // 60 frames 1/60 s apart, the last 10 ms late: 60 FPS, the worst gap 26.7 ms.
    double t = 10;
    for (int i = 0; i < 60; ++i) {
        t += 1.0 / 60 + (i == 59 ? 0.010 : 0);
        m.Frame(t, 2.0);
    }
    const auto s = m.Over(t);
    CHECK(s.frames == 60 && std::fabs(s.fps - 59.0 / (t - (10 + 1.0 / 60))) < 0.01 && s.fps > 55 && s.fps < 61);
    CHECK(std::fabs(s.worstMs - 26.67) < 0.1 && std::fabs(s.workMs - 2.0) < 1e-9);
    // Nothing drawn for a second: nothing to tell (the editor draws only what changes).
    CHECK(m.Over(t + 1.5).frames == 0);
    // One frame alone.
    m.Frame(t + 3);
    CHECK(m.Over(t + 3).frames == 1 && m.Over(t + 3).fps == 1);
}

RM_TEST(DebugPanel_MonitorsAndGpu) {
    const std::vector<platform::MonitorDesc> monitors = platform::Monitors();
    CHECK(!monitors.empty());
    int primaries = 0;
    for (const auto& m : monitors) {
        CHECK(m.width > 0 && m.height > 0 && !m.name.empty() && m.device.starts_with(L"\\\\.\\") && m.scale >= 1 && !m.hasWindow);
        primaries += m.primary ? 1 : 0;
    }
    CHECK(primaries == 1);
    // The GPU of a device (the software one when there is no GPU here: a name all the same).
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, nullptr);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &fl, nullptr);
    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) return;
    const auto gpu = platform::GpuOf(device.Get());
    CHECK(gpu && !gpu->name.empty() && !gpu->featureLevel.empty() && gpu->featureLevel.find(L'_') != std::wstring::npos);
    CHECK(!platform::GpuOf(nullptr));
    CHECK(platform::GpuVendorName(0x10DE) == L"NVIDIA" && platform::GpuVendorName(0x1234).empty());
    const platform::ProcessMemory mine = platform::MemoryOfThisProcess();
    CHECK(mine.workingSet > 0 && mine.peakWorkingSet >= mine.workingSet && mine.handles > 0 && mine.threads >= 1);
}

RM_TEST(DebugPanel_CpuAndMemory) {
    const platform::CpuDesc cpu = platform::Cpu();
    CHECK(!cpu.name.empty() && cpu.cores >= 1 && cpu.threads >= cpu.cores);
    CHECK(cpu.performanceCores + cpu.efficiencyCores == 0 || cpu.performanceCores + cpu.efficiencyCores == cpu.cores);
    const platform::MemoryStatus pc = platform::MemoryOfThePc();
    CHECK(pc.total > 0 && pc.available <= pc.total && pc.load <= 100 && pc.commitLimit >= pc.total / 2);
    // Busy for a while between two samples: this program used some of the CPU (never more than all of it).
    platform::CpuMeter meter;
    CHECK(!meter.Sample());
    volatile double sink = 0;
    const DWORD until = GetTickCount() + 120;
    while (GetTickCount() < until) sink = sink + 1.0;
    const auto use = meter.Sample();
    CHECK(use && use->pc >= 0 && use->pc <= 100 && use->thisProcess > 0 && use->thisProcess <= use->pc + 1e-6);
}

RM_TEST(DebugPanel_PadNamesAndParticles) {
    using namespace rm::game_production;
    CHECK(ControlName(kPadControlBase) == "PadA" && ControlName(kPadControlBase + 4) == "PadLB" && ControlName(kPadControlBase + 19) == "PadRightStickY");
    // A drawer's report is there while it is fresh, one entry per drawer.
    int drawer = 0;
    game_x_particles::PictureParticles p;
    p.picture = "scene";
    p.alive = 120;
    p.aliveKnown = true;
    game_x_particles::ReportParticles(&drawer, p);
    p.alive = 130;
    game_x_particles::ReportParticles(&drawer, p);
    size_t mine = 0;
    for (const auto& r : game_x_particles::RecentParticles()) {
        if (r.picture == "scene" && r.alive == 130) ++mine;
    }
    CHECK(mine == 1);
    CHECK(game_x_particles::RecentParticles(-1).empty());  // (nothing is newer than now)
}

RM_TEST(DebugPanel_SystemsTimed_FixedSteps) {
    using namespace rm::game_production;
    static int fixedRuns = 0;
    static const SystemInfo kFixed{"fixed", SystemPhase::Fixed, 0, [](SystemContext&) { ++fixedRuns; }};
    static const SystemInfo kFrame{"frame", SystemPhase::Frame, 0, [](SystemContext&) {}};
    SystemRunner r;
    const SystemInfo* list[] = {&kFrame, &kFixed};
    r.SetSystems(list);
    Scene s;
    r.Advance(s, 0.1, 0.02f);
    CHECK(r.Times().empty() && r.LastSteps() == 5);  // (not timed unless asked)
    r.SetTiming(true);
    r.Advance(s, 0.04, 0.02f);
    // In the systems' order (Fixed first): the fixed one ran twice, the frame one once.
    CHECK(r.Times().size() == 2 && r.Times()[0].system == &kFixed && r.Times()[0].runs == 2 && r.Times()[1].runs == 1 && r.LastSteps() == 2);
    CHECK(r.Times()[0].ms >= 0);
    r.Advance(s, 1.0, 0.02f);  // far behind: the most steps, no more
    CHECK(r.LastSteps() == SystemRunner::kMaxFixedSteps);
}

RM_TEST(DebugPanel_LogCountsAndViewReports) {
    using namespace rm::game_production;
    const log::Counts before = log::Counted();
    log::Warn("デバッグのテスト {}", 1);
    log::Info("デバッグのテスト {}", 2);
    const log::Counts after = log::Counted();
    CHECK(after.warn == before.warn + 1 && after.info == before.info + 1 && after.error == before.error);
    WantViewReports();
    CHECK(ViewReportsWanted());
    int a = 0, b = 0;
    ViewReport r;
    r.title = L"zz test B";
    r.lines.push_back({L"x", L"1"});
    ReportView(&a, r);
    r.title = L"zz test A";
    ReportView(&b, r);
    ReportView(&b, r);  // (one per view)
    size_t mine = 0;
    std::wstring order;
    for (const ViewReport& v : RecentViews()) {
        if (v.title.starts_with(L"zz test")) {
            ++mine;
            order += v.title.back();
        }
    }
    CHECK(mine == 2 && order == L"AB");  // sorted by title
}
