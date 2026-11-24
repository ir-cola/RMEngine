#pragma once
// コンソールのパネルの、見せる行と数 (純粋 (I/O も描画もしない)。tests/TestConsole.cpp でテストする)。
//
// ログ (base/Log) の残っている項目 (Snapshot: 新しいもの 2000 件まで) から:
//   - 「クリア」より後の項目だけを使う (ログ自身は消さない: ほかのウィンドウ・ログのファイル・
//     デバッグの窓の数はそのまま。クリアはコンソールの見え方だけ)
//   - 情報 / 警告 / エラーの数 (クリアより後の、そのレベルの項目すべて。絞り込みと検索に関係なく)
//   - 見せる行 (レベルの表示が入っていて、検索の文字を含むもの)
// 検索は本文だけを見て、英字の大文字と小文字を区別しない (日本語などはそのまま比べる)。空なら全部。
//
// ツールバーの幅の割り振り (FitBar) もここ: 狭いときは検索の欄が先に縮み、それでも入らなければ
// ラベルを外す (チェックと数だけ)。

#include "base/Log.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::console {

struct Filter {
    bool info = true, warn = true, error = true;  // そのレベルの行を見せる
    std::string search;                            // UTF-8。空 = 絞らない
    uint64_t clearedThrough = 0;                   // この seq まで (含む) はクリアした
};

struct LevelCounts {
    size_t info = 0, warn = 0, error = 0;
    size_t Of(log::Level level) const;
};

struct Result {
    std::vector<size_t> visible;  // 見せる項目の、entries の中の番号 (古い順)
    LevelCounts counts;
};

bool Shows(const Filter& f, log::Level level);
// `needle` が `text` の中にあるか (英字の大文字と小文字を区別しない)。空の needle は常に true。
bool ContainsNoCase(std::string_view text, std::string_view needle);
Result Apply(std::span<const log::Entry> entries, const Filter& f);

// ツールバーの割り振り。`full` / `compact`: 検索より前に並ぶもの全部の幅 (ラベルあり / なし)。
// `searchMin` / `searchMax`: 検索の欄の幅の下限と上限。
struct BarFit {
    bool compact = false;  // ラベルを外す
    float searchW = 0;     // 検索の欄の幅
};
BarFit FitBar(float barW, float full, float compact, float searchMin, float searchMax);

}  // namespace rm::editor::console
