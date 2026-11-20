#pragma once
// 音 - 音のデバイスと、その上で鳴っているものすべて (Windows 10 以降の XAudio2。3D は
// X3DAudio)。鳴らすゲーム 1 つにつき 1 つ (エディターの Play、書き出したゲーム) - それと
// アセット パネルのプレビュー用に 1 つ。
//
//   ゲームのスレッド   Play / Stop / SetPaused / SetVolume ... (速い: ほしい状態を印すだけ)
//                      Update(dt) を毎フレーム: フェード、3D (各ボイスが聞こえる方向)、終わったボイス
//   専用のスレッド     音を読み (短いものは丸ごと 1 回デコードして、その音のボイス全部で共有する。
//                      長いものは開いてストリームにする)、XAudio2 のボイスを作り、各ストリームに
//                      送り込み続ける (1/4 秒の断片を 4 つ待たせる: ゲームが 1 秒ほど止まっても途切れない)
//
// ボイスは 2 つのサブミックス - 効果音と BGM - を通って、マスタリングボイス (全体) に入る: ゲーム設定の
// 3 つの音量。ピッチ: ボイスは kMaxPitch まで作る (XAudio2 の MaxFrequencyRatio は既定で 2 で、それ以上は
// 断られる)。一時停止 = XAudio2 の Stop (位置を保つ) / Start。本当の停止はバッファを空にしてボイスも
// 壊す (どちらも、それらを持つエンジンのスレッドで)。
//
// 無音 (デバイスが無い - 音の出ない PC、テスト): ボイスは残り、鳴っているかのように時間が過ぎる
// (音は長さが過ぎれば終わる)。何も聞こえない。
//
// 問題 (読めない音、Media Foundation が無い ...) は、呼び出し側のコンソール用に取っておく
// (TakeMessages): 日本語で、音ごとに 1 回。

#include "base/Math.h"
#include "core/asset/ImportSettings.h"
#include "rm_engine/audio/runtime/Decoder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rm::audio {

enum class Bus : uint8_t { Effects, Music };
// 音の読み方 (取り込みの設定 ストリーミング): 長いものはストリーム、短いものは丸ごとデコード。
enum class StreamMode : uint8_t { Auto, Always, Never };
inline constexpr double kStreamLongerThan = 10.0;  // 秒 (Auto)

// 鳴らす音: ファイルのバイト列と取り込みの設定。
struct SoundSource {
    Bytes bytes;
    std::string key;   // 同じキー = 同じ音 (1 回だけデコード): そのアセットの id
    std::string name;  // メッセージ用 (ファイルの名前)
    float assetVolume = 1;
    StreamMode stream = StreamMode::Auto;
    double loopStart = 0, loopEnd = 0;  // 秒 (loopEnd が 0: 終わりまで)
};

// 音の取り込みの設定 (.meta: 音量・ストリーミング・ループの点) をその音に反映する。
void ApplyImport(SoundSource& sound, const asset::SoundImport& import);

struct PlayParams {
    Bus bus = Bus::Effects;
    float volume = 1;   // 頼まれた値 (0..4)
    float pitch = 1;
    bool loop = false;
    bool spatial = false;  // `position` から聞こえる (3D / 2D 距離で減衰)
    Vec3 position{0, 0, 0};
    float minDistance = 1, maxDistance = 30;
    double fadeIn = 0;   // 秒
    bool paused = false;  // 一時停止にして作る (ゲームが一時停止中の AudioSource)
};

class AudioEngine {
public:
    using VoiceId = uint64_t;  // 0: なし

    explicit AudioEngine(bool silent = false);
    ~AudioEngine();  // すべて止まる (戻るときにはボイスもスレッドも無くなっている)
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool HasDevice() const;

    VoiceId Play(const SoundSource& sound, const PlayParams& params);
    void Stop(VoiceId id, double fadeOut = 0);
    void SetPaused(VoiceId id, bool paused);
    void SetVolume(VoiceId id, float volume);
    void SetPitch(VoiceId id, float pitch);
    void SetPosition(VoiceId id, Vec3 position);
    bool IsPlaying(VoiceId id) const;  // まだある (読み込み中、鳴っている、一時停止中、フェードアウト中)

    // 聞く人 (ゲームのカメラ)。3D 空間と同じ右手系、Y が上 (2D のゲーム: z = 0)。
    void SetListener(Vec3 position, Vec3 forward, Vec3 up);
    void SetVolumes(float master, float music, float effects);
    void PauseAll(bool paused);  // ゲームが一時停止 (⏸): どのボイスも位置を保つ
    void StopAll();              // すぐに (■: そのあとは何も聞こえない)
    size_t Voices() const;

    void Update(double dt);
    std::vector<std::string> TakeMessages();

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace rm::audio
