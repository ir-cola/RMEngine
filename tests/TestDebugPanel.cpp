// Tests for what the デバッグ panel shows: the frame meter (frames of the last second, the gaps, a view not
// drawn), the PC's monitors and the GPU of a Direct3D device (platform/SystemInfo), the names of the pads'
// controls, and the particles each picture reports.

#include "Test.h"

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
    CHECK(platform::MemoryOfThisProcess().workingSet > 0);
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
