#pragma once
// PC が何か: モニター (名前、大きさ、リフレッシュレート、倍率) と、Direct3D デバイスが動く GPU
// (名前、メモリ、その使用量、ドライバー)。デバッグパネル用 - 聞かれたときに読み、何も覚えない。

#include "platform/Win.h"

#include <d3d11.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rm::platform {

struct MonitorDesc {
    std::wstring name;    // モニター自身の名前 ("MSI G244F")。Windows に無いときはデバイスの名前
    std::wstring device;  // "\\.\DISPLAY1"
    int x = 0, y = 0, width = 0, height = 0;  // デスクトップの上の位置と大きさ (ピクセル)
    int refreshHz = 0;                         // 0: 分からない
    float scale = 1;                           // ディスプレイの拡大率 (1.5 = 150 %)
    bool primary = false;
    bool hasWindow = false;  // 聞いたウィンドウがその上にある (一番多く乗っているもの)
};
// 全モニターを Windows の順に。`window`: それがどれの上にあるか (null: 印を付けない)。
std::vector<MonitorDesc> Monitors(HWND window = nullptr);
// このスレッドのウィンドウ (手前のもの、無ければ最初の見えているもの): エディターが表示されている所。
HWND ThisThreadsWindow();

struct GpuDesc {
    std::wstring name;
    uint32_t vendorId = 0, deviceId = 0;
    uint64_t dedicatedBytes = 0;  // 専用のメモリ
    uint64_t sharedBytes = 0;     // 使ってよい PC のメモリ
    uint64_t usedBytes = 0;       // このプログラムが今、自分のメモリのうち使っている量 (0: 分からない)
    uint64_t budgetBytes = 0;     // Windows がこのプログラムに使わせてくれる量 (0: 分からない)
    std::wstring driver;          // "31.0.15.5222" (空: 分からない)
    std::wstring featureLevel;    // "11_1"
};
std::optional<GpuDesc> GpuOf(ID3D11Device* device);
// PCI のベンダー id に対して "NVIDIA" / "AMD" / "Intel" / "Microsoft" (ソフトのもの) / ""。
std::wstring GpuVendorName(uint32_t vendorId);

// 今 Windows が音を出している出力先 (既定のもの: "スピーカー (Realtek(R) Audio)")。空: 無い / 分からない。
std::wstring DefaultAudioOutput();

// このプログラムが使っているメモリ (ワーキングセットと、その最大)、コミットした量、ハンドルと
// スレッドの数 (バイト / 個数。0: 分からない)。
struct ProcessMemory {
    uint64_t workingSet = 0;
    uint64_t peakWorkingSet = 0;
    uint64_t committed = 0;
    uint32_t handles = 0;
    uint32_t threads = 0;
};
ProcessMemory MemoryOfThisProcess();

// PC のメモリ: 全体、今空いている量、どれだけ埋まっているか (%)、コミットできる最大 (メモリ + ページ
// ファイル) と、そのうち空いている量。
struct MemoryStatus {
    uint64_t total = 0;
    uint64_t available = 0;
    uint32_t load = 0;
    uint64_t commitLimit = 0;
    uint64_t commitAvailable = 0;
};
MemoryStatus MemoryOfThePc();

// CPU: 名前 ("AMD Ryzen 7 7800X3D 8-Core Processor")、コア、スレッド (論理プロセッサー)、
// 両方の種類があるときの性能コア / 効率コア (無いときは 0 / 0)、基本クロック (MHz。0: 分からない)。
struct CpuDesc {
    std::wstring name;
    uint32_t cores = 0;
    uint32_t threads = 0;
    uint32_t performanceCores = 0;
    uint32_t efficiencyCores = 0;
    uint32_t mhz = 0;
};
CpuDesc Cpu();

// 2 回の Sample の間の CPU の忙しさ (%、0..100): PC 全体と、このプログラム (全スレッド合わせて -
// 100 は全部が忙しい)。最初の Sample は nullopt を返す (比べるものが無い)。
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
