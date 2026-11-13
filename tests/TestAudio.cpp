// Tests for 音 (plugins/rm_engine/audio/runtime): reading WAV in every form, broken files, Ogg that is
// not, the pieces of a stream and its loop, volumes / fades / distance, and the engine (silent: no
// device) - a sound ends after its length, a pause keeps it, stopping stops everything.

#include "Test.h"

#include "rm_engine/audio/runtime/AudioEngine.h"
#include "rm_engine/audio/runtime/Decoder.h"
#include "rm_engine/audio/runtime/Sound.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace rm;
using namespace rm::audio;

namespace {

// A WAV file by hand: `tag` 1 (PCM) / 3 (float) / 0xFFFE (extensible), `bits`, the sample bytes.
std::string Wav(uint16_t tag, uint16_t channels, uint32_t rate, uint16_t bits, const std::string& data, uint16_t subTag = 1) {
    std::string fmt;
    auto u16 = [](std::string& s, uint32_t v) {
        s.push_back(static_cast<char>(v & 0xFF));
        s.push_back(static_cast<char>((v >> 8) & 0xFF));
    };
    auto u32 = [&](std::string& s, uint32_t v) {
        u16(s, v & 0xFFFF);
        u16(s, v >> 16);
    };
    const uint16_t block = static_cast<uint16_t>(channels * bits / 8);
    u16(fmt, tag);
    u16(fmt, channels);
    u32(fmt, rate);
    u32(fmt, rate * block);
    u16(fmt, block);
    u16(fmt, bits);
    if (tag == 0xFFFE) {
        u16(fmt, 22);
        u16(fmt, bits);
        u32(fmt, 3);
        u16(fmt, subTag);  // the sub format GUID: its first 2 bytes say PCM / float
        fmt += std::string("\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", 14);
    }
    std::string out = "RIFF";
    u32(out, static_cast<uint32_t>(4 + 8 + fmt.size() + 8 + data.size()));
    out += "WAVE";
    out += "fmt ";
    u32(out, static_cast<uint32_t>(fmt.size()));
    out += fmt;
    out += "LIST";  // a chunk it does not know (skipped)
    u32(out, 4);
    out += "INFO";
    out += "data";
    u32(out, static_cast<uint32_t>(data.size()));
    out += data;
    return out;
}

std::span<const uint8_t> Span(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

std::vector<float> Read(const std::string& file) {
    const auto info = ParseWav(Span(file), SoundBudget{}, nullptr);
    std::vector<float> out(info ? info->frames * info->format.channels : 0);
    if (info) WavToFloat(Span(file), *info, 0, info->frames, out.data());
    return out;
}

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

template <class F>
bool Within(F done, double seconds = 3) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

}  // namespace

RM_TEST(Audio_Wav_EveryForm) {
    // 8-bit: 0..255 around 128
    CHECK((Read(Wav(1, 1, 8000, 8, std::string("\x00\x80\xFF", 3))) == std::vector<float>{-1.f, 0.f, 127.f / 128.f}));
    // 16-bit stereo
    const auto s16 = Read(Wav(1, 2, 44100, 16, std::string("\x00\x80\xFF\x7F\x00\x00\x00\x40", 8)));
    CHECK(s16.size() == 4 && Near(s16[0], -1.f) && Near(s16[1], 1.f, 1e-4f) && s16[2] == 0.f && Near(s16[3], 0.5f));
    // 24-bit (negative numbers keep their sign)
    const auto s24 = Read(Wav(1, 1, 48000, 24, std::string("\x00\x00\x80\xFF\xFF\x7F\x00\x00\xC0", 9)));
    CHECK(s24.size() == 3 && Near(s24[0], -1.f) && Near(s24[1], 1.f, 1e-4f) && Near(s24[2], -0.5f));
    // 32-bit PCM
    const auto s32 = Read(Wav(1, 1, 48000, 32, std::string("\x00\x00\x00\x40", 4)));
    CHECK(s32.size() == 1 && Near(s32[0], 0.5f));
    // float 32 and 64 (a loud value is kept to 1; NaN is silence)
    float f[3] = {0.25f, 3.f, std::nanf("")};
    const auto sf = Read(Wav(3, 1, 48000, 32, std::string(reinterpret_cast<const char*>(f), 12)));
    CHECK(sf.size() == 3 && sf[0] == 0.25f && sf[1] == 1.f && sf[2] == 0.f);
    double d = -0.75;
    const auto sd = Read(Wav(3, 1, 48000, 64, std::string(reinterpret_cast<const char*>(&d), 8)));
    CHECK(sd.size() == 1 && sd[0] == -0.75f);
    // WAVE_FORMAT_EXTENSIBLE (PCM 16 inside)
    const auto se = Read(Wav(0xFFFE, 1, 22050, 16, std::string("\x00\x40", 2), 1));
    CHECK(se.size() == 1 && Near(se[0], 0.5f));
    // What WriteWav16 writes reads back the same.
    const std::vector<float> wave = {0.f, 0.5f, -0.5f, 1.f};
    const auto back = Read(WriteWav16(wave, {11025, 2}));
    CHECK(back.size() == 4 && Near(back[1], 0.5f) && Near(back[2], -0.5f));
    const auto info = ParseWav(Span(WriteWav16(wave, {11025, 2})), SoundBudget{}, nullptr);
    CHECK(info && info->frames == 2 && info->format.sampleRate == 11025 && info->format.channels == 2);
}

RM_TEST(Audio_Wav_BrokenFiles) {
    std::string why;
    const SoundBudget budget;
    CHECK(!ParseWav(Span(std::string("hello")), budget, &why) && !why.empty());
    // A chunk claiming more than the file has.
    std::string big = Wav(1, 1, 8000, 16, std::string(4, '\0'));
    big[16] = '\xFF';  // fmt's size
    big[17] = '\xFF';
    CHECK(!ParseWav(Span(big), budget, &why));
    // A form it cannot read (12-bit), no channels, a sample rate past the budget.
    CHECK(!ParseWav(Span(Wav(1, 1, 8000, 12, std::string(4, '\0'))), budget, &why));
    CHECK(!ParseWav(Span(Wav(1, 0, 8000, 16, std::string(4, '\0'))), budget, &why));
    CHECK(!ParseWav(Span(Wav(1, 1, 999999, 16, std::string(4, '\0'))), budget, &why));
    // A recording cut off (data longer than the file): what is there plays.
    std::string cut = Wav(1, 1, 8000, 16, std::string(8, '\0'));
    cut.resize(cut.size() - 4);
    const auto info = ParseWav(Span(cut), budget, &why);
    CHECK(info && info->frames == 2);
    // The decoders: not a sound, an Ogg that is not Vorbis (garbage), too big for the budget.
    CHECK(!OpenDecoder(std::make_shared<const std::string>("OggS-not-really-vorbis-at-all"), budget, &why) && !why.empty());
    SoundBudget tiny;
    tiny.maxFileBytes = 10;
    CHECK(!OpenDecoder(std::make_shared<const std::string>(Wav(1, 1, 8000, 16, std::string(4, '\0'))), tiny, &why));
    CHECK(!OpenDecoder(std::make_shared<const std::string>(), budget, &why));
}

RM_TEST(Audio_DecoderAndWhole) {
    std::vector<float> wave(1000);
    for (size_t i = 0; i < wave.size(); ++i) wave[i] = static_cast<float>(std::sin(i * 0.1));
    auto bytes = std::make_shared<const std::string>(WriteWav16(wave, {8000, 1}));
    std::string why;
    auto d = OpenDecoder(bytes, SoundBudget{}, &why);
    CHECK(d && d->frames == 1000 && std::string_view(d->codec) == "WAV");
    float piece[300];
    CHECK(d->Read(piece, 300) == 300 && Near(piece[10], wave[10], 1e-3f));
    CHECK(d->Seek(990) && d->Read(piece, 300) == 10 && d->Read(piece, 300) == 0);
    CHECK(d->Seek(0));
    const auto pcm = DecodeWhole(*d, SoundBudget{}, &why);
    CHECK(pcm && pcm->Frames() == 1000);
    // Longer than what is decoded whole: refused (it is streamed instead).
    SoundBudget shortOnes;
    shortOnes.maxWholeSeconds = 0.05;
    CHECK(d->Seek(0) && !DecodeWhole(*d, shortOnes, &why));
    const auto info = Describe(bytes, SoundBudget{}, &why);
    CHECK(info && Near(static_cast<float>(info->seconds), 0.125f) && info->codec == "WAV");
    (void)MediaFoundationAvailable();  // (true or false: either is a PC the game runs on)
}

RM_TEST(Audio_StreamPiecesAndLoops) {
    // Not looping: pieces up to the end, the last says so.
    Piece p = NextPiece(0, 1000, 400, false, {});
    CHECK(p.from == 0 && p.frames == 400 && !p.ends);
    p = NextPiece(800, 1000, 400, false, {});
    CHECK(p.from == 800 && p.frames == 200 && p.ends);
    // Looping over [200, 900): a piece stops at the loop's end, the next starts at its start.
    const LoopPoints lp{200, 900};
    p = NextPiece(700, 1000, 400, true, lp);
    CHECK(p.from == 700 && p.frames == 200 && !p.ends);
    p = NextPiece(900, 1000, 400, true, lp);
    CHECK(p.from == 200 && p.frames == 400 && !p.ends);
    // Loop points from seconds: kept inside; a broken pair = the whole.
    CHECK((LoopFrames(1.0, 2.0, 100, 1000).start == 100 && LoopFrames(1.0, 2.0, 100, 1000).end == 200));
    CHECK(LoopFrames(0, 0, 100, 1000).end == 1000);
    CHECK((LoopFrames(5.0, 2.0, 100, 1000).start == 0 && LoopFrames(5.0, 2.0, 100, 1000).end == 1000));
    CHECK(LoopFrames(1.0, 99.0, 100, 1000).end == 1000);
}

RM_TEST(Audio_Loudness) {
    CHECK(Gain(1, 1, 1, 1) == 1.f && Near(Gain(0.5f, 0.5f, 2, 1), 0.5f) && Gain(1, 1, 1, -3) == 0.f && Gain(9, 1, 1, 1) == 4.f);
    CHECK(Gain(std::nanf(""), 1, 1, 1) == 0.f);
    // A crossfade at equal power: in² + out² = 1 all along.
    for (double t = 0; t <= 2.0; t += 0.25) {
        const float in = FadeGain(t, 2.0, true), out = FadeGain(t, 2.0, false);
        CHECK(Near(in * in + out * out, 1.f));
    }
    CHECK(FadeGain(0, 2, true) == 0.f && Near(FadeGain(2, 2, true), 1.f) && FadeGain(0, 0, false) == 0.f);
    // Distance: full up to min, softer further, silent from max, never louder further away.
    CHECK(DistanceGain(0.5f, 1, 30) == 1.f && DistanceGain(30, 1, 30) == 0.f && DistanceGain(50, 1, 30) == 0.f);
    CHECK(Near(DistanceGain(2, 1, 30), 0.5f));
    float last = 1;
    for (float dd = 1; dd <= 31; dd += 0.5f) {
        const float g = DistanceGain(dd, 1, 30);
        CHECK(g <= last + 1e-6f);
        last = g;
    }
    CHECK(ClampPitch(100) == kMaxPitch && ClampPitch(0) == kMinPitch && ClampPitch(std::nanf("")) == 1.f);
    CHECK(LengthText(83.45) == "1:23.4" && LengthText(5) == "0:05.0");
}

RM_TEST(Audio_Engine_StopsWhenAsked) {
    AudioEngine engine(true);  // silent: no device, time passes as if it played
    CHECK(!engine.HasDevice());
    std::vector<float> wave(800, 0.1f);  // 0.1 s at 8 kHz
    SoundSource s;
    s.bytes = std::make_shared<const std::string>(WriteWav16(wave, {8000, 1}));
    s.key = "beep";
    s.name = "beep.wav";
    // It ends after its length.
    const auto a = engine.Play(s, {});
    CHECK(engine.IsPlaying(a) && engine.Voices() == 1);
    CHECK(Within([&] {
        engine.Update(0.02);
        return !engine.IsPlaying(a);
    }));
    CHECK(engine.Voices() == 0);
    // Looping: it goes on; paused, time does not count; stopped, it is gone at once.
    PlayParams loop;
    loop.loop = true;
    const auto b = engine.Play(s, loop);
    const auto c = engine.Play(s, {});
    engine.PauseAll(true);
    for (int i = 0; i < 20; ++i) {
        engine.Update(0.05);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(engine.IsPlaying(b) && engine.IsPlaying(c));  // (paused: the short one has not ended)
    engine.PauseAll(false);
    engine.StopAll();
    CHECK(engine.Voices() == 0 && !engine.IsPlaying(b) && !engine.IsPlaying(c));
    // A fade-out: still there during it, gone after.
    const auto e = engine.Play(s, loop);
    CHECK(Within([&] {
        engine.Update(0.01);
        return engine.IsPlaying(e);
    }));
    engine.Stop(e, 0.5);
    for (int i = 0; i < 5; ++i) engine.Update(0.05);
    CHECK(engine.IsPlaying(e));
    CHECK(Within([&] {
        engine.Update(0.1);
        return !engine.IsPlaying(e);
    }));
    // A sound that cannot be read: told once, nothing plays.
    SoundSource bad;
    bad.bytes = std::make_shared<const std::string>("not a sound");
    bad.key = "bad";
    bad.name = "bad.wav";
    engine.Play(bad, {});
    engine.Play(bad, {});
    CHECK(Within([&] { return engine.Voices() == 0; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(engine.TakeMessages().size() == 1);
}
