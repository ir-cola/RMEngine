#pragma once
// RM Engine - sounds in scripts (ゲーム制作 × 音). Sounds are assets: an rm::SoundRef field picked in
// the インスペクター (drag a .wav / .ogg from the アセット panel).
//
//   RM_PROPERTY(Edit, Label="ジャンプの音") rm::SoundRef jump;
//   ...
//   if (rm::Input::Pressed("Jump")) rm::Audio::Play(jump);
//   rm::Audio::PlayMusic(stageBgm, 2.0f);              // cross-fades from the music before (2 seconds)
//   rm::AudioSource(self).Stop();                      // the entity's 音源 (the component)
//
// Everything stops when the game stops (■ in the editor). Without the plugin (or a sound device) it all
// does nothing.

#include "RM.h"
#include "RMAudioAbi.h"

namespace rm {

namespace Audio {
namespace detail {
inline const RmAudioApi* Table() {
    static const RmAudioApi* api = Part<RmAudioApi>(RM_PART_AUDIO, RM_PART_AUDIO_VERSION);  // the engine's, for good
    return api;
}
}  // namespace detail

enum class Bus { Master = 0, Music = 1, Effects = 2 };
using Voice = uint64_t;  // one sound playing (0: none)

// An effect, once - heard the same everywhere.
inline Voice Play(const SoundRef& sound, float volume = 1, float pitch = 1) {
    const RmAudioApi* t = detail::Table();
    return t ? t->play(Api()->ctx, sound.guid, volume, pitch, nullptr) : 0;
}
// An effect, once - heard from `position` (smaller farther from the game's camera; left / right).
inline Voice PlayAt(const SoundRef& sound, Vec3 position, float volume = 1, float pitch = 1) {
    const RmAudioApi* t = detail::Table();
    const float p[3] = {position.x, position.y, position.z};
    return t ? t->play(Api()->ctx, sound.guid, volume, pitch, p) : 0;
}
inline void Stop(Voice voice, float fadeSeconds = 0) {
    if (const RmAudioApi* t = detail::Table()) t->stop(Api()->ctx, voice, fadeSeconds);
}
inline bool IsPlaying(Voice voice) {
    const RmAudioApi* t = detail::Table();
    return t && t->playing(Api()->ctx, voice) != 0;
}
// The game's music (BGM, looped): the music before fades out over `fadeSeconds` while this fades in.
// The same sound already playing as the music goes on as it is.
inline void PlayMusic(const SoundRef& sound, float fadeSeconds = 1, float volume = 1) {
    if (const RmAudioApi* t = detail::Table()) t->playMusic(Api()->ctx, sound.guid, volume, fadeSeconds);
}
inline void StopMusic(float fadeSeconds = 1) {
    if (const RmAudioApi* t = detail::Table()) t->stopMusic(Api()->ctx, fadeSeconds);
}
// 全体 / BGM / 効果音 (they start as the game settings' 音量: an options screen changes them).
inline void SetVolume(Bus bus, float volume) {
    if (const RmAudioApi* t = detail::Table()) t->setVolume(Api()->ctx, static_cast<int>(bus), volume);
}
inline float Volume(Bus bus) {
    const RmAudioApi* t = detail::Table();
    return t ? t->volume(Api()->ctx, static_cast<int>(bus)) : 0.f;
}
}  // namespace Audio

// The entity's 音源 (the component; Entity::Add / Get / Remove<rm::AudioSource>() too).
class AudioSource {
public:
    static constexpr const char* kComponentType = "rm::game_x_audio::AudioSource";
    explicit AudioSource(Entity entity) : entity_(entity) {}

    bool Valid() const {
        const RmAudioApi* t = Audio::detail::Table();
        return t && t->sourceSet(Api()->ctx, entity_.id, -1, 0) != 0;
    }
    void Play() const { Op(0); }    // from the start
    void Stop() const { Op(1); }    // (with its フェード)
    void Pause() const { Op(2); }
    void Resume() const { Op(3); }
    bool IsPlaying() const {
        const RmAudioApi* t = Audio::detail::Table();
        return t && t->sourcePlaying(Api()->ctx, entity_.id) != 0;
    }
    void SetVolume(float volume) const { Set(0, volume); }
    void SetPitch(float pitch) const { Set(1, pitch); }

private:
    void Op(int op) const {
        if (const RmAudioApi* t = Audio::detail::Table()) t->source(Api()->ctx, entity_.id, op);
    }
    void Set(int what, float value) const {
        if (const RmAudioApi* t = Audio::detail::Table()) t->sourceSet(Api()->ctx, entity_.id, what, value);
    }
    Entity entity_;
};

}  // namespace rm
