#include "rm_engine/audio/editor/Preview.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "editor/assets/AssetPreview.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"
#include "platform/Win.h"
#include "rm_engine/audio/editor/Describer.h"
#include "rm_engine/audio/runtime/AudioEngine.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <functional>
#include <string_view>

namespace rm::audio {

namespace {

namespace fs = std::filesystem;
using editor::Tr;
using editor::TrF;

// エディターに 1 つ: ファイルが何かを調べるもの (Describer: 専用のスレッド) と、プレビューのエンジン。
struct Previewer {
    Describer describer;
    std::unique_ptr<AudioEngine> engine;
    double lastUpdate = 0;  // SoundPlaying が最後にエンジンを進めた時刻 (ボイスはそこからの経過時間で終わる)

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
    static Previewer p;  // (初めて使うときに作る: 音を一度もプレビューしないエディターはデバイスを開かない)
    return p;
}

std::wstring DescribeSound(const fs::path& file, const std::function<void()>& known) { return ThePreviewer().describer.Line(file, known); }

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
    // エンジンは、デコードしたものをこのキーで覚えている。キーにはファイルの中身が入っているので、
    // 書き直したファイル (もう一度書き出した曲) はデコードし直す - 再起動まで古い音のまま、にはならない。
    s.key = std::format("{}|{}|{:x}", PathToUtf8(file), s.bytes->size(), std::hash<std::string_view>{}(*s.bytes));
    s.name = PathToUtf8(file.filename());
    ApplyImport(s, imp);
    PlayParams params;
    params.loop = imp.loopEnd > 0 || imp.loopStart > 0;  // (プレビューがループするのは、ループが設定されているときだけ)
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
    if (!p.engine) return false;
    // (終わったボイスは Update で手放す - ゲームは毎フレーム呼ぶ。ここのプレビューは、パネルが
    // 尋ねるとおり、前回からの経過時間を渡して呼ぶ。音のデバイスが無いと、ボイスは時間が経つことでしか
    // 終わらない - 2026-11-19、Update(0) ではどれも終わらなかった。)
    const double now = platform::NowSeconds();
    const double dt = p.lastUpdate > 0 ? std::clamp(now - p.lastUpdate, 0.0, 0.25) : 0.0;
    p.lastUpdate = now;
    p.engine->Update(static_cast<float>(dt));
    return p.engine->Voices() > 0;
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const editor::AssetPreview kSound{asset::Kind::Audio, &DescribeSound, &PlaySound, &StopSound, &SoundPlaying};
    registry.Contribute(editor::kAssetPreviewPoint, kSound);
}

}  // namespace rm::audio
