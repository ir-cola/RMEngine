#pragma once
// Assets/ フォルダーを読み、その .meta ファイルを書く (AssetIndex.h の I/O の半分)。
// 標準ライブラリだけ: ゲームは同じファイルを読んで、id からアセットを見つける (ReadIndex)。

#include "core/asset/AssetIndex.h"

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::asset {

struct Scan {
    std::vector<Found> found;
    std::vector<Orphan> orphans;
    bool stopped = false;  // 途中で止めるよう頼まれた: 持っているのはフォルダー全部ではない - 使わないこと
    // フォルダーを最後まで一覧にできなかった (OneDrive が動いていない間はオンラインにしかない
    // OneDrive のフォルダーなど): これもフォルダー全部ではない - 使わないこと (stopped も立つ)。
    std::string problem;
};
// 前回の調べで各 .meta から読んだもの。大きさと時刻付き: 大きさも時刻も同じ .meta は
// もう一度読まない (大きなフォルダーを調べ直しても、変わった分だけ読む)。
struct MetaCache {
    struct Seen {
        uintmax_t size = 0;
        std::filesystem::file_time_type time{};
        MetaInfo info;
        int64_t created = 0;
    };
    std::unordered_map<std::wstring, Seen> byPath;
    size_t reads = 0;  // 前回の走査で読んだ .meta ファイル (テスト用)
};
// `root` 以下の全ファイルとフォルダー (無視したものは除く) を、その .meta の id と一緒に、
// ファイルがなくなった .meta も。パスは表示順。`cache` (省略可): 調べの間で持ち越す。`stop`:
// 数エントリーごとに見る - 立つと、Scan::stopped を付けて早めに終わる。
Scan ScanFolder(const std::filesystem::path& root, MetaCache* cache = nullptr, std::stop_token stop = {});

// フォルダーを 1 回調べる: ScanFolder + Reconcile。何も書かない - 専用のスレッド (エディターの
// Worker) で安全。計画の .meta の書き込みは、後で ApplyPlan が、フォルダーの id を持つスレッドで行う。
// *stopped: 止めるよう頼まれた、またはフォルダー全部を一覧にできなかった (*problem に理由) -
// 計画は空で、使わないこと: id はそのまま。
Plan Look(const std::filesystem::path& root, std::span<const Entry> previous, MetaCache* cache, std::stop_token stop, bool* stopped,
          std::string* problem = nullptr);
// .meta を書く: まず隣にディスクへ (フラッシュして) 書き、それから元の場所へ移す - プログラムが
// 落ちても、Windows が落ちても (停電)、書きかけの .meta も、id が失われた空の .meta も残らない。
bool WriteMetaFile(const std::filesystem::path& p, const std::string& text);
// Windows が比べるのと同じパス (大文字小文字は区別しない): "Tree.png" と "tree.png" は同じファイル。
std::wstring FoldCase(std::wstring_view path);

// どの調べの結果を使ってよいか。調べを頼む - またはエディターからフォルダーを変える
// (名前の変更、移動など) - たびに新しい番号を取り、結果を使うのはその番号が最新の間だけ。
// 追い越された調べ (動いている間にフォルダーがまた変わった) は捨て、決して適用しない。
class LookNumber {
public:
    uint64_t Next() { return ++now_; }
    bool Current(uint64_t n) const { return n == now_; }
    uint64_t Now() const { return now_; }

private:
    uint64_t now_ = 0;
};

// 1 つの項目にすぐ id を付ける (エディターが今書いたファイル - 保存した Prefab): 隣の .meta が
// 読めればその id、そうでなければ `guid` を新しい / 読める .meta に (名前と一緒に) 書く。
// 読めないものには決して上書きしない。今持っている id (付けられなかったときは "")。
std::string GiveId(const std::filesystem::path& root, std::string_view path, const std::string& guid);

// Reconcile の計画の .meta ファイルを書く / 動かす。問題 (日本語、UTF-8) は `errors` へ。
void ApplyPlan(const std::filesystem::path& root, const Plan& plan, std::vector<std::string>& errors);

// Scan + Reconcile + ApplyPlan: 今のフォルダーの id。`previous`: 前回の結果 (最初は空)。
// `notes` には動いたことと全部の問題が入る。
// .meta を書いた、または動かしたとき、*wrote が true になる (フォルダーの監視がそれを知らせる)。
std::vector<Entry> Update(const std::filesystem::path& root, std::span<const Entry> previous, std::vector<std::string>& notes,
                          MetaCache* cache = nullptr, bool* wrote = nullptr);

// .meta ファイルが言う通りの id。何も書かない (ゲーム: id -> そのファイル)。
std::vector<Entry> ReadIndex(const std::filesystem::path& root);

// ファイルの今の名前をその .meta に記録する (名前の変更 / 移動の後) - 全部読める .meta にだけ。
// 書かなかったときは false。
bool RecordName(const std::filesystem::path& file);

// 新しいランダムな id (どのスレッドからでも)。
std::string NewGuid();

// 項目がディスクのどこにあるか。
std::filesystem::path FullPath(const std::filesystem::path& root, std::string_view path);
std::filesystem::path MetaPath(const std::filesystem::path& root, std::string_view path);

}  // namespace rm::asset
