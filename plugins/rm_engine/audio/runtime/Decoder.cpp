#include "rm_engine/audio/runtime/Decoder.h"

#include "platform/Win.h"
#include "rm_engine/audio/runtime/StbVorbis.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <vector>

#pragma comment(lib, "mfuuid.lib")   // Media Foundation's GUIDs only (a static library: no DLL is needed for them)
#pragma comment(lib, "shlwapi.lib")  // SHCreateMemStream (every Windows has it)

using Microsoft::WRL::ComPtr;

namespace rm::audio {

namespace {

std::span<const uint8_t> Span(const Bytes& b) { return {reinterpret_cast<const uint8_t*>(b->data()), b->size()}; }

bool CheckFormat(const Format& f, const SoundBudget& budget, std::string* error) {
    if (f.channels == 0 || f.channels > budget.maxChannels) {
        if (error) *error = std::format("チャンネルの数 ({}) が使えません (1〜{})", f.channels, budget.maxChannels);
        return false;
    }
    if (f.sampleRate < budget.minSampleRate || f.sampleRate > budget.maxSampleRate) {
        if (error) *error = std::format("サンプリング周波数 ({} Hz) が使えません", f.sampleRate);
        return false;
    }
    return true;
}

// --- WAV -------------------------------------------------------------------------------------------
class WavDecoder final : public Decoder {
public:
    WavDecoder(Bytes bytes, const WavInfo& info) : bytes_(std::move(bytes)), info_(info) {
        format = info.format;
        frames = info.frames;
        codec = "WAV";
    }
    size_t Read(float* out, size_t n) override {
        const size_t got = WavToFloat(Span(bytes_), info_, pos_, n, out);
        pos_ += got;
        return got;
    }
    bool Seek(uint64_t frame) override {
        pos_ = std::min(frame, info_.frames);
        return true;
    }

private:
    Bytes bytes_;
    WavInfo info_;
    uint64_t pos_ = 0;
};

// --- Ogg Vorbis (stb_vorbis) -------------------------------------------------------------------------
class VorbisDecoder final : public Decoder {
public:
    VorbisDecoder(Bytes bytes, stb_vorbis* v) : bytes_(std::move(bytes)), v_(v) {
        const stb_vorbis_info info = stb_vorbis_get_info(v_);
        format = {static_cast<uint32_t>(info.sample_rate), static_cast<uint16_t>(info.channels)};
        frames = stb_vorbis_stream_length_in_samples(v_);
        codec = "Ogg Vorbis";
    }
    ~VorbisDecoder() override { stb_vorbis_close(v_); }
    size_t Read(float* out, size_t n) override {
        const int total = static_cast<int>(std::min<size_t>(n, 1 << 20) * format.channels);
        const int got = stb_vorbis_get_samples_float_interleaved(v_, format.channels, out, total);
        return got > 0 ? static_cast<size_t>(got) : 0;
    }
    bool Seek(uint64_t frame) override { return stb_vorbis_seek(v_, static_cast<unsigned>(std::min<uint64_t>(frame, 0xFFFFFFFFu))) != 0; }

private:
    Bytes bytes_;  // (stb_vorbis reads from them: kept while it lives)
    stb_vorbis* v_;
};

// --- Media Foundation (MP3 / AAC / FLAC ...) - loaded when first needed ------------------------------
struct Mf {
    bool ok = false;
    HRESULT(WINAPI* createByteStreamOnStream)(IStream*, IMFByteStream**) = nullptr;
    HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* createSourceReaderFromByteStream)(IMFByteStream*, IMFAttributes*, IMFSourceReader**) = nullptr;
};

const Mf& MediaFoundation() {
    static Mf mf;
    static std::once_flag once;
    std::call_once(once, [] {
        // From the system's folder only (never a DLL lying next to the game).
        HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE reader = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!plat || !reader) return;  // the N editions of Windows: no Media Foundation
        using Startup = HRESULT(WINAPI*)(ULONG, DWORD);
        const auto startup = reinterpret_cast<Startup>(reinterpret_cast<void*>(GetProcAddress(plat, "MFStartup")));
        mf.createByteStreamOnStream = reinterpret_cast<decltype(mf.createByteStreamOnStream)>(
            reinterpret_cast<void*>(GetProcAddress(plat, "MFCreateMFByteStreamOnStream")));
        mf.createMediaType = reinterpret_cast<decltype(mf.createMediaType)>(reinterpret_cast<void*>(GetProcAddress(plat, "MFCreateMediaType")));
        mf.createSourceReaderFromByteStream = reinterpret_cast<decltype(mf.createSourceReaderFromByteStream)>(
            reinterpret_cast<void*>(GetProcAddress(reader, "MFCreateSourceReaderFromByteStream")));
        if (!startup || !mf.createByteStreamOnStream || !mf.createMediaType || !mf.createSourceReaderFromByteStream) return;
        mf.ok = SUCCEEDED(startup(MF_VERSION, MFSTARTUP_LITE));  // (never shut down: it lives as long as the program)
    });
    return mf;
}

class MfDecoder final : public Decoder {
public:
    bool Open(const Bytes& bytes, const SoundBudget& budget, std::string* error) {
        const Mf& mf = MediaFoundation();
        if (!mf.ok) {
            if (error) {
                *error = "この PC には Media Foundation がありません (Windows の N エディションなど)。MP3 / AAC / FLAC は鳴らせません。"
                         "ゲームの音は WAV か OGG にしてください";
            }
            return false;
        }
        // A copy in a stream of memory (SHCreateMemStream copies the bytes).
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes->data()), static_cast<UINT>(bytes->size())));
        ComPtr<IMFByteStream> byteStream;
        if (!stream || FAILED(mf.createByteStreamOnStream(stream.Get(), &byteStream)) ||
            FAILED(mf.createSourceReaderFromByteStream(byteStream.Get(), nullptr, &reader_))) {
            if (error) *error = "Media Foundation で開けません (音のファイルではないか、この PC で読めない形です。WAV か OGG にしてください)";
            return false;
        }
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
        ComPtr<IMFMediaType> want;
        if (FAILED(mf.createMediaType(&want))) return false;
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);  // Media Foundation converts to 32-bit float
        if (FAILED(reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, want.Get()))) {
            if (error) *error = "Media Foundation で音として読めません";
            return false;
        }
        ComPtr<IMFMediaType> got;
        if (FAILED(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &got))) return false;
        UINT32 channels = 0, rate = 0;
        got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        format = {rate, static_cast<uint16_t>(channels)};
        if (!CheckFormat(format, budget, error)) return false;
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(reader_->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &var)) && var.vt == VT_UI8) {
            frames = var.uhVal.QuadPart * rate / 10'000'000ull;  // (100 ns units)
        }
        PropVariantClear(&var);
        codec = "Media Foundation";
        return true;
    }

    size_t Read(float* out, size_t n) override {
        size_t done = 0;
        const size_t ch = format.channels;
        while (done < n) {
            if (at_ < left_.size()) {
                const size_t take = std::min((left_.size() - at_) / ch, n - done);
                std::memcpy(out + done * ch, left_.data() + at_, take * ch * sizeof(float));
                at_ += take * ch;
                done += take;
                continue;
            }
            if (ended_) break;
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample)) ||
                (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                ended_ = true;
                break;
            }
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
            BYTE* data = nullptr;
            DWORD length = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &length))) continue;
            left_.assign(reinterpret_cast<const float*>(data), reinterpret_cast<const float*>(data) + length / sizeof(float));
            buffer->Unlock();
            at_ = 0;
        }
        return done;
    }

    bool Seek(uint64_t frame) override {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = static_cast<LONGLONG>(format.sampleRate ? frame * 10'000'000ull / format.sampleRate : 0);
        const bool ok = SUCCEEDED(reader_->SetCurrentPosition(GUID_NULL, var));
        left_.clear();
        at_ = 0;
        ended_ = !ok;
        return ok;
    }

private:
    ComPtr<IMFSourceReader> reader_;
    std::vector<float> left_;  // decoded and not yet given
    size_t at_ = 0;
    bool ended_ = false;
};

}  // namespace

std::unique_ptr<Decoder> OpenDecoder(Bytes bytes, const SoundBudget& budget, std::string* error) {
    if (!bytes || bytes->empty()) {
        if (error) *error = "空のファイルです";
        return nullptr;
    }
    if (bytes->size() > budget.maxFileBytes) {
        if (error) *error = std::format("ファイルが大きすぎます ({} MB まで)", budget.maxFileBytes >> 20);
        return nullptr;
    }
    const std::span<const uint8_t> b = Span(bytes);
    if (LooksLikeWav(b)) {
        const auto info = ParseWav(b, budget, error);
        if (!info) return nullptr;
        return std::make_unique<WavDecoder>(std::move(bytes), *info);
    }
    if (b.size() >= 4 && std::memcmp(b.data(), "OggS", 4) == 0) {
        int err = 0;
        stb_vorbis* v = stb_vorbis_open_memory(b.data(), static_cast<int>(std::min<size_t>(b.size(), 0x7FFFFFFF)), &err, nullptr);
        if (!v) {
            if (error) *error = std::format("Ogg Vorbis として読めません (stb_vorbis のエラー {}: 壊れているか、Vorbis でない Ogg (Opus など) です)", err);
            return nullptr;
        }
        auto d = std::make_unique<VorbisDecoder>(std::move(bytes), v);
        if (!CheckFormat(d->format, budget, error)) return nullptr;
        if (d->format.sampleRate && static_cast<double>(d->frames) / d->format.sampleRate > budget.maxSeconds) {
            if (error) *error = "長すぎます (壊れた長さ)";
            return nullptr;
        }
        return d;
    }
    auto mf = std::make_unique<MfDecoder>();
    if (!mf->Open(bytes, budget, error)) return nullptr;
    return mf;
}

std::optional<Pcm> DecodeWhole(Decoder& d, const SoundBudget& budget, std::string* error) {
    const uint64_t most = static_cast<uint64_t>(budget.maxWholeSeconds * d.format.sampleRate);
    if (d.frames > most) {
        if (error) *error = "長いので全部は読みません (ストリーミングで鳴らします)";
        return std::nullopt;
    }
    Pcm pcm;
    pcm.format = d.format;
    pcm.samples.reserve(static_cast<size_t>(d.frames) * d.format.channels);
    constexpr size_t kPiece = 16384;
    std::vector<float> piece(kPiece * d.format.channels);
    for (;;) {
        const size_t got = d.Read(piece.data(), kPiece);
        if (got == 0) break;
        pcm.samples.insert(pcm.samples.end(), piece.begin(), piece.begin() + static_cast<std::ptrdiff_t>(got * d.format.channels));
        if (pcm.Frames() > most) {
            if (error) *error = "長いので全部は読みません (ストリーミングで鳴らします)";
            return std::nullopt;
        }
    }
    return pcm;
}

std::optional<SoundInfo> Describe(Bytes bytes, const SoundBudget& budget, std::string* error) {
    const auto d = OpenDecoder(std::move(bytes), budget, error);
    if (!d) return std::nullopt;
    SoundInfo info;
    info.format = d->format;
    info.codec = d->codec;
    info.seconds = d->format.sampleRate ? static_cast<double>(d->frames) / d->format.sampleRate : 0;
    return info;
}

bool MediaFoundationAvailable() { return MediaFoundation().ok; }

}  // namespace rm::audio
