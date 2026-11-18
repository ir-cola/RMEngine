#include "platform/SystemInfo.h"

#include <dxgi1_4.h>
#include <psapi.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include <format>

#pragma comment(lib, "shcore.lib")  // GetDpiForMonitor

namespace rm::platform {

namespace {

// The monitors' own names (DisplayConfig: what the monitor tells Windows), by GDI device ("\\.\DISPLAY1").
std::vector<std::pair<std::wstring, std::wstring>> FriendlyNames() {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    UINT32 paths = 0, modes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
    std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, p.data(), &modes, m.data(), nullptr) != ERROR_SUCCESS) return out;
    p.resize(paths);
    for (const DISPLAYCONFIG_PATH_INFO& path : p) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
        out.push_back({source.viewGdiDeviceName, target.monitorFriendlyDeviceName});
    }
    return out;
}

struct Gather {
    std::vector<MonitorDesc> list;
    HMONITOR window = nullptr;
};

BOOL CALLBACK OneMonitor(HMONITOR h, HDC, LPRECT, LPARAM data) {
    auto& g = *reinterpret_cast<Gather*>(data);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(h, &mi)) return TRUE;
    MonitorDesc d;
    d.device = mi.szDevice;
    d.x = mi.rcMonitor.left;
    d.y = mi.rcMonitor.top;
    d.width = mi.rcMonitor.right - mi.rcMonitor.left;
    d.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    d.hasWindow = h == g.window;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
        d.refreshHz = static_cast<int>(mode.dmDisplayFrequency);
    }
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(h, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) d.scale = static_cast<float>(dpiX) / 96.f;
    g.list.push_back(std::move(d));
    return TRUE;
}

BOOL CALLBACK FirstVisible(HWND h, LPARAM data) {
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
    *reinterpret_cast<HWND*>(data) = h;
    return FALSE;
}

}  // namespace

std::vector<MonitorDesc> Monitors(HWND window) {
    Gather g;
    if (window) g.window = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    EnumDisplayMonitors(nullptr, nullptr, &OneMonitor, reinterpret_cast<LPARAM>(&g));
    const auto names = FriendlyNames();
    for (MonitorDesc& d : g.list) {
        for (const auto& [device, name] : names) {
            if (device == d.device && !name.empty()) d.name = name;
        }
        if (d.name.empty()) {
            // (no name from the monitor: the display adapter's words for it)
            DISPLAY_DEVICEW dd{};
            dd.cb = sizeof(dd);
            d.name = EnumDisplayDevicesW(d.device.c_str(), 0, &dd, 0) && dd.DeviceString[0] ? std::wstring(dd.DeviceString) : d.device;
        }
    }
    return g.list;
}

HWND ThisThreadsWindow() {
    if (HWND active = GetActiveWindow()) return GetAncestor(active, GA_ROOT);
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), &FirstVisible, reinterpret_cast<LPARAM>(&found));
    return found;
}

std::wstring GpuVendorName(uint32_t vendorId) {
    switch (vendorId) {
        case 0x10DE: return L"NVIDIA";
        case 0x1002:
        case 0x1022: return L"AMD";
        case 0x8086: return L"Intel";
        case 0x1414: return L"Microsoft";
        case 0x5143: return L"Qualcomm";
        default: return {};
    }
}

std::optional<GpuDesc> GpuOf(ID3D11Device* device) {
    if (!device) return std::nullopt;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter))) return std::nullopt;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) return std::nullopt;
    GpuDesc g;
    g.name = desc.Description;
    g.vendorId = desc.VendorId;
    g.deviceId = desc.DeviceId;
    g.dedicatedBytes = desc.DedicatedVideoMemory;
    g.sharedBytes = desc.SharedSystemMemory;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> a3;
    if (SUCCEEDED(adapter.As(&a3))) {
        DXGI_QUERY_VIDEO_MEMORY_INFO mem{};
        if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mem))) {
            g.usedBytes = mem.CurrentUsage;
            g.budgetBytes = mem.Budget;
        }
    }
    LARGE_INTEGER umd{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        const auto v = static_cast<uint64_t>(umd.QuadPart);
        g.driver = std::format(L"{}.{}.{}.{}", (v >> 48) & 0xFFFF, (v >> 32) & 0xFFFF, (v >> 16) & 0xFFFF, v & 0xFFFF);
    }
    const D3D_FEATURE_LEVEL fl = device->GetFeatureLevel();
    g.featureLevel = std::format(L"{}_{}", (static_cast<int>(fl) >> 12) & 0xF, (static_cast<int>(fl) >> 8) & 0xF);
    return g;
}

ProcessMemory MemoryOfThisProcess() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    ProcessMemory m;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.workingSet = pmc.WorkingSetSize;
        m.committed = pmc.PrivateUsage;
    }
    return m;
}

}  // namespace rm::platform
