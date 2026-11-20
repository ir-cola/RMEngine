// デバッグパネルが見せるもののテスト: フレームメーター (直近 1 秒のフレーム、間隔、描かれて
// いないビュー)、PC のモニターと Direct3D デバイスの GPU (platform/SystemInfo)、パッドの
// 操作の名前、そして各画面が報告するパーティクル。

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
    // 1/60 秒おきの 60 フレーム、最後が 10 ms 遅れた: 60 FPS、一番大きい間隔は 26.7 ms。
    double t = 10;
    for (int i = 0; i < 60; ++i) {
        t += 1.0 / 60 + (i == 59 ? 0.010 : 0);
        m.Frame(t, 2.0);
    }
    const auto s = m.Over(t);
    CHECK(s.frames == 60 && std::fabs(s.fps - 59.0 / (t - (10 + 1.0 / 60))) < 0.01 && s.fps > 55 && s.fps < 61);
    CHECK(std::fabs(s.worstMs - 26.67) < 0.1 && std::fabs(s.workMs - 2.0) < 1e-9);
    // 1 秒間何も描かれていない: 知らせることはない (エディターは変わったものだけを描く)。
    CHECK(m.Over(t + 1.5).frames == 0);
    // フレームが 1 つだけ。
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
    // デバイスの GPU (ここに GPU がなければソフトのもの: それでも名前はある)。
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
    // 2 つのサンプルの間しばらく忙しかった: このプログラムは CPU を少し使った (全部を超えることはない)。
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
    // 描画役の報告は、新しいうちはある。描画役ごとに 1 項目。
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
    CHECK(game_x_particles::RecentParticles(-1).empty());  // (今より新しいものはない)
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
    CHECK(r.Times().empty() && r.LastSteps() == 5);  // (頼まれない限り時間は計らない)
    r.SetTiming(true);
    r.Advance(s, 0.04, 0.02f);
    // システムの順 (Fixed が先): 固定のものは 2 回、フレームのものは 1 回走った。
    CHECK(r.Times().size() == 2 && r.Times()[0].system == &kFixed && r.Times()[0].runs == 2 && r.Times()[1].runs == 1 && r.LastSteps() == 2);
    CHECK(r.Times()[0].ms >= 0);
    r.Advance(s, 1.0, 0.02f);  // 大きく遅れた: 最大のステップ数まで、それ以上は進まない
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
    ReportView(&b, r);  // (ビューごとに 1 つ)
    size_t mine = 0;
    std::wstring order;
    for (const ViewReport& v : RecentViews()) {
        if (v.title.starts_with(L"zz test")) {
            ++mine;
            order += v.title.back();
        }
    }
    CHECK(mine == 2 && order == L"AB");  // タイトル順に並ぶ
}

RM_TEST(DebugPanel_WindowLayoutShapeAndHits) {
    using namespace rm::game_production;
    using rm::editor::ToolHit;
    const FolderWindowLayout l = LayoutFolderWindow(620, 720);
    // 上に 5 つのタブが横に並び、その下にシート、シートの右上に ×。
    for (int i = 1; i < kFolderTabs; ++i) CHECK(l.tabs[i].x > l.tabs[i - 1].Right() && l.tabs[i].y == 0);
    CHECK(l.tabs[0].x == 0 && l.body.y == kFolderTabH && l.body.Bottom() == 720 && l.close.Right() < 620 && l.close.y > l.body.y);
    CHECK(l.content.y >= l.grip.Bottom() && l.content.Bottom() <= l.body.Bottom());
    // 形: シートと、タブごとの角丸の四角。
    const auto shape = FolderWindowShape(620, 720);
    CHECK(shape.size() == 1 + kFolderTabs && shape[0].rect.y == kFolderTabH);
    // 点が何か: タブと × は UI のもの、帯はウィンドウを動かし、縁は大きさを変える。
    // タブの右はウィンドウではない。
    CHECK(HitFolderWindow(l, l.tabs[2].x + 5, 10) == ToolHit::Client);
    CHECK(HitFolderWindow(l, l.close.x + 5, l.close.y + 5) == ToolHit::Client);
    CHECK(HitFolderWindow(l, 200, l.body.y + 15) == ToolHit::Caption);
    CHECK(HitFolderWindow(l, 300, 400) == ToolHit::Client);
    CHECK(HitFolderWindow(l, 619, 719) == ToolHit::BottomRight && HitFolderWindow(l, 1, 719) == ToolHit::BottomLeft);
    CHECK(HitFolderWindow(l, 619, 400) == ToolHit::Right && HitFolderWindow(l, 300, 719) == ToolHit::Bottom);
    // 広い: タブは一番広い幅で止まる。その右はウィンドウではない。
    const FolderWindowLayout wide = LayoutFolderWindow(1000, 600);
    CHECK(wide.tabs[kFolderTabs - 1].Right() < 900 && HitFolderWindow(wide, 950, 10) == ToolHit::Nowhere);
    // 狭い: タブは縮むが、ウィンドウの中で 5 つのまま。
    const FolderWindowLayout n = LayoutFolderWindow(420, 300);
    CHECK(n.tabs[kFolderTabs - 1].Right() <= 420);
}
