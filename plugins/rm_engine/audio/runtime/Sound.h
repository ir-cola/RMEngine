#pragma once
// 音 - 音の純粋な部分 (デバイスも、ファイルも使わない): WAV を読むこと、ストリームを断片に切ること、
// ループの点、音量とフェード、距離による音の大きさ。tests/TestAudio.cpp でテストする。
//
// 設計 (TestEngine RME_PLATFORM.md 「音」): XAudio2 で鳴らし (AudioEngine.h)、このエンジンが読む
// (WAV はここ、Ogg Vorbis は stb_vorbis) ほか Media Foundation も読む (MP3 / AAC / FLAC: Decoder.h -
// どの Windows にもあるわけではない: N エディションには無い)。ゲームの音は WAV か OGG が一番よい。
//
// すべて 32 ビット float のサンプルをインターリーブして (L R L R ...) 鳴らす。

#include "base/Math.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rm::audio {

struct Format {
    uint32_t sampleRate = 0;  // 1 秒あたりのフレーム数
    uint16_t channels = 0;    // 1 はモノラル、2 はステレオ ...
    bool operator==(const Format&) const = default;
};

// 音のファイルが取ってよい大きさ (3D モデルローダーの ReadBudget と同じ考え: 壊れた、または悪意ある
// ファイルでも、エンジンが何ギガバイトも読んだり、際限なく確保したりしない)。`maxWholeSeconds` より
// 長い音は必ずストリームにする (丸ごとメモリにデコードしない)。
struct SoundBudget {
    uint64_t maxFileBytes = 1ull << 30;  // 1 GB
    uint32_t maxChannels = 8;
    uint32_t minSampleRate = 1000;
    uint32_t maxSampleRate = 384000;
    double maxWholeSeconds = 60 * 10;  // ここまでは丸ごとデコードする (48 kHz ステレオ float で 230 MB)
    double maxSeconds = 60 * 60 * 6;   // これより長いものは音ではない (長さが壊れている)
};

// --- WAV (RIFF) - このエンジンが読む ------------------------------------------------------------
enum class SampleKind : uint8_t { Pcm8, Pcm16, Pcm24, Pcm32, Float32, Float64 };
struct WavInfo {
    Format format;
    SampleKind sample = SampleKind::Pcm16;
    size_t dataOffset = 0;  // サンプルがファイルのどこから始まるか
    size_t dataBytes = 0;
    uint64_t frames = 0;
};
size_t BytesPerSample(SampleKind k);
bool LooksLikeWav(std::span<const uint8_t> bytes);
// ファイルのチャンクを調べたもの (サイズがファイルの中に収まる、サンプルの種類が分かる、PCM 8 / 16 / 24 / 32
// か float 32 / 64、WAVE_FORMAT_EXTENSIBLE も); 鳴らせないものでは nullopt と *error (日本語)。
std::optional<WavInfo> ParseWav(std::span<const uint8_t> bytes, const SoundBudget& budget, std::string* error);
// フレーム `from` から `frames` フレームを、-1..1 の float (インターリーブ) で `out` (frames * channels) へ。
// 終わりを越えたフレームは読まない: 返すのは読んだ数。
size_t WavToFloat(std::span<const uint8_t> bytes, const WavInfo& info, uint64_t from, size_t frames, float* out);
// 16 ビットのサンプルの WAV ファイル (エンジン自身の音: テンプレートのサンプル、テスト)。
std::string WriteWav16(std::span<const float> samples, Format format);

// --- ストリームとループ ------------------------------------------------------------------------
// 繰り返す部分: フレームで [start, end)。end 0 = 音の終わり。
struct LoopPoints {
    uint64_t start = 0, end = 0;
};
// 秒 (取り込みの設定) から。音の中に収める (start が end より前。壊れた組 = 音全体)。
LoopPoints LoopFrames(double startSeconds, double endSeconds, uint32_t sampleRate, uint64_t totalFrames);
// ストリームの、フレーム `pos` にある次の断片: `from` から `frames` フレームを読む (ループする音は
// 終わりでループの先頭へ戻る - 断片はそこで止まり、次の断片がそこから始まる)。
// `ends`: このあとには何も続かない (ループせず、終わりに着いた)。
struct Piece {
    uint64_t from = 0;
    size_t frames = 0;
    bool ends = false;
};
Piece NextPiece(uint64_t pos, uint64_t totalFrames, size_t want, bool loop, LoopPoints loopPoints);

// --- 音の大きさ -------------------------------------------------------------------------------
// ボイスが鳴らすときのゲイン: ゲームの 全体 x その種類 (BGM / 効果音) x アセットの取り込みの設定 x
// ゲームが頼んだもの (ソースの 音量、スクリプトの volume) - どれも 0..4 に収める。
float Gain(float master, float kind, float asset, float asked);
// `elapsed` から `seconds` かけるフェード: in = 0 -> 1、out = 1 -> 0 (等パワー: クロスフェードで
// 真ん中が凹まない)。seconds <= 0: すぐに終わる。
float FadeGain(double elapsed, double seconds, bool in);
// `distance` の所での音の大きさ (3D / 2D 距離で減衰): `minDistance` までは 1、そのあと min / d で、
// `maxDistance` で 0 になる (最後の部分は細くなって、段で止まらず無音になる)。
float DistanceGain(float distance, float minDistance, float maxDistance);
// ボイスが作られた範囲に収めたピッチ (XAudio2 の MaxFrequencyRatio - 既定の 2 では、それより高いものは
// 断られる: エンジンは kMaxPitch までのボイスを作る)。
inline constexpr float kMinPitch = 0.05f;
inline constexpr float kMaxPitch = 8.0f;
float ClampPitch(float pitch);

// "1:23.4" (分:秒.十分の一秒) - アセット パネルに出す音の長さ。
std::string LengthText(double seconds);

}  // namespace rm::audio
