#pragma once
// What the PC is: its monitors (name, size, refresh rate, scale) and the GPU a Direct3D device runs on
// (name, memory, how much of it is used, the driver). For the debug panel - read when asked, nothing kept.

#include "platform/Win.h"

#include <d3d11.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rm::platform {

struct MonitorDesc {
    std::wstring name;    // the monitor's own name ("MSI G244F"); the device's when Windows has none
    std::wstring device;  // "\\.\DISPLAY1"
    int x = 0, y = 0, width = 0, height = 0;  // on the desktop (pixels)
    int refreshHz = 0;                         // 0: not known
    float scale = 1;                           // the display's scaling (1.5 = 150 %)
    bool primary = false;
    bool hasWindow = false;  // the window asked about is on it (the most of it)
};
// Every monitor, in Windows' order. `window`: which one it is on (null: none marked).
std::vector<MonitorDesc> Monitors(HWND window = nullptr);
// A window of this thread (the one in front, else the first visible one): what the editor shows on.
HWND ThisThreadsWindow();

struct GpuDesc {
    std::wstring name;
    uint32_t vendorId = 0, deviceId = 0;
    uint64_t dedicatedBytes = 0;  // its own memory
    uint64_t sharedBytes = 0;     // the PC's memory it may use
    uint64_t usedBytes = 0;       // what this program uses of its own memory now (0: not known)
    uint64_t budgetBytes = 0;     // what Windows lets this program use (0: not known)
    std::wstring driver;          // "31.0.15.5222" (empty: not known)
    std::wstring featureLevel;    // "11_1"
};
std::optional<GpuDesc> GpuOf(ID3D11Device* device);
// "NVIDIA" / "AMD" / "Intel" / "Microsoft" (the software one) / "" for the PCI vendor id.
std::wstring GpuVendorName(uint32_t vendorId);

// This program's memory in use (its working set, and the most it has been), what it has committed, and
// its handles and threads (bytes / counts; 0: not known).
struct ProcessMemory {
    uint64_t workingSet = 0;
    uint64_t peakWorkingSet = 0;
    uint64_t committed = 0;
    uint32_t handles = 0;
    uint32_t threads = 0;
};
ProcessMemory MemoryOfThisProcess();

// The PC's memory: all of it, what is free now, how full it is (%), and the most that may be committed
// (memory + the page file) and what of that is free.
struct MemoryStatus {
    uint64_t total = 0;
    uint64_t available = 0;
    uint32_t load = 0;
    uint64_t commitLimit = 0;
    uint64_t commitAvailable = 0;
};
MemoryStatus MemoryOfThePc();

// The CPU: its name ("AMD Ryzen 7 7800X3D 8-Core Processor"), cores, threads (logical processors), the
// performance / efficiency cores when it has both kinds (0 / 0 when not), its base clock (MHz; 0: not
// known).
struct CpuDesc {
    std::wstring name;
    uint32_t cores = 0;
    uint32_t threads = 0;
    uint32_t performanceCores = 0;
    uint32_t efficiencyCores = 0;
    uint32_t mhz = 0;
};
CpuDesc Cpu();

// How busy the CPU is between two Sample calls (%, 0..100): the whole PC, and this program (of all the
// threads together - 100 is every one busy). The first Sample gives nullopt (nothing to compare with).
class CpuMeter {
public:
    struct Usage {
        double pc = 0;
        double thisProcess = 0;
    };
    std::optional<Usage> Sample();

private:
    bool has_ = false;
    uint64_t idle_ = 0, kernel_ = 0, user_ = 0, process_ = 0;
};

}  // namespace rm::platform
