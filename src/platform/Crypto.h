#pragma once
// AES with Windows' own cryptography (CNG: BCrypt - no outside code). Used for the exported game's
// .rmpak (core/pak takes the cipher as a function and stays plain C++).
//
// AES-256 in counter mode (CTR): the bytes are XORed with AES(key, counter), the counter going up by
// one for every 16 bytes - the same call encrypts and decrypts, any part can be read alone, and the
// length never changes. ★ The key is in the game's exe (it has to read its own assets): this keeps the
// files from being opened as they are, not from someone who takes the exe apart (TECHNOLOGIES.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace rm::platform {

using AesKey = std::array<uint8_t, 32>;

// XORs `data` with the AES-256-CTR keystream of `key` from the counter block `iv` (big-endian, the
// whole 16 bytes counted up). False when Windows' AES could not be used.
bool AesCtr(const AesKey& key, const std::array<uint8_t, 16>& iv, uint8_t* data, size_t size);

// SHA-256 of bytes (BCrypt).
std::array<uint8_t, 32> Sha256(std::string_view bytes);

// The .rmpak's cipher for one export (`build`): the key is made from the engine's fixed secret and
// the build number (so the same game exported again gives the same file), each file of the pak its
// own counter range (`stream` in the upper half of the counter). Call it with the data of a stream
// from its start.
std::function<bool(char* data, size_t size, uint64_t stream)> PakCipher(uint64_t build);

}  // namespace rm::platform
