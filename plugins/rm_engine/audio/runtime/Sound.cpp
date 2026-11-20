#include "rm_engine/audio/runtime/Sound.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <vector>

namespace rm::audio {

namespace {

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

void Fail(std::string* error, std::string text) {
    if (error) *error = std::move(text);
}

}  // namespace

size_t BytesPerSample(SampleKind k) {
    switch (k) {
        case SampleKind::Pcm8: return 1;
        case SampleKind::Pcm16: return 2;
        case SampleKind::Pcm24: return 3;
        case SampleKind::Pcm32: return 4;
        case SampleKind::Float32: return 4;
        case SampleKind::Float64: return 8;
    }
    return 2;
}

bool LooksLikeWav(std::span<const uint8_t> b) { return b.size() >= 12 && std::memcmp(b.data(), "RIFF", 4) == 0 && std::memcmp(b.data() + 8, "WAVE", 4) == 0; }

std::optional<WavInfo> ParseWav(std::span<const uint8_t> b, const SoundBudget& budget, std::string* error) {
    if (!LooksLikeWav(b)) {
        Fail(error, "WAV ではありません (RIFF / WAVE の印がありません)");
        return std::nullopt;
    }
    if (b.size() > budget.maxFileBytes) {
        Fail(error, std::format("ファイルが大きすぎます ({} MB まで)", budget.maxFileBytes >> 20));
        return std::nullopt;
    }
    WavInfo info;
    bool haveFormat = false, haveData = false;
    uint16_t bits = 0, tag = 0, blockAlign = 0;
    size_t at = 12;
    // チャンク: 名前 4 バイト、サイズ 4 バイト、本体 (偶数サイズになるよう詰める)。どのサイズも
    // ファイルの残りと照らして調べる (壊れた、または悪意あるサイズでも、終わりを越えて読むことはない)。
    while (at + 8 <= b.size()) {
        const uint8_t* h = b.data() + at;
        const uint32_t size = U32(h + 4);
        const size_t body = at + 8;
        if (size > b.size() - body) {
            if (std::memcmp(h, "data", 4) == 0 && haveFormat) {
                // ファイルより長い data チャンク (録音が途中で切れた): あるところまで鳴らす。
                info.dataOffset = body;
                info.dataBytes = b.size() - body;
                haveData = true;
                break;
            }
            Fail(error, std::format("「{}」のかたまりがファイルの外まで続いています (壊れています)", std::string(reinterpret_cast<const char*>(h), 4)));
            return std::nullopt;
        }
        if (std::memcmp(h, "fmt ", 4) == 0) {
            if (size < 16) {
                Fail(error, "fmt のかたまりが短すぎます");
                return std::nullopt;
            }
            const uint8_t* f = b.data() + body;
            tag = U16(f);
            info.format.channels = U16(f + 2);
            info.format.sampleRate = U32(f + 4);
            blockAlign = U16(f + 12);
            bits = U16(f + 14);
            if (tag == kFormatExtensible) {
                if (size < 40) {
                    Fail(error, "WAVE_FORMAT_EXTENSIBLE の fmt が短すぎます");
                    return std::nullopt;
                }
                tag = U16(f + 24);  // サブフォーマット GUID の最初の 2 バイト: 1 は PCM / 3 は float
            }
            haveFormat = true;
        } else if (std::memcmp(h, "data", 4) == 0) {
            info.dataOffset = body;
            info.dataBytes = size;
            haveData = true;
        }
        at = body + size + (size & 1);
    }
    if (!haveFormat || !haveData) {
        Fail(error, !haveFormat ? "fmt のかたまりがありません" : "data のかたまりがありません");
        return std::nullopt;
    }
    if (tag == kFormatPcm && bits == 8) {
        info.sample = SampleKind::Pcm8;
    } else if (tag == kFormatPcm && bits == 16) {
        info.sample = SampleKind::Pcm16;
    } else if (tag == kFormatPcm && bits == 24) {
        info.sample = SampleKind::Pcm24;
    } else if (tag == kFormatPcm && bits == 32) {
        info.sample = SampleKind::Pcm32;
    } else if (tag == kFormatFloat && bits == 32) {
        info.sample = SampleKind::Float32;
    } else if (tag == kFormatFloat && bits == 64) {
        info.sample = SampleKind::Float64;
    } else {
        Fail(error, std::format("この WAV の形 (形式 {}・{} ビット) は読めません。PCM 8 / 16 / 24 / 32 ビットか float 32 / 64 ビットにしてください", tag, bits));
        return std::nullopt;
    }
    if (info.format.channels == 0 || info.format.channels > budget.maxChannels) {
        Fail(error, std::format("チャンネルの数 ({}) が使えません (1〜{})", info.format.channels, budget.maxChannels));
        return std::nullopt;
    }
    if (info.format.sampleRate < budget.minSampleRate || info.format.sampleRate > budget.maxSampleRate) {
        Fail(error, std::format("サンプリング周波数 ({} Hz) が使えません", info.format.sampleRate));
        return std::nullopt;
    }
    const size_t frameBytes = BytesPerSample(info.sample) * info.format.channels;
    if (blockAlign != 0 && blockAlign != frameBytes) {
        Fail(error, std::format("1 フレームの大きさ ({}) が形 ({} バイト) と合いません", blockAlign, frameBytes));
        return std::nullopt;
    }
    info.frames = info.dataBytes / frameBytes;
    if (static_cast<double>(info.frames) / info.format.sampleRate > budget.maxSeconds) {
        Fail(error, "長すぎます (壊れた長さ)");
        return std::nullopt;
    }
    return info;
}

size_t WavToFloat(std::span<const uint8_t> b, const WavInfo& info, uint64_t from, size_t frames, float* out) {
    if (from >= info.frames) return 0;
    const size_t n = static_cast<size_t>(std::min<uint64_t>(frames, info.frames - from));
    const size_t bps = BytesPerSample(info.sample);
    const size_t samples = n * info.format.channels;
    const uint8_t* p = b.data() + info.dataOffset + from * bps * info.format.channels;
    for (size_t i = 0; i < samples; ++i, p += bps) {
        float v = 0;
        switch (info.sample) {
            case SampleKind::Pcm8: v = (static_cast<float>(p[0]) - 128.f) / 128.f; break;
            case SampleKind::Pcm16: v = static_cast<float>(static_cast<int16_t>(U16(p))) / 32768.f; break;
            case SampleKind::Pcm24: {
                int32_t s = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (s & 0x800000) s |= ~0xFFFFFF;  // 24 ビットの符号
                v = static_cast<float>(s) / 8388608.f;
                break;
            }
            case SampleKind::Pcm32: v = static_cast<float>(static_cast<double>(static_cast<int32_t>(U32(p))) / 2147483648.0); break;
            case SampleKind::Float32: {
                float f;
                std::memcpy(&f, p, 4);
                v = f;
                break;
            }
            case SampleKind::Float64: {
                double d;
                std::memcpy(&d, p, 8);
                v = static_cast<float>(d);
                break;
            }
        }
        out[i] = std::isfinite(v) ? std::clamp(v, -1.f, 1.f) : 0.f;  // (float のファイルには NaN や大きすぎる値があるかもしれない)
    }
    return n;
}

std::string WriteWav16(std::span<const float> samples, Format format) {
    const uint32_t channels = std::max<uint32_t>(format.channels, 1);
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    std::string out;
    out.reserve(44 + dataBytes);
    auto u16 = [&](uint32_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
    };
    auto u32 = [&](uint32_t v) {
        u16(v & 0xFFFF);
        u16(v >> 16);
    };
    out += "RIFF";
    u32(36 + dataBytes);
    out += "WAVEfmt ";
    u32(16);
    u16(kFormatPcm);
    u16(channels);
    u32(format.sampleRate);
    u32(format.sampleRate * channels * 2);
    u16(channels * 2);
    u16(16);
    out += "data";
    u32(dataBytes);
    for (const float s : samples) {
        const float c = std::clamp(std::isfinite(s) ? s : 0.f, -1.f, 1.f);
        const int v = static_cast<int>(std::lround(c * 32767.f));
        u16(static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(v))));
    }
    return out;
}

LoopPoints LoopFrames(double startSeconds, double endSeconds, uint32_t sampleRate, uint64_t totalFrames) {
    LoopPoints lp;
    if (sampleRate == 0 || totalFrames == 0) return lp;
    auto frame = [&](double s) {
        if (!std::isfinite(s) || s <= 0) return uint64_t{0};
        return std::min<uint64_t>(static_cast<uint64_t>(s * sampleRate), totalFrames);
    };
    lp.start = frame(startSeconds);
    lp.end = frame(endSeconds);
    if (lp.end == 0 || lp.end > totalFrames) lp.end = totalFrames;
    if (lp.start >= lp.end) return {0, totalFrames};  // 壊れた組: 音全体
    return lp;
}

Piece NextPiece(uint64_t pos, uint64_t total, size_t want, bool loop, LoopPoints lp) {
    Piece p;
    if (total == 0 || want == 0) {
        p.ends = true;
        return p;
    }
    const uint64_t end = loop ? (lp.end == 0 || lp.end > total ? total : lp.end) : total;
    const uint64_t start = loop ? std::min(lp.start, end > 0 ? end - 1 : 0) : 0;
    if (pos >= end) pos = loop ? start : end;
    p.from = pos;
    p.frames = static_cast<size_t>(std::min<uint64_t>(want, end - pos));
    p.ends = !loop && pos + p.frames >= end;
    return p;
}

float Gain(float master, float kind, float asset, float asked) {
    auto c = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 4.f) : 0.f; };
    return c(master) * c(kind) * c(asset) * c(asked);
}

float FadeGain(double elapsed, double seconds, bool in) {
    if (seconds <= 0 || !std::isfinite(seconds)) return in ? 1.f : 0.f;
    const double t = std::clamp(elapsed / seconds, 0.0, 1.0);
    constexpr double kHalfPi = 1.5707963267948966;
    // 等パワー: in = sin、out = cos (クロスフェード中、in² + out² = 1)。
    return static_cast<float>(in ? std::sin(t * kHalfPi) : std::cos(t * kHalfPi));
}

float DistanceGain(float d, float minD, float maxD) {
    if (!std::isfinite(d) || d < 0) d = 0;
    minD = std::max(minD, 0.001f);
    maxD = std::max(maxD, minD);
    if (d <= minD) return 1.f;
    if (d >= maxD) return 0.f;
    const float inverse = minD / d;
    // maxDistance までの最後の 4 分の 1 は 0 へ細くする (止まるところに段を作らない)。
    const float taperFrom = minD + (maxD - minD) * 0.75f;
    const float taper = d <= taperFrom ? 1.f : (maxD - d) / (maxD - taperFrom);
    return std::clamp(inverse * taper, 0.f, 1.f);
}

float ClampPitch(float pitch) { return std::isfinite(pitch) ? std::clamp(pitch, kMinPitch, kMaxPitch) : 1.f; }

std::string LengthText(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const int whole = static_cast<int>(seconds);
    const int tenths = static_cast<int>((seconds - whole) * 10);
    return std::format("{}:{:02}.{}", whole / 60, whole % 60, tenths);
}

}  // namespace rm::audio
