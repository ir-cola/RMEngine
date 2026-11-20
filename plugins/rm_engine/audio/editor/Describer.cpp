#include "rm_engine/audio/editor/Describer.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"
#include "platform/Win.h"
#include "rm_engine/audio/runtime/Decoder.h"
#include "rm_engine/audio/runtime/Sound.h"

#include <objbase.h>

#include <string_view>

namespace rm::audio {

namespace fs = std::filesystem;
using editor::Tr;
using editor::TrF;

Describer::Describer() {
    thread_ = std::jthread([this](std::stop_token stop) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // (Media Foundation)
        while (!stop.stop_requested()) {
            fs::path file;
            {
                std::unique_lock lock(m_);
                cv_.wait(lock, stop, [&] { return !asked_.empty(); });
                if (stop.stop_requested()) break;
                file = std::move(asked_.front());
                asked_.pop_front();
            }
            const std::wstring line = Read(file);
            std::vector<std::function<void()>> tell;
            {
                std::lock_guard lock(m_);
                Known& k = known_[file.native()];
                k.ready = true;
                k.line = line;
                tell.swap(k.waiting);
            }
            for (const auto& t : tell) {
                if (t) t();  // (ロックの外で呼ぶ: UI スレッドを起こすかもしれない)
            }
        }
        if (SUCCEEDED(co)) CoUninitialize();
    });
}

Describer::~Describer() {
    thread_.request_stop();
    cv_.notify_all();
}

std::wstring Describer::Read(const fs::path& file) {
    std::string err;
    const auto bytes = platform::ReadFileBytes(file, &err);
    if (!bytes) return TrF(L"読めません: {}", Utf8ToWide(err));
    const auto info = Describe(std::make_shared<const std::string>(std::move(*bytes)), SoundBudget{}, &err);
    if (!info) return TrF(L"鳴らせません: {}", Utf8ToWide(err));
    const std::wstring channels = info->format.channels == 1   ? Tr(L"モノラル")
                                  : info->format.channels == 2 ? Tr(L"ステレオ")
                                                               : TrF(L"{} チャンネル", info->format.channels);
    std::wstring line = TrF(L"長さ {}  ·  {} Hz {}  ·  {}", Utf8ToWide(LengthText(info->seconds)), info->format.sampleRate, channels,
                            Utf8ToWide(info->codec));
    if (std::string_view(info->codec) == "Media Foundation") line += L"  ·  " + Tr(L"ゲームには WAV か OGG を勧めます (N 版の Windows では鳴りません)");
    return line;
}

std::wstring Describer::Line(const fs::path& file, const std::function<void()>& known) {
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    std::lock_guard lock(m_);
    auto [it, made] = known_.try_emplace(file.native());
    Known& k = it->second;
    if (made || k.time != time) {
        k.time = time;
        k.ready = false;
        k.line.clear();
        asked_.push_back(file);
        cv_.notify_all();
    }
    if (k.ready) return k.line;
    if (known && k.waiting.empty()) k.waiting.push_back(known);  // (一度だけ: これで全ウィンドウがまた描かれる)
    return Tr(L"長さを調べています…");
}

}  // namespace rm::audio
