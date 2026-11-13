#pragma once
// 音 - the pure part of sound (no device, no files): reading WAV, how a stream is cut into pieces,
// loop points, volumes and fades, and how loud a sound is at a distance. Tested in tests/TestAudio.cpp.
//
// Design (TestEngine RME_PLATFORM.md 「音」): played with XAudio2 (AudioEngine.h), read by this engine
// (WAV here, Ogg Vorbis with stb_vorbis) and by Media Foundation (MP3 / AAC / FLAC: Decoder.h - not
// on every Windows: the N editions have none). A game's sounds are best WAV or OGG.
//
// Everything is played as 32-bit float samples, interleaved (L R L R ...).

#include "base/Math.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rm::audio {

struct Format {
    uint32_t sampleRate = 0;  // frames a second
    uint16_t channels = 0;    // 1 mono, 2 stereo ...
    bool operator==(const Format&) const = default;
};

// What a sound file may be (as the 3D model loader's ReadBudget: a broken or a hostile file never
// makes the engine read gigabytes or allocate without end). A sound longer than `maxWholeSeconds` is
// always streamed (never decoded whole into memory).
struct SoundBudget {
    uint64_t maxFileBytes = 1ull << 30;  // 1 GB
    uint32_t maxChannels = 8;
    uint32_t minSampleRate = 1000;
    uint32_t maxSampleRate = 384000;
    double maxWholeSeconds = 60 * 10;  // decoded whole up to this (at 48 kHz stereo float: 230 MB)
    double maxSeconds = 60 * 60 * 6;   // longer than this is not a sound (a broken length)
};

// --- WAV (RIFF) - read by this engine ------------------------------------------------------------
enum class SampleKind : uint8_t { Pcm8, Pcm16, Pcm24, Pcm32, Float32, Float64 };
struct WavInfo {
    Format format;
    SampleKind sample = SampleKind::Pcm16;
    size_t dataOffset = 0;  // where the samples start in the file
    size_t dataBytes = 0;
    uint64_t frames = 0;
};
size_t BytesPerSample(SampleKind k);
bool LooksLikeWav(std::span<const uint8_t> bytes);
// The file's chunks checked (sizes inside the file, a known sample kind, PCM 8 / 16 / 24 / 32 or float
// 32 / 64, WAVE_FORMAT_EXTENSIBLE too); nullopt and *error (Japanese) when it is not one to play.
std::optional<WavInfo> ParseWav(std::span<const uint8_t> bytes, const SoundBudget& budget, std::string* error);
// `frames` frames from frame `from`, as float -1..1 (interleaved) into `out` (frames * channels).
// Frames past the end are not read: the number read.
size_t WavToFloat(std::span<const uint8_t> bytes, const WavInfo& info, uint64_t from, size_t frames, float* out);
// A WAV file of 16-bit samples (the engine's own sounds: the templates' samples, the tests).
std::string WriteWav16(std::span<const float> samples, Format format);

// --- Streams and loops --------------------------------------------------------------------------
// The part that repeats: [start, end) in frames; end 0 = the end of the sound.
struct LoopPoints {
    uint64_t start = 0, end = 0;
};
// From seconds (an import setting), kept inside the sound (start before end; a broken pair = the whole).
LoopPoints LoopFrames(double startSeconds, double endSeconds, uint32_t sampleRate, uint64_t totalFrames);
// The next piece of a stream at frame `pos`: read `frames` frames from `from` (a looping sound goes
// back to the loop's start at its end - the piece stops there and the next one starts from it).
// `ends`: nothing comes after it (not looping, the end reached).
struct Piece {
    uint64_t from = 0;
    size_t frames = 0;
    bool ends = false;
};
Piece NextPiece(uint64_t pos, uint64_t totalFrames, size_t want, bool loop, LoopPoints loopPoints);

// --- Loudness -------------------------------------------------------------------------------------
// The gain a voice plays with: the game's 全体 x its kind (BGM / 効果音) x the asset's import setting x
// what the game asked (the source's 音量, a script's volume) - each kept to 0..4.
float Gain(float master, float kind, float asset, float asked);
// A fade over `seconds` after `elapsed`: in = 0 -> 1, out = 1 -> 0 (equal power: a crossfade does not
// dip in the middle). seconds <= 0: done at once.
float FadeGain(double elapsed, double seconds, bool in);
// How loud a sound is at `distance` (3D / 2D 距離で減衰): 1 up to `minDistance`, then min / d, reaching
// 0 at `maxDistance` (the last part tapers so it goes silent rather than stopping at a step).
float DistanceGain(float distance, float minDistance, float maxDistance);
// A pitch kept inside what a voice was made for (XAudio2's MaxFrequencyRatio - its default of 2 would
// refuse anything higher: the engine makes voices for kMaxPitch).
inline constexpr float kMinPitch = 0.05f;
inline constexpr float kMaxPitch = 8.0f;
float ClampPitch(float pitch);

// "1:23.4" (minutes:seconds.tenths) - a sound's length for the アセット panel.
std::string LengthText(double seconds);

}  // namespace rm::audio
