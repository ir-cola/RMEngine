#include "rm_engine/game_x_audio/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_audio/runtime/AudioService.h"
#include "rm_engine/game_x_audio/runtime/AudioSource.h"

namespace rm::game_x_audio {

namespace {

std::unique_ptr<game_production::PlayService> CreateService(const game_production::PlayHost& host) { return std::make_unique<AudioService>(host); }

}  // namespace

void RegisterRuntime(RuntimeRegistry& registry) {
    // 音源: in 3D and 2D games alike (an entity may have several: footsteps and a voice).
    static const game_production::ComponentInfo kSource{.type = &reflect::TypeOf<AudioSource>, .multiple = true};
    registry.Contribute(game_production::kComponentPoint, kSource);
    // The sounds while the game plays (the editor's Play and the game).
    static const game_production::PlayServiceInfo kService{kAudioServiceName, &CreateService};
    registry.Contribute(game_production::kPlayServicePoint, kService);
    // Scripts: rm::Audio / rm::AudioSource (sdk/RMAudio.h).
    registry.Contribute(game_production::kScriptApiPoint, ScriptAudioPart());
}

}  // namespace rm::game_x_audio

RM_RUNTIME_ENTRY(rm_engine, game_x_audio, rm::game_x_audio::RegisterRuntime)
