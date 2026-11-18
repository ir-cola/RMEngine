#include "platform/SystemInfo.h"

#include <dxgi1_4.h>
#include <mmdeviceapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <intrin.h>
#include <vector>

#pragma comment(lib, "shcore.lib")    // GetDpiForMonitor
#pragma comment(lib, "advapi32.lib")  // RegGetValueW
#pragma comment(lib, "ole32.lib")     // CoCreateInstance, PropVariantClear

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

std::wstring DefaultAudioOutput() {
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> devices;
    Microsoft::WRL::ComPtr<IMMDevice> device;
    Microsoft::WRL::ComPtr<IPropertyStore> props;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) ||
        FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || FAILED(device->OpenPropertyStore(STGM_READ, &props))) {
        return {};
    }
    // PKEY_Device_FriendlyName (written here: its header wants its definitions linked in from elsewhere)
    static const PROPERTYKEY kFriendlyName{{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    PROPVARIANT name;
    PropVariantInit(&name);
    std::wstring out;
    if (SUCCEEDED(props->GetValue(kFriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal) out = name.pwszVal;
    PropVariantClear(&name);
    return out;
}

ProcessMemory MemoryOfThisProcess() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    ProcessMemory m;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.workingSet = pmc.WorkingSetSize;
        m.peakWorkingSet = pmc.PeakWorkingSetSize;
        m.committed = pmc.PrivateUsage;
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) m.handles = handles;
    // The threads of this process (the system's list of every thread: one walk).
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        const DWORD me = GetCurrentProcessId();
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID == me) ++m.threads;
        }
        CloseHandle(snap);
    }
    return m;
}

MemoryStatus MemoryOfThePc() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    MemoryStatus s;
    if (GlobalMemoryStatusEx(&ms)) {
        s.total = ms.ullTotalPhys;
        s.available = ms.ullAvailPhys;
        s.load = ms.dwMemoryLoad;
        s.commitLimit = ms.ullTotalPageFile;
        s.commitAvailable = ms.ullAvailPageFile;
    }
    return s;
}

CpuDesc Cpu() {
    CpuDesc d;
    // The name the CPU tells itself (CPUID 0x80000002..4), without the spaces around it.
    int regs[4] = {};
    __cpuid(regs, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
        char brand[49] = {};
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs, static_cast<int>(0x80000002 + i));
            std::memcpy(brand + i * 16, regs, 16);
        }
        std::string s(brand);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        d.name.assign(s.begin(), s.end());
    }
    constexpr wchar_t kKey[] = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    DWORD mhz = 0, bytes = sizeof(mhz);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"~MHz", RRF_RT_REG_DWORD, nullptr, &mhz, &bytes) == ERROR_SUCCESS) d.mhz = mhz;
    if (d.name.empty()) {
        wchar_t name[128] = {};
        DWORD size = sizeof(name);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, kKey, L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS) d.name = name;
    }
    d.threads = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    // The cores, and their kinds (a hybrid CPU: the cores of the highest efficiency class are the fast ones).
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<uint8_t> buf(len);
    if (len > 0 && GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &len)) {
        std::vector<BYTE> classes;
        for (DWORD at = 0; at < len;) {
            const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + at);
            if (info->Relationship == RelationProcessorCore) classes.push_back(info->Processor.EfficiencyClass);
            at += info->Size;
        }
        d.cores = static_cast<uint32_t>(classes.size());
        if (!classes.empty()) {
            const BYTE top = *std::max_element(classes.begin(), classes.end());
            const BYTE low = *std::min_element(classes.begin(), classes.end());
            if (top != low) {
                for (const BYTE c : classes) ++(c == top ? d.performanceCores : d.efficiencyCores);
            }
        }
    }
    return d;
}

namespace {
uint64_t Ticks(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
}  // namespace

std::optional<CpuMeter::Usage> CpuMeter::Sample() {
    FILETIME idle{}, kernel{}, user{}, created{}, exited{}, pk{}, pu{};
    if (!GetSystemTimes(&idle, &kernel, &user) || !GetProcessTimes(GetCurrentProcess(), &created, &exited, &pk, &pu)) return std::nullopt;
    const uint64_t i = Ticks(idle), k = Ticks(kernel), u = Ticks(user), p = Ticks(pk) + Ticks(pu);
    std::optional<Usage> out;
    if (has_) {
        // (the system's kernel time counts its idle time too)
        const double all = static_cast<double>((k - kernel_) + (u - user_));
        if (all > 0) {
            Usage use;
            use.pc = std::clamp(100.0 * (all - static_cast<double>(i - idle_)) / all, 0.0, 100.0);
            use.thisProcess = std::clamp(100.0 * static_cast<double>(p - process_) / all, 0.0, 100.0);
            out = use;
        }
    }
    has_ = true;
    idle_ = i;
    kernel_ = k;
    user_ = u;
    process_ = p;
    return out;
}

}  // namespace rm::platform
