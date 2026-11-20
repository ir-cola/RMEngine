#pragma once
// ゲーム制作 × 音 - 1 回の Play (ゲームの) の音: game production の PlayService
// (game_production/runtime/PlayServices.h)。ゲームが動き始めるときに作り、■ で消える。
// その AudioEngine (rm_engine/audio) も一緒に消えるので、そのあとは何も聞こえない。
//
// 毎フレーム (スクリプトとシステムのあとで):
//   音量              ゲーム設定の 音量 (全体・BGM・効果音)、またはスクリプトが決めた値
//   聞く人            ゲームのカメラ (どこにいて何を見ているか: 空間の PlaceInfo)
//   音源 部品         新しいもの (Play、切り替えたシーン、オンにした、生成した) は
//                     始めから鳴らす なら鳴り始める。なくなったもの (削除、オフ) は止まる
//                     (フェードつき)。他は 音量 / ピッチ と、エンティティの場所に追従する
// 音楽 (BGM): 同時に 1 つ。1 つ始めると前のものがフェードアウトする (クロスフェード)。同じ音が
// もう音楽として鳴っていれば、そのまま続く (同じ BGM のシーンに切り替えても、また始めない)。
//
// 音はホスト (PlayHost::asset) から来る: エディターは .meta つきのファイル、ゲームは
// .rmsound (SoundPack.h)。どの音も 1 回の Play で一度だけ読む。

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
    // シーンが切り替わった: 前のシーンのすべての 音源 が止まる (フェードつき)。その音楽は、新しい
    // シーンが同じ音を音楽として始めるときだけ続く (次の Frame が決める)。
    void SceneSwitched() override;

    // 他のサービスの音 (ゲームの UI のボタン): 1 回だけ、効果音のバスで。
    void PlaySound(const AssetRef& sound, float volume) override { PlayOnce(sound, volume, 1, nullptr); }
    float MusicVolume() const override { return Volume(BusVolume::Master) * Volume(BusVolume::Music); }

    // --- スクリプトが頼むこと (RMAudio.h) ---
    VoiceId PlayOnce(const AssetRef& sound, float volume, float pitch, const Vec3* position);
    void Stop(VoiceId voice, float fade);
    bool Playing(VoiceId voice) const;
    void PlayMusic(const AssetRef& sound, float volume, float fade);
    void StopMusic(float fade);
    void SetVolume(BusVolume bus, float volume);
    float Volume(BusVolume bus) const;
    // エンティティの (最初の) 音源。ないときは false。
    bool SourcePlay(game_production::Scene& scene, int entity);
    bool SourceStop(game_production::Scene& scene, int entity);
    bool SourcePause(game_production::Scene& scene, int entity, bool paused);
    bool SourcePlaying(game_production::Scene& scene, int entity);

    audio::AudioEngine& Engine() { return *engine_; }

    // エディターの デバッグ パネル用: デバイス、鳴っている声、知っている 音源、音楽のファイル
    // (空: なし)、今の音量。
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
        bool paused = false;  // スクリプトによる
    };
    using SourceKey = std::pair<const void*, int>;  // 部品のオブジェクトとそのエンティティ
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
    std::unordered_map<std::string, std::optional<audio::SoundSource>> sounds_;  // アセット id ごと (nullopt: 読めない)
    std::unordered_map<SourceKey, Source, KeyHash> sources_;
    float scriptVolume_[3] = {-1, -1, -1};  // スクリプトが決めた値 (< 0: ゲーム設定のもの)
    // 音楽: その声、その音、始めたもの (音源のオブジェクト。null: スクリプト)、そのシーンが
    // なくなったか (SceneSwitched: 引き継がれなければ次の Frame で止める)。
    VoiceId music_ = 0;
    std::string musicSound_;
    const void* musicOwner_ = nullptr;
    bool musicOrphan_ = false;
    float musicFade_ = 0;
};

// スクリプトの部分 (RMAudioAbi.h): game production の kScriptApiPoint に出す。
const game_production::ScriptApiPart& ScriptAudioPart();

}  // namespace rm::game_x_audio
