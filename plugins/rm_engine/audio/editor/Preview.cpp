#include "rm_engine/audio/editor/Preview.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "editor/assets/AssetPreview.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"
#include "platform/Win.h"
#include "rm_engine/audio/runtime/AudioEngine.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace rm::audio {

namespace {

namespace fs = std::filesystem;
using editor::Tr;
using editor::TrF;

// What is known of a file (its describe line), by its path and time.
struct Known {
    fs::file_time_type time{};
    bool ready = false;
    std::wstring line;
};

// One for the editor: the preview's engine and the thread that reads what files are.
struct Previewer {
    std::mutex m;
    std::condition_variable_any cv;
    std::unordered_map<std::wstring, Known> known;
    std::deque<fs::path> asked;
    std::unique_ptr<AudioEngine> engine;
    std::jthread thread;

    Previewer() {
        thread = std::jthread([this](std::stop_token stop) {
            const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // (Media Foundation)
            while (!stop.stop_requested()) {
                fs::path file;
                {
                    std::unique_lock lock(m);
                    cv.wait(lock, stop, [&] { return !asked.empty(); });
                    if (stop.stop_requested()) break;
                    file = std::move(asked.front());
                    asked.pop_front();
                }
                const std::wstring line = Read(file);
                std::lock_guard lock(m);
                Known& k = known[file.native()];
                k.ready = true;
                k.line = line;
            }
            if (SUCCEEDED(co)) CoUninitialize();
        });
    }
    ~Previewer() {
        thread.request_stop();
        cv.notify_all();
    }

    static std::wstring Read(const fs::path& file) {
        std::string err;
        const auto bytes = platform::ReadFileBytes(file, &err);
        if (!bytes) return TrF(L"読めません: {}", Utf8ToWide(err));
        const auto info = Describe(std::make_shared<const std::string>(std::move(*bytes)), SoundBudget{}, &err);
        if (!info) return TrF(L"鳴らせません: {}", Utf8ToWide(err));
        const std::wstring channels = info->format.channels == 1 ? Tr(L"モノラル") : info->format.channels == 2 ? Tr(L"ステレオ")
                                                                                                                 : TrF(L"{} チャンネル", info->format.channels);
        std::wstring line = TrF(L"長さ {}  ·  {} Hz {}  ·  {}", Utf8ToWide(LengthText(info->seconds)), info->format.sampleRate, channels,
                                Utf8ToWide(info->codec));
        if (std::string_view(info->codec) == "Media Foundation") line += L"  ·  " + Tr(L"ゲームには WAV か OGG を勧めます (N 版の Windows では鳴りません)");
        return line;
    }

    AudioEngine& Engine() {
        if (!engine) engine = std::make_unique<AudioEngine>();
        return *engine;
    }
    void TellMessages() {
        if (!engine) return;
        for (const std::string& msg : engine->TakeMessages()) log::Warn("音の試し: {}", msg);
    }
};

Previewer& ThePreviewer() {
    static Previewer p;  // (made on first use: an editor that never previews a sound opens no device)
    return p;
}

std::wstring DescribeSound(const fs::path& file) {
    Previewer& p = ThePreviewer();
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    std::lock_guard lock(p.m);
    auto [it, made] = p.known.try_emplace(file.native());
    if (made || it->second.time != time) {
        it->second = {time, false, {}};
        p.asked.push_back(file);
        p.cv.notify_all();
    }
    return it->second.ready ? it->second.line : Tr(L"長さを調べています…");
}

void PlaySound(const fs::path& file, const json::Value& importJson) {
    Previewer& p = ThePreviewer();
    AudioEngine& engine = p.Engine();
    engine.StopAll();
    std::string err;
    auto bytes = platform::ReadFileBytes(file, &err);
    if (!bytes) {
        log::Warn("音の試し: 「{}」を読めません: {}", PathToUtf8(file.filename()), err);
        return;
    }
    asset::SoundImport imp;
    reflect::Load(imp, importJson);
    SoundSource s;
    s.bytes = std::make_shared<const std::string>(std::move(*bytes));
    s.key = PathToUtf8(file);
    s.name = PathToUtf8(file.filename());
    ApplyImport(s, imp);
    PlayParams params;
    params.loop = imp.loopEnd > 0 || imp.loopStart > 0;  // (a preview loops only when its loop is set)
    engine.Play(s, params);
    log::Info("音の試し: {}{}", s.name, params.loop ? " (ループ: 「止める」で止まります)" : "");
    p.TellMessages();
}

void StopSound() {
    Previewer& p = ThePreviewer();
    if (p.engine) p.engine->StopAll();
}

bool SoundPlaying() {
    Previewer& p = ThePreviewer();
    p.TellMessages();
    return p.engine && p.engine->Voices() > 0;
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const editor::AssetPreview kSound{asset::Kind::Audio, &DescribeSound, &PlaySound, &StopSound, &SoundPlaying};
    registry.Contribute(editor::kAssetPreviewPoint, kSound);
}

}  // namespace rm::audio
