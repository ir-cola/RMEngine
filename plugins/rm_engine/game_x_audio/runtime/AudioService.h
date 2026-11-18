#pragma once
// ゲーム制作 × 音 - the sounds of one Play (of the game): game production's PlayService
// (game_production/runtime/PlayServices.h). Made when the game starts to play, gone at ■ - with it its
// AudioEngine (rm_engine/audio), so nothing is heard after.
//
// Each frame (after the scripts and the systems):
//   the volumes        the game settings' 音量 (全体・BGM・効果音), or what a script set
//   who hears          the game's camera (where it is and looks: the space's PlaceInfo)
//   the 音源 components a new one (Play, the scene switched to, switched on, spawned) starts when
//                      始めから鳴らす; one that went (deleted, switched off) stops (with its フェード);
//                      the others follow their 音量 / ピッチ and where their entity is
// The music (BGM): one at a time. Starting one fades out the one before (クロスフェード); the same
// sound already playing as the music goes on (a scene switched to with the same BGM does not start it
// again).
//
// Sounds come from the host (PlayHost::asset): the editor's files with their .meta, the game's
// .rmsound (SoundPack.h). Each sound read once per Play.

#include "rm_engine/audio/runtime/AudioEngine.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_audio/runtime/AudioSource.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace rm::game_x_audio {

inline constexpr char kAudioServiceName[] = "rm_engine.game_x_audio.audio";

class AudioService final : public game_production::PlayService, public game_production::SoundPlayer {
public:
    using VoiceId = audio::AudioEngine::VoiceId;
    enum class BusVolume { Master = 0, Music = 1, Effects = 2 };

    explicit AudioService(const game_production::PlayHost& host);
    ~AudioService() override;

    void Frame(const game_production::PlayFrame& f) override;
    void SetPaused(bool paused) override;
    // The scene switched: every 音源 of the scene before stops (with its フェード); its music goes on
    // only when the new scene starts the same sound as its music (the next Frame decides).
    void SceneSwitched() override;

    // The other services' sounds (game UI's buttons): once, on the effects' bus.
    void PlaySound(const AssetRef& sound, float volume) override { PlayOnce(sound, volume, 1, nullptr); }
    float MusicVolume() const override { return Volume(BusVolume::Master) * Volume(BusVolume::Music); }

    // --- What scripts ask (RMAudio.h) ---
    VoiceId PlayOnce(const AssetRef& sound, float volume, float pitch, const Vec3* position);
    void Stop(VoiceId voice, float fade);
    bool Playing(VoiceId voice) const;
    void PlayMusic(const AssetRef& sound, float volume, float fade);
    void StopMusic(float fade);
    void SetVolume(BusVolume bus, float volume);
    float Volume(BusVolume bus) const;
    // An entity's (first) 音源; false when it has none.
    bool SourcePlay(game_production::Scene& scene, int entity);
    bool SourceStop(game_production::Scene& scene, int entity);
    bool SourcePause(game_production::Scene& scene, int entity, bool paused);
    bool SourcePlaying(game_production::Scene& scene, int entity);

    audio::AudioEngine& Engine() { return *engine_; }

    // For the editor's デバッグ panel: the device, the voices sounding, the 音源 known, the music's file
    // (empty: none) and the volumes now.
    struct Facts {
        bool device = false;
        size_t voices = 0;
        size_t sources = 0;
        std::string music;
        float master = 1, musicVolume = 1, effects = 1;
    };
    Facts DebugFacts() const;
    VoiceId Music() const { return music_; }

private:
    struct Source {
        int entity = 0;
        VoiceId voice = 0;
        float fade = 0;
        bool paused = false;  // by a script
    };
    using SourceKey = std::pair<const void*, int>;  // the component's object and its entity
    struct KeyHash {
        size_t operator()(const SourceKey& k) const { return std::hash<const void*>()(k.first) ^ (static_cast<size_t>(k.second) * 0x9E3779B97F4A7C15ull); }
    };

    const audio::SoundSource* Sound(const AssetRef& ref);
    VoiceId StartSource(const AudioSource& c, Source& s, game_production::Scene& scene);
    VoiceId StartMusic(const AssetRef& sound, audio::PlayParams params, double fade, const void* owner);
    void StopVoice(Source& s);
    std::pair<AudioSource*, Source*> SourceOf(game_production::Scene& scene, int entity);
    void Tell(int level, const std::string& text);

    const game_production::PlayHost& host_;
    std::unique_ptr<audio::AudioEngine> engine_;
    std::unordered_map<std::string, std::optional<audio::SoundSource>> sounds_;  // by asset id (nullopt: cannot be read)
    std::unordered_map<SourceKey, Source, KeyHash> sources_;
    float scriptVolume_[3] = {-1, -1, -1};  // set by a script (< 0: the game settings')
    // The music: its voice, its sound, who started it (a 音源's object; null: a script), and whether
    // its scene went (SceneSwitched: stopped at the next Frame unless taken over).
    VoiceId music_ = 0;
    std::string musicSound_;
    const void* musicOwner_ = nullptr;
    bool musicOrphan_ = false;
    float musicFade_ = 0;
};

// The scripts' part (RMAudioAbi.h): offered at game production's kScriptApiPoint.
const game_production::ScriptApiPart& ScriptAudioPart();

}  // namespace rm::game_x_audio
