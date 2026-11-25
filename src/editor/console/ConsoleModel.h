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
//
// 時刻 (ClockText): 行を書いたときの PC の時計 (地方時。Entry::clockMs) を "HH:MM:SS.mmm" で。日付は出さない。
//
// 行の選択 (Selection): 行の番号ではなく項目の seq で持つ (新しい行が来ても・自動スクロールしても
// 同じ行のまま)。並び (`order`) は今見せている行の seq を表示順 (古い順) に並べたもの:
//   クリック = その行だけ / Ctrl = 足す・外す / Shift = 起点 (anchor) からの範囲 (見せている行のうち) /
//   Ctrl+Shift = その範囲を足す / ドラッグ = 押した行からの範囲 (SelectRange) / Ctrl+A = 見せている行の全部
// ログから落ちた行 (古い 2000 件より前) とクリアした行は Prune が外す。絞り込みや検索で隠した行は
// 選んだまま残る (戻すとまた選ばれている) が、コピーするのは見せている行だけ。
//
// コピー (CopyText): 選んだ行のうち見せているものを表示順に、1 行ずつ
//   "HH:MM:SS.mmm [INFO] 本文" / "[WARN]" / "[ERROR]"   (messagesOnly: 本文だけ)
// 行の間は "\r\n" (Windows のクリップボード。最後の行の後には付けない)。本文の中の改行はそのまま。

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

// "HH:MM:SS.mmm" (00:00:00.000 .. 23:59:59.999。1970 年より前の負の値も、その日の時刻に)。
std::string ClockText(int64_t clockMs);
// コピーの文字のレベル: "INFO" / "WARN" / "ERROR"。
std::string_view LevelTag(log::Level level);
// "HH:MM:SS.mmm [WARN] 本文"
std::string CopyLine(const log::Entry& e);

struct Selection {
    std::vector<uint64_t> seqs;  // 選んだ項目の seq (小さい順 = 表示順。重ならない)
    uint64_t anchor = 0;         // Shift の範囲の起点 (0 = なし)

    bool Contains(uint64_t seq) const;
    bool Empty() const { return seqs.empty(); }
    bool operator==(const Selection&) const = default;
};

// 見せている行の seq (表示順)。
std::vector<uint64_t> SeqsOf(std::span<const log::Entry> entries, std::span<const size_t> visible);
// 行 `seq` (`order` の中にあること) のクリック。
void Click(Selection& sel, std::span<const uint64_t> order, uint64_t seq, bool ctrl, bool shift);
// `order` の中の `from` から `to` まで (どちらが先でもよい) を選ぶ。add: 前の選択に足す / 置き換える。
// 起点は `from` になる。どちらかが `order` に無ければ何もしない。
void SelectRange(Selection& sel, std::span<const uint64_t> order, uint64_t from, uint64_t to, bool add);
void SelectAll(Selection& sel, std::span<const uint64_t> order);
void ClearSelection(Selection& sel);
// ログに残っていない項目 (entries に無い) と、クリアした項目 (seq <= clearedThrough) を外す。
// 選択が変わったら true。
bool Prune(Selection& sel, std::span<const log::Entry> entries, uint64_t clearedThrough);
// 選んだ行のうち見せているもの (`visible` の順) を、コピーする文字に (上の形)。選んだ行が見えていなければ空。
std::string CopyText(std::span<const log::Entry> entries, std::span<const size_t> visible, const Selection& sel,
                     bool messagesOnly = false);
// 見せている行の `count` 行のうち、縦の位置 `y` にある行 (行の並びの上端が `top`、1 行の高さが `lineH`)。
// 上へはみ出したら 0、下へはみ出したら最後の行 (ドラッグで選ぶとき)。`count` が 0 なら 0。
size_t RowAt(float y, float top, float lineH, size_t count);

}  // namespace rm::editor::console
