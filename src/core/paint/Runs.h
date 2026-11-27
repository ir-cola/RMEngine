#pragma once
// マス目に塗った物を文字にする: ランレングス (同じ値の続きを「値*数」と書き、カンマで区切る)。
//   {0, 0, 0, 1, 1, 2, 0}  <->  "0*3,1*2,2,0"
// 自己記述の JSON の中の 1 つの文字にして保存する (GitHub で差分が読める。地面のように同じ値が続くと短い)。
// 気候の地図 (rm_engine/climate) とタイルマップ (rm_engine/tilemap) が使う。どのソフトの言葉も知らない。
// 純粋 - tests/TestClimate.cpp と tests/TestTilemap.cpp でテストする。

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::paint {

// 値の並び -> "1*12,0*3,2" (続く同じ値をまとめる)。空: 空の文字。
std::string EncodeRuns(std::span<const int> values);
std::string EncodeRuns(std::span<const uint32_t> values);
// その逆。`count` 個にそろえる (足りない分は 0、多い分は捨てる)。読めない値 (と負の値) は 0、
// 読めない数は 0 個。
std::vector<int> DecodeRuns(std::string_view text, size_t count);
std::vector<uint32_t> DecodeRunsU32(std::string_view text, size_t count);

}  // namespace rm::paint
