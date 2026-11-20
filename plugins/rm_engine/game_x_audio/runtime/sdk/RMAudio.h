#pragma once
// RM Engine - スクリプトの音 (ゲーム制作 × 音)。音はアセット: インスペクターで選ぶ
// rm::SoundRef のフィールド (アセットパネルから .wav / .ogg をドラッグする)。
//
//   RM_PROPERTY(Edit, Label="ジャンプの音") rm::SoundRef jump;
//   ...
//   if (rm::Input::Pressed("Jump")) rm::Audio::Play(jump);
//   rm::Audio::PlayMusic(stageBgm, 2.0f);              // 前の音楽からクロスフェード (2 秒)
//   rm::AudioSource(self).Stop();                      // エンティティの 音源 (部品)
//
// ゲームが止まる (エディターの ■) と、すべて止まる。プラグイン (か音のデバイス) がなければ、
// 何もしない。

#include "RM.h"
#include "RMAudioAbi.h"

namespace rm {

namespace Audio {
namespace detail {
inline const RmAudioApi* Table() {
    static const RmAudioApi* api = Part<RmAudioApi>(RM_PART_AUDIO, RM_PART_AUDIO_VERSION);  // エンジンのもの、ずっと
    return api;
}
}  // namespace detail

enum class Bus { Master = 0, Music = 1, Effects = 2 };
using Voice = uint64_t;  // 鳴っている 1 つの音 (0: なし)

// 効果音を 1 回 - どこでも同じに聞こえる。
inline Voice Play(const SoundRef& sound, float volume = 1, float pitch = 1) {
    const RmAudioApi* t = detail::Table();
    return t ? t->play(Api()->ctx, sound.guid, volume, pitch, nullptr) : 0;
}
// 効果音を 1 回 - `position` から聞こえる (ゲームのカメラから遠いほど小さい。左右も)。
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
// ゲームの音楽 (BGM、ループ): 前の音楽が `fadeSeconds` 秒かけてフェードアウトし、
// これがフェードインする。同じ音がもう音楽として鳴っていれば、そのまま続く。
inline void PlayMusic(const SoundRef& sound, float fadeSeconds = 1, float volume = 1) {
    if (const RmAudioApi* t = detail::Table()) t->playMusic(Api()->ctx, sound.guid, volume, fadeSeconds);
}
inline void StopMusic(float fadeSeconds = 1) {
    if (const RmAudioApi* t = detail::Table()) t->stopMusic(Api()->ctx, fadeSeconds);
}
// 全体 / BGM / 効果音 (始めはゲーム設定の 音量: オプション画面が変える)。
inline void SetVolume(Bus bus, float volume) {
    if (const RmAudioApi* t = detail::Table()) t->setVolume(Api()->ctx, static_cast<int>(bus), volume);
}
inline float Volume(Bus bus) {
    const RmAudioApi* t = detail::Table();
    return t ? t->volume(Api()->ctx, static_cast<int>(bus)) : 0.f;
}
}  // namespace Audio

// エンティティの 音源 (部品。Entity::Add / Get / Remove<rm::AudioSource>() でも)。
class AudioSource {
public:
    static constexpr const char* kComponentType = "rm::game_x_audio::AudioSource";
    explicit AudioSource(Entity entity) : entity_(entity) {}

    bool Valid() const {
        const RmAudioApi* t = Audio::detail::Table();
        return t && t->sourceSet(Api()->ctx, entity_.id, -1, 0) != 0;
    }
    void Play() const { Op(0); }    // 始めから
    void Stop() const { Op(1); }    // (フェードつき)
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
