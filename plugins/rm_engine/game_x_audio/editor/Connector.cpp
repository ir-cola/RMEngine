#include "rm_engine/game_x_audio/editor/Connector.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/asset/ImportSettings.h"
#include "platform/Shell.h"
#include "rm_engine/audio/runtime/Sound.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/game_x_audio/runtime/AudioSource.h"
#include "rm_engine/game_x_audio/runtime/SoundPack.h"

#include <format>
#include <span>

namespace rm::game_x_audio {

namespace {

using game_production::BakeResult;

bool IsSound(const game_production::ExportAsset& a) { return asset::KindOf(PathToUtf8(a.file.filename()), false) == asset::Kind::Audio; }

BakeResult Bake(const game_production::ExportAsset& a, game_production::ExportBaked& out, std::string* error) {
    if (!IsSound(a)) return BakeResult::NotMine;
    std::string why;
    const auto bytes = platform::ReadFileBytes(a.file, &why);
    if (!bytes) {
        if (error) *error = why;
        return BakeResult::Failed;
    }
    const std::string name = PathToUtf8(a.file.filename());
    out.path = "Baked/" + a.id + ".rmsound";
    out.files.push_back({out.path, PackSound(name, asset::ReadImportJson(a.file), *bytes)});
    // WAV and OGG are read by the engine itself; anything else needs Media Foundation on the player's PC.
    const std::span<const uint8_t> head(reinterpret_cast<const uint8_t*>(bytes->data()), bytes->size());
    const bool own = audio::LooksLikeWav(head) || bytes->starts_with("OggS");
    out.note = own ? std::format("音を入れました: {} ({} KB)", a.rel, (bytes->size() + 1023) / 1024)
                   : std::format("音を入れました: {} ({} KB)。この形は Media Foundation で読むので、それの無い PC (Windows の N エディションなど) では鳴りません。WAV か OGG を勧めます",
                                 a.rel, (bytes->size() + 1023) / 1024);
    return BakeResult::Done;
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::ComponentLook kSource{.type = &reflect::TypeOf<AudioSource>, .icon = "audio"};
    registry.Contribute(game_production::kComponentLookPoint, kSource);
    static const game_production::ExportBaker kBaker{&Bake, &IsSound};
    registry.Contribute(game_production::kExportBakePoint, kBaker);
}

}  // namespace rm::game_x_audio
