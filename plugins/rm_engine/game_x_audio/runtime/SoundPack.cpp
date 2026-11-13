#include "rm_engine/game_x_audio/runtime/SoundPack.h"

#include <cstdint>

namespace rm::game_x_audio {

std::string PackSound(std::string_view name, const json::Value& import, std::string_view file) {
    json::Value head = json::Value::MakeObject();
    head.Set("name", std::string(name));
    head.Set("import", import.IsObject() ? import : json::Value::MakeObject());
    const std::string text = json::Write(head);
    std::string out;
    out.reserve(kSoundPackMagic.size() + 4 + text.size() + file.size());
    out += kSoundPackMagic;
    const auto n = static_cast<uint32_t>(text.size());
    for (int i = 0; i < 4; ++i) out += static_cast<char>((n >> (8 * i)) & 0xFF);
    out += text;
    out += file;
    return out;
}

std::optional<UnpackedSound> UnpackSound(std::string_view packed) {
    if (!packed.starts_with(kSoundPackMagic) || packed.size() < kSoundPackMagic.size() + 4) return std::nullopt;
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(static_cast<unsigned char>(packed[kSoundPackMagic.size() + static_cast<size_t>(i)])) << (8 * i);
    const size_t at = kSoundPackMagic.size() + 4;
    if (n > kMaxSoundPackHeader || n > packed.size() - at) return std::nullopt;
    auto head = json::Parse(packed.substr(at, n));
    if (!head || !head->IsObject()) return std::nullopt;
    UnpackedSound out;
    out.name = head->Get("name").AsString();
    out.import = head->Get("import").IsObject() ? head->Get("import") : json::Value::MakeObject();
    out.file = packed.substr(at + n);
    return out;
}

}  // namespace rm::game_x_audio
