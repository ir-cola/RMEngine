#pragma once
// 音 - the sound device and everything playing on it (XAudio2, in Windows 10 and later; 3D with
// X3DAudio). One per game that plays (the editor's Play, the exported game) - and one for the アセット
// panel's preview.
//
//   game thread   Play / Stop / SetPaused / SetVolume ... (quick: they only mark what is wanted)
//                 Update(dt) every frame: fades, 3D (where each voice is heard), voices that finished
//   its thread    reads the sounds (decodes a short one whole, once - shared by every voice of it -
//                 or opens a long one to stream), makes the XAudio2 voices and keeps each stream fed
//                 (4 pieces of 1/4 second queued: the game may stall up to a second without a gap)
//
// Voices go to two submixes - 効果音 and BGM - into the mastering voice (全体): the game settings'
// three volumes. Pitch: voices are made for up to kMaxPitch (XAudio2's MaxFrequencyRatio is 2 by
// default and refuses more). Pause = XAudio2's Stop (it keeps the place) / Start; a real stop also
// flushes the buffers and destroys the voice (both on the engine's thread, which owns them).
//
// Silent (no device - a PC without sound, the tests): voices are kept and time passes for them as if
// they played (a sound ends after its length), nothing is heard.
//
// Problems (a sound that cannot be read, no Media Foundation ...) are kept for the caller's console
// (TakeMessages): Japanese, once per sound.

#include "base/Math.h"
#include "core/asset/ImportSettings.h"
#include "rm_engine/audio/runtime/Decoder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rm::audio {

enum class Bus : uint8_t { Effects, Music };
// How a sound is read (its import setting ストリーミング): long ones streamed, short ones decoded whole.
enum class StreamMode : uint8_t { Auto, Always, Never };
inline constexpr double kStreamLongerThan = 10.0;  // seconds (Auto)

// A sound to play: its file's bytes and its import settings.
struct SoundSource {
    Bytes bytes;
    std::string key;   // the same key = the same sound (decoded once): its asset id
    std::string name;  // for messages (the file's name)
    float assetVolume = 1;
    StreamMode stream = StreamMode::Auto;
    double loopStart = 0, loopEnd = 0;  // seconds (loopEnd 0: the end)
};

// A sound's import settings (its .meta: 音量・ストリーミング・ループの点) onto it.
void ApplyImport(SoundSource& sound, const asset::SoundImport& import);

struct PlayParams {
    Bus bus = Bus::Effects;
    float volume = 1;   // what was asked (0..4)
    float pitch = 1;
    bool loop = false;
    bool spatial = false;  // heard from `position` (3D / 2D 距離で減衰)
    Vec3 position{0, 0, 0};
    float minDistance = 1, maxDistance = 30;
    double fadeIn = 0;   // seconds
    bool paused = false;  // made paused (an AudioSource whose game is paused)
};

class AudioEngine {
public:
    using VoiceId = uint64_t;  // 0: none

    explicit AudioEngine(bool silent = false);
    ~AudioEngine();  // everything stops (the voices and the thread are gone when it returns)
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool HasDevice() const;

    VoiceId Play(const SoundSource& sound, const PlayParams& params);
    void Stop(VoiceId id, double fadeOut = 0);
    void SetPaused(VoiceId id, bool paused);
    void SetVolume(VoiceId id, float volume);
    void SetPitch(VoiceId id, float pitch);
    void SetPosition(VoiceId id, Vec3 position);
    bool IsPlaying(VoiceId id) const;  // still there (being read, playing, paused or fading out)

    // Who hears (the game's camera). Right-handed, Y up, as the 3D space (a 2D game: z = 0).
    void SetListener(Vec3 position, Vec3 forward, Vec3 up);
    void SetVolumes(float master, float music, float effects);
    void PauseAll(bool paused);  // the game paused (⏸): every voice keeps its place
    void StopAll();              // at once (■: nothing is heard after it)
    size_t Voices() const;

    void Update(double dt);
    std::vector<std::string> TakeMessages();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace rm::audio
