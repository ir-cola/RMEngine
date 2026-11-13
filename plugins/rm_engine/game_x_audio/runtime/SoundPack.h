#pragma once
// A sound as the exported game has it (.rmsound in Game.rmpak, "Baked/<id>.rmsound"): the file as it
// is (WAV / OGG / MP3 ... - not decoded, not made again) after a small header with what the editor
// read from its .meta (the import settings) and its name. The game reads no .meta.
//
//   "RMSOUND1"  uint32 (little endian) n  n bytes of JSON {"name": "bgm.ogg", "import": {...}}  the file
//
// Pure (no files): tested in tests/TestAudio.cpp.

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
    std::string_view file;  // inside the packed bytes
};
// nullopt: not a pack (or a broken one: the header longer than the bytes, or not JSON).
std::optional<UnpackedSound> UnpackSound(std::string_view packed);

}  // namespace rm::game_x_audio
