#pragma once
// 音 - 音のファイルをサンプルにする。1 回に 1 断片ずつ (長い BGM は丸ごとデコードしない)。
//
//   WAV                このエンジン (Sound.h: PCM 8 / 16 / 24 / 32、float 32 / 64)
//   Ogg Vorbis         stb_vorbis (パブリックドメイン / MIT: runtime/third_party) - Media Foundation は
//                      どの PC でも OGG を読めるわけではない
//   MP3 / AAC / FLAC   Media Foundation (Windows)。初めて要るときに読み込む (LoadLibrary): 無い PC
//                      (Windows の N エディション) でもゲームは動く - その音は無音になり、
//                      理由がコンソールと game.log に 1 回出る。
//
// どれを使うかは、ファイルの最初のバイトで決める (名前ではない)。デコーダーは渡されたバイト列から
// 読む (共有: 同じ音を何回か鳴らしても、バイト列は共有する)。どのスレッドでもよいが、1 つのデコーダーを
// 同時に使うのは 1 つのスレッドだけ。Media Foundation は、CoInitializeEx を呼んだスレッド (エンジンの)
// で使う。

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
    uint64_t frames = 0;   // 長さ (0: 不明 - Read が 0 を返すまで読む)
    const char* codec = "";  // "WAV" / "Ogg Vorbis" / "Media Foundation"
    // `frames` フレームまでを `out` (frames * channels 個の float) に読む。終わりでは 0。
    virtual size_t Read(float* out, size_t frames) = 0;
    // `frame` フレームへ移る (ループで戻る)。できなければ false。
    virtual bool Seek(uint64_t frame) = 0;
};

// `bytes` のデコーダー。読めないときは null と *error (日本語で、どうすればよいかも書く)。
std::unique_ptr<Decoder> OpenDecoder(Bytes bytes, const SoundBudget& budget, std::string* error);

// 音全体を float のサンプルで (短い効果音: 1 回デコードして、何度も鳴らす)。budget.maxWholeSeconds
// より長いものは断る (ストリームにするもの)。
struct Pcm {
    Format format;
    std::vector<float> samples;  // インターリーブ
    uint64_t Frames() const { return format.channels ? samples.size() / format.channels : 0; }
};
std::optional<Pcm> DecodeWhole(Decoder& decoder, const SoundBudget& budget, std::string* error);

// 音が何か (アセット パネル): 形式、長さ、読むもの。nullopt: 読めない。
struct SoundInfo {
    Format format;
    double seconds = 0;
    std::string codec;
};
std::optional<SoundInfo> Describe(Bytes bytes, const SoundBudget& budget, std::string* error);

// この PC に Media Foundation がある (読み込んで開始済み。調べるのは 1 回)。
bool MediaFoundationAvailable();

}  // namespace rm::audio
