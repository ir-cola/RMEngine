#include "platform/Crypto.h"

#include "platform/Win.h"

#include <bcrypt.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace rm::platform {

namespace {

// The secret the .rmpak keys are made from (with each export's number). ★ It is in every game's exe.
constexpr uint8_t kSecret[32] = {0x52, 0x4d, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x9e, 0x37, 0x79, 0xb9, 0x7f, 0x4a, 0x7c, 0x15,
                                 0xf3, 0x9c, 0xc0, 0x60, 0x5c, 0xed, 0xc8, 0x34, 0x10, 0x82, 0x27, 0x6b, 0xf3, 0xa2, 0x72, 0x37};

// AES in ECB on blocks of counters = CTR (BCrypt has no CTR mode of its own).
struct Aes {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    ~Aes() {
        if (key) BCryptDestroyKey(key);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    bool Open(const AesKey& k) {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0))) return false;
        if (!BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                                              sizeof(BCRYPT_CHAIN_MODE_ECB), 0))) {
            return false;
        }
        return BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, const_cast<PUCHAR>(k.data()), static_cast<ULONG>(k.size()), 0));
    }
};

void Increment(std::array<uint8_t, 16>& block, uint64_t by) {
    for (int i = 15; i >= 0 && by != 0; --i) {
        const uint64_t sum = block[static_cast<size_t>(i)] + (by & 0xff);
        block[static_cast<size_t>(i)] = static_cast<uint8_t>(sum);
        by = (by >> 8) + (sum >> 8);
    }
}

}  // namespace

bool AesCtr(const AesKey& key, const std::array<uint8_t, 16>& iv, uint8_t* data, size_t size) {
    if (size == 0) return true;
    Aes aes;
    if (!aes.Open(key)) return false;
    // The keystream a chunk at a time: the counters, encrypted in one call.
    constexpr size_t kChunk = 64 * 1024;
    std::vector<uint8_t> counters(kChunk), stream(kChunk);
    std::array<uint8_t, 16> counter = iv;
    for (size_t at = 0; at < size; at += kChunk) {
        const size_t n = std::min(kChunk, size - at);
        const size_t blocks = (n + 15) / 16;
        for (size_t b = 0; b < blocks; ++b) {
            std::copy(counter.begin(), counter.end(), counters.begin() + static_cast<std::ptrdiff_t>(b * 16));
            Increment(counter, 1);
        }
        ULONG out = 0;
        if (!BCRYPT_SUCCESS(BCryptEncrypt(aes.key, counters.data(), static_cast<ULONG>(blocks * 16), nullptr, nullptr, 0, stream.data(),
                                          static_cast<ULONG>(blocks * 16), &out, 0))) {
            return false;
        }
        for (size_t i = 0; i < n; ++i) data[at + i] ^= stream[i];
    }
    return true;
}

std::array<uint8_t, 32> Sha256(std::string_view bytes) {
    std::array<uint8_t, 32> digest{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return digest;
}

std::function<bool(char*, size_t, uint64_t)> PakCipher(uint64_t build) {
    std::string seed(reinterpret_cast<const char*>(kSecret), sizeof(kSecret));
    for (int i = 0; i < 8; ++i) seed += static_cast<char>(build >> (8 * i));
    const AesKey key = Sha256(seed);
    return [key](char* data, size_t size, uint64_t stream) {
        std::array<uint8_t, 16> iv{};
        for (int i = 0; i < 8; ++i) iv[static_cast<size_t>(7 - i)] = static_cast<uint8_t>(stream >> (8 * i));
        return AesCtr(key, iv, reinterpret_cast<uint8_t*>(data), size);
    };
}

}  // namespace rm::platform
