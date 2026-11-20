#pragma once
// 書き出したゲームが持つ形の音 (Game.rmpak の .rmsound、"Baked/<id>.rmsound"): ファイルを
// そのまま (WAV / OGG / MP3 ... - デコードも作り直しもしない)、その前に小さなヘッダーを付ける。
// ヘッダーにはエディターが .meta から読んだもの (取り込みの設定) と名前が入る。ゲームは .meta を読まない。
//
//   "RMSOUND1"  uint32 (リトルエンディアン) n  n バイトの JSON {"name": "bgm.ogg", "import": {...}}  ファイル
//
// 純粋 (ファイルは扱わない): tests/TestAudio.cpp でテストする。

#include "base/Json.h"

#include <optional>
#include <string>
#include <string_view>

namespace rm::game_x_audio {

inline constexpr std::string_view kSoundPackMagic = "RMSOUND1";
inline constexpr size_t kMaxSoundPackHeader = 64 * 1024;

std::string PackSound(std::string_view name, const json::Value& import, std::string_view file);

struct UnpackedSound {
    std::string name;
    json::Value import;
    std::string_view file;  // 詰めたバイト列の中
};
// nullopt: パックではない (または壊れている: ヘッダーがバイト列より長い、または JSON でない)。
std::optional<UnpackedSound> UnpackSound(std::string_view packed);

}  // namespace rm::game_x_audio
