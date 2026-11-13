#pragma once
// 音 - turning a sound file into samples, a piece at a time (a long BGM is never decoded whole).
//
//   WAV                this engine (Sound.h: PCM 8 / 16 / 24 / 32, float 32 / 64)
//   Ogg Vorbis         stb_vorbis (public domain / MIT: runtime/third_party) - Media Foundation cannot
//                      read OGG on every PC
//   MP3 / AAC / FLAC   Media Foundation (Windows). Loaded when first needed (LoadLibrary): a PC without
//                      it (the Windows N editions) still runs the game - those sounds are then silent,
//                      with the reason on the console / in game.log once.
//
// Which one is chosen by the file's first bytes (not its name). A decoder reads from the bytes it was
// given (shared: one sound playing several times shares them). Any thread; one decoder is used by one
// thread at a time. Media Foundation is used on a thread that called CoInitializeEx (the engine's).

#include "rm_engine/audio/runtime/Sound.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::audio {

using Bytes = std::shared_ptr<const std::string>;

class Decoder {
public:
    virtual ~Decoder() = default;
    Format format;
    uint64_t frames = 0;   // the length (0: unknown - read until Read gives 0)
    const char* codec = "";  // "WAV" / "Ogg Vorbis" / "Media Foundation"
    // Up to `frames` frames into `out` (frames * channels floats); 0 at the end.
    virtual size_t Read(float* out, size_t frames) = 0;
    // To frame `frame` (a loop going back). False when it cannot.
    virtual bool Seek(uint64_t frame) = 0;
};

// The decoder for `bytes`; null and *error (Japanese, with what to do) when it cannot be read.
std::unique_ptr<Decoder> OpenDecoder(Bytes bytes, const SoundBudget& budget, std::string* error);

// The whole sound as float samples (a short sound effect: decoded once, played many times). Refuses
// what is longer than budget.maxWholeSeconds (it is to be streamed).
struct Pcm {
    Format format;
    std::vector<float> samples;  // interleaved
    uint64_t Frames() const { return format.channels ? samples.size() / format.channels : 0; }
};
std::optional<Pcm> DecodeWhole(Decoder& decoder, const SoundBudget& budget, std::string* error);

// What a sound is (the アセット panel): its form, length and reader. nullopt: it cannot be read.
struct SoundInfo {
    Format format;
    double seconds = 0;
    std::string codec;
};
std::optional<SoundInfo> Describe(Bytes bytes, const SoundBudget& budget, std::string* error);

// Media Foundation is on this PC (loaded and started; asked once).
bool MediaFoundationAvailable();

}  // namespace rm::audio
