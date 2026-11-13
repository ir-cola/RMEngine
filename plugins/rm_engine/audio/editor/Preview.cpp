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

#include <filesystem>
#include <format>

namespace rm::audio {

namespace {

namespace fs = std::filesystem;
using editor::Tr;
using editor::TrF;

// One for the editor: what files are (Describer: on a thread of its own) and the preview's engine.
struct Previewer {
    Describer describer;
    std::unique_ptr<AudioEngine> engine;

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
