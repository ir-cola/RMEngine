#pragma once
// Windows 自身の暗号機能 (CNG: BCrypt - 外のコードは使わない) による AES。書き出したゲームの
// .rmpak に使う (core/pak は暗号を関数として受け取り、素の C++ のまま)。
//
// カウンターモード (CTR) の AES-256: バイトを AES(鍵, カウンター) と XOR し、カウンターは 16 バイト
// ごとに 1 ずつ増える。同じ呼び出しで暗号化も復号もでき、どの部分だけでも読め、長さは変わらない。
// ★ 鍵はゲームの exe に入っている (自分のアセットを読むため): ファイルをそのままでは開けなくする
// ためのもので、exe を分解する人は防げない (TECHNOLOGIES.md)。

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace rm::platform {

using AesKey = std::array<uint8_t, 32>;

// `data` を、カウンターブロック `iv` (ビッグエンディアン。16 バイト全部を数え上げる) から始まる
// `key` の AES-256-CTR キーストリームと XOR する。Windows の AES が使えなかったときは false。
bool AesCtr(const AesKey& key, const std::array<uint8_t, 16>& iv, uint8_t* data, size_t size);

// バイト列の SHA-256 (BCrypt)。
std::array<uint8_t, 32> Sha256(std::string_view bytes);

// 1 回の書き出し (`build`) の .rmpak の暗号: 鍵は、エンジン固定の秘密とビルド番号から作る (同じ
// ゲームをもう一度書き出せば同じファイルになる)。pak の各ファイルには、それぞれ別のカウンターの
// 範囲を割り当てる (`stream` をカウンターの上半分に入れる)。ストリームのデータを、先頭から渡して呼ぶ。
std::function<bool(char* data, size_t size, uint64_t stream)> PakCipher(uint64_t build);

}  // namespace rm::platform
