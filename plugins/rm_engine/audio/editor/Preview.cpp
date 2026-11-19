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

// One for the editor: what files are (Describer: on a thread of its own) and the preview's engine.
struct Previewer {
    Describer describer;
    std::unique_ptr<AudioEngine> engine;
    double lastUpdate = 0;  // when SoundPlaying last moved the engine on (its voices end by the time since)

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
    // The engine keeps what it decoded by this key: the file's contents are in it, so a file written
    // again (a song exported once more) is decoded again - not the old sound until a restart.
    s.key = std::format("{}|{}|{:x}", PathToUtf8(file), s.bytes->size(), std::hash<std::string_view>{}(*s.bytes));
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
    if (!p.engine) return false;
    // (Voices that finished are let go in Update - the game calls it every frame; the preview here, as
    // the panel asks, with the time since it last did: without a sound device a voice ends only as its
    // time goes by - 2026-11-19, Update(0) never ended one.)
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
