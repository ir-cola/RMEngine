// 音 (plugins/rm_engine/audio/runtime) のテスト: あらゆる形の WAV の読み込み、壊れたファイル、
// Ogg ではないもの、ストリームの断片とそのループ、音量・フェード・距離、そしてエンジン
// (無音: デバイスなし) - 音は長さのあとで終わり、ポーズは保ち、停止はすべてを止める。

#include "Test.h"

#include "editor/lang/Lang.h"
#include "platform/Win.h"
#include "rm_engine/audio/editor/Describer.h"
#include "rm_engine/audio/runtime/AudioEngine.h"
#include "rm_engine/audio/runtime/Decoder.h"
#include "rm_engine/audio/runtime/Sound.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace rm;
using namespace rm::audio;

namespace {

// 手作りの WAV ファイル: `tag` 1 (PCM) / 3 (float) / 0xFFFE (extensible)、`bits`、サンプルのバイト。
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
        u16(fmt, subTag);  // サブフォーマット GUID: 最初の 2 バイトが PCM / float を表す
        fmt += std::string("\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", 14);
    }
    std::string out = "RIFF";
    u32(out, static_cast<uint32_t>(4 + 8 + fmt.size() + 8 + data.size()));
    out += "WAVE";
    out += "fmt ";
    u32(out, static_cast<uint32_t>(fmt.size()));
    out += fmt;
    out += "LIST";  // 知らないチャンク (読み飛ばす)
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
    // 8 ビット: 0..255 で 128 が中心
    CHECK((Read(Wav(1, 1, 8000, 8, std::string("\x00\x80\xFF", 3))) == std::vector<float>{-1.f, 0.f, 127.f / 128.f}));
    // 16 ビットのステレオ
    const auto s16 = Read(Wav(1, 2, 44100, 16, std::string("\x00\x80\xFF\x7F\x00\x00\x00\x40", 8)));
    CHECK(s16.size() == 4 && Near(s16[0], -1.f) && Near(s16[1], 1.f, 1e-4f) && s16[2] == 0.f && Near(s16[3], 0.5f));
    // 24 ビット (負の数は符号を保つ)
    const auto s24 = Read(Wav(1, 1, 48000, 24, std::string("\x00\x00\x80\xFF\xFF\x7F\x00\x00\xC0", 9)));
    CHECK(s24.size() == 3 && Near(s24[0], -1.f) && Near(s24[1], 1.f, 1e-4f) && Near(s24[2], -0.5f));
    // 32 ビット PCM
    const auto s32 = Read(Wav(1, 1, 48000, 32, std::string("\x00\x00\x00\x40", 4)));
    CHECK(s32.size() == 1 && Near(s32[0], 0.5f));
    // float 32 と 64 (大きな値は 1 に収める。NaN は無音)
    float f[3] = {0.25f, 3.f, std::nanf("")};
    const auto sf = Read(Wav(3, 1, 48000, 32, std::string(reinterpret_cast<const char*>(f), 12)));
    CHECK(sf.size() == 3 && sf[0] == 0.25f && sf[1] == 1.f && sf[2] == 0.f);
    double d = -0.75;
    const auto sd = Read(Wav(3, 1, 48000, 64, std::string(reinterpret_cast<const char*>(&d), 8)));
    CHECK(sd.size() == 1 && sd[0] == -0.75f);
    // WAVE_FORMAT_EXTENSIBLE (中身は PCM 16)
    const auto se = Read(Wav(0xFFFE, 1, 22050, 16, std::string("\x00\x40", 2), 1));
    CHECK(se.size() == 1 && Near(se[0], 0.5f));
    // WriteWav16 が書いたものは、同じに読み戻せる。
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
    // ファイルにある量より多いと主張するチャンク。
    std::string big = Wav(1, 1, 8000, 16, std::string(4, '\0'));
    big[16] = '\xFF';  // fmt のサイズ
    big[17] = '\xFF';
    CHECK(!ParseWav(Span(big), budget, &why));
    // 読めない形 (12 ビット)、チャンネルなし、予算を超えるサンプルレート。
    CHECK(!ParseWav(Span(Wav(1, 1, 8000, 12, std::string(4, '\0'))), budget, &why));
    CHECK(!ParseWav(Span(Wav(1, 0, 8000, 16, std::string(4, '\0'))), budget, &why));
    CHECK(!ParseWav(Span(Wav(1, 1, 999999, 16, std::string(4, '\0'))), budget, &why));
    // 録音が途中で切れた (data がファイルより長い): ある分は鳴る。
    std::string cut = Wav(1, 1, 8000, 16, std::string(8, '\0'));
    cut.resize(cut.size() - 4);
    const auto info = ParseWav(Span(cut), budget, &why);
    CHECK(info && info->frames == 2);
    // デコーダー: 音ではないもの、Vorbis ではない Ogg (ゴミ)、予算に対して大きすぎるもの。
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
    // 丸ごとデコードする上限より長い: 断る (代わりにストリームで鳴らす)。
    SoundBudget shortOnes;
    shortOnes.maxWholeSeconds = 0.05;
    CHECK(d->Seek(0) && !DecodeWhole(*d, shortOnes, &why));
    const auto info = Describe(bytes, SoundBudget{}, &why);
    CHECK(info && Near(static_cast<float>(info->seconds), 0.125f) && info->codec == "WAV");
    (void)MediaFoundationAvailable();  // (true でも false でも: どちらもゲームが動く PC)
}

RM_TEST(Audio_StreamPiecesAndLoops) {
    // ループしない: 終わりまでの断片で、最後のものがそう告げる。
    Piece p = NextPiece(0, 1000, 400, false, {});
    CHECK(p.from == 0 && p.frames == 400 && !p.ends);
    p = NextPiece(800, 1000, 400, false, {});
    CHECK(p.from == 800 && p.frames == 200 && p.ends);
    // [200, 900) でループ: 断片はループの終わりで止まり、次は始まりから始まる。
    const LoopPoints lp{200, 900};
    p = NextPiece(700, 1000, 400, true, lp);
    CHECK(p.from == 700 && p.frames == 200 && !p.ends);
    p = NextPiece(900, 1000, 400, true, lp);
    CHECK(p.from == 200 && p.frames == 400 && !p.ends);
    // 秒からのループ点: 中に収める。壊れた組 = 全体。
    CHECK((LoopFrames(1.0, 2.0, 100, 1000).start == 100 && LoopFrames(1.0, 2.0, 100, 1000).end == 200));
    CHECK(LoopFrames(0, 0, 100, 1000).end == 1000);
    CHECK((LoopFrames(5.0, 2.0, 100, 1000).start == 0 && LoopFrames(5.0, 2.0, 100, 1000).end == 1000));
    CHECK(LoopFrames(1.0, 99.0, 100, 1000).end == 1000);
}

RM_TEST(Audio_Loudness) {
    CHECK(Gain(1, 1, 1, 1) == 1.f && Near(Gain(0.5f, 0.5f, 2, 1), 0.5f) && Gain(1, 1, 1, -3) == 0.f && Gain(9, 1, 1, 1) == 4.f);
    CHECK(Gain(std::nanf(""), 1, 1, 1) == 0.f);
    // 等パワーのクロスフェード: in² + out² = 1 がずっと保たれる。
    for (double t = 0; t <= 2.0; t += 0.25) {
        const float in = FadeGain(t, 2.0, true), out = FadeGain(t, 2.0, false);
        CHECK(Near(in * in + out * out, 1.f));
    }
    CHECK(FadeGain(0, 2, true) == 0.f && Near(FadeGain(2, 2, true), 1.f) && FadeGain(0, 0, false) == 0.f);
    // 距離: min までは全開、遠いほど小さく、max からは無音、遠くのほうが大きくなることはない。
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
    AudioEngine engine(true);  // 無音: デバイスなし、鳴ったかのように時間が進む
    CHECK(!engine.HasDevice());
    std::vector<float> wave(800, 0.1f);  // 8 kHz で 0.1 秒
    SoundSource s;
    s.bytes = std::make_shared<const std::string>(WriteWav16(wave, {8000, 1}));
    s.key = "beep";
    s.name = "beep.wav";
    // 長さのあとで終わる。
    const auto a = engine.Play(s, {});
    CHECK(engine.IsPlaying(a) && engine.Voices() == 1);
    CHECK(Within([&] {
        engine.Update(0.02);
        return !engine.IsPlaying(a);
    }));
    CHECK(engine.Voices() == 0);
    // ループ: 続く。ポーズ中は時間が数えられない。停止するとすぐ消える。
    PlayParams loop;
    loop.loop = true;
    const auto b = engine.Play(s, loop);
    const auto c = engine.Play(s, {});
    engine.PauseAll(true);
    for (int i = 0; i < 20; ++i) {
        engine.Update(0.05);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(engine.IsPlaying(b) && engine.IsPlaying(c));  // (ポーズ中: 短いほうはまだ終わっていない)
    engine.PauseAll(false);
    engine.StopAll();
    CHECK(engine.Voices() == 0 && !engine.IsPlaying(b) && !engine.IsPlaying(c));
    // フェードアウト: その間はまだあり、終わると消える。
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
    // 読めない音: 1 回だけ知らせ、何も鳴らない。
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

RM_TEST(Audio_Describer_TellsWhenTheLineIsKnown) {
    // アセットパネルの行: 説明役のスレッドで読む。`known` はそれが出たときに呼ばれる
    // (パネルはそこでウィンドウを描き直す - 以前は、壊れたファイルがマウスを動かすまで
    // 「長さを調べています…」のままだった)。
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / (L"rm_describe_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path broken = dir / L"broken.wav", good = dir / L"good.wav";
    std::ofstream(broken, std::ios::binary) << "not a sound";
    {
        const std::string wav = WriteWav16(std::vector<float>(4000, 0.1f), {8000, 1});  // 0.5 s
        std::ofstream(good, std::ios::binary).write(wav.data(), static_cast<std::streamsize>(wav.size()));
    }
    const std::wstring reading = editor::Tr(L"長さを調べています…");
    std::wstring cannot = editor::Tr(L"鳴らせません: {}");
    cannot = cannot.substr(0, cannot.find(L"{}"));
    std::atomic<int> told{0};
    const std::function<void()> known = [&told] { ++told; };
    auto waitFor = [&](int n) { return Within([&] { return told.load() >= n; }); };
    {
        Describer d;
        CHECK(d.Line(broken, known) == reading);
        CHECK(d.Line(broken, known) == reading || told.load() == 1);  // (毎フレーム頼まれる: 知らせるのは 1 回)
        CHECK(waitFor(1));
        CHECK(d.Line(broken, known).starts_with(cannot));
        CHECK(d.Line(good, known) == reading);
        CHECK(waitFor(2));
        const std::wstring line = d.Line(good, known);
        CHECK(line.find(L"0:00.5") != std::wstring::npos && line.find(L"8000 Hz") != std::wstring::npos);
        // 分かった: もう知らせない。ディスクで変わった: 読み直し、また知らせる。
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(told.load() == 2);
        fs::last_write_time(good, fs::last_write_time(good, ec) + std::chrono::seconds(2), ec);
        CHECK(d.Line(good, known) == reading);
        CHECK(waitFor(3));
        CHECK(told.load() == 3);
    }
    fs::remove_all(dir, ec);
}
