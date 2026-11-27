#pragma once
// ナビゲーション - 焼いたナビメッシュのファイル .rmnav (設計 docs/NAVIGATION.md 4-4)。.rmmesh と同じキー付きのチャンク:
//
//   "RMNAV\0\0\0"  format u32                     その後にチャンク。各チャンクは tag (4) + size u64 + バイト列
//   HEAD  JSON (UTF-8): 形式の版・焼いたエンジンの版・範囲・区画の大きさ・体の形の写し・形の指紋 ... (下の NavFileHead)。
//         このバージョンが知らないキーは `extra` に残し、書くときに戻す
//   TILE  区画 1 つ: x i32、z i32、続いて Detour の区画のデータそのまま (区画ごとに 1 つのチャンク。z、x の順)
//   TCLY  動く障害物の層 1 つ (DetourTileCache): x i32、z i32、層 i32、続いて層のデータ (圧縮しない)
//   LINK  つなぎの線: 数 u32、続いて線ごとに a (float 3)、b (float 3)、半径 float、両方向 u8、エリア u8、持ち主 u32
//
// このバージョンが知らないタグのチャンクは飛ばす。同じ中身なら同じバイトになる (時刻を入れない)。
// 純粋 (ファイルは読み書きしない: バイト列だけ)。tests/TestNavigation.cpp でテストする。

#include "base/Json.h"
#include "rm_engine/navigation/runtime/NavTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rm::navigation {

inline constexpr uint32_t kNavFileFormat = 1;
inline constexpr char kNavFileKind[] = "rm_engine.navmesh";

// HEAD のうち、ナビゲーションが知るもの以外 (エディターが決める: 焼いた型の番号と名前・指紋)。
struct NavFileHead {
    std::string engine;        // 焼いたエンジンの版 ("0.0.35")
    int agentType = 0;         // 焼いた型の番号 (ゲーム制作 × ナビゲーションの NavAgentType::id)
    std::string agentName;     // その名前 (見せるだけ)
    std::string fingerprint;   // 焼いたときの形の指紋 (16 進 16 桁)。今のものと違えば「古い」
    json::Value extra = json::Value::MakeObject();  // 知らないキー (書き戻す)
};

std::string WriteNavFile(const NavData& data, const NavFileHead& head);
// 中身が .rmnav でない・新しい形式・壊れているときは nullopt と *error (日本語)。
std::optional<NavData> ReadNavFile(std::string_view bytes, NavFileHead* head, std::string* error);

// 指紋の 16 進の文字 (16 桁) と、その逆 (読めなければ 0)。
std::string FingerprintText(uint64_t value);

}  // namespace rm::navigation
