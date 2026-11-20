#pragma once
// アセット: プロジェクトの Assets/ フォルダーのファイル。それぞれに、ファイルを動かしても名前を
// 変えても変わらない id が付く (AssetRef が持つのは id で、パスではない)。id は隣の小さな JSON
// ファイル - "<名前>.meta" { "guid": "..." } - に保ち、それはファイルと一緒に動く (フォルダーにもある)。
// .meta の他のキーはそのまま保つ (憲法 6)。
//
// このヘッダーは純粋な部分 (tests/TestAsset.cpp でテストする): 種類、.meta のテキスト、そして
// Reconcile - ファイルが足された、消された、動いた、名前が変わったあと、どのファイルがどの id か。
// フォルダーを読んで .meta を書くのは AssetFiles.h。エディターのパネルとファイル操作は editor/assets。
//
// パスは Assets/ からの相対で、UTF-8、フォルダーの区切りは '/' ("Models/Tree.fbx")。

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::asset {

// ファイルが何か (拡張子から)。AssetRef のフィールドは、いくつかの種類だけを求めることができる
// (RM_PROPERTY(AssetKind="model") か "image,model"): キーは KindKey のもの。
enum class Kind : uint8_t { Folder, Image, Model, Audio, Font, Text, Other };
Kind KindOf(std::string_view fileName, bool folder);
std::string_view KindKey(Kind k);  // "folder" "image" "model" "audio" "font" "text" "other"
std::optional<Kind> KindFromKey(std::string_view key);
// `filter`: カンマ区切りのキー。空 = どのファイルでも ("folder" を並べない限りフォルダーは入らない)。
// "." で始まるキーは拡張子 (".rmprefab": ソフトが作るファイルで、専用の種類がない):
// `fileName` の拡張子と合う。
bool KindMatches(Kind k, std::string_view filter, std::string_view fileName = {});
// `filter` のキーがすべて種類か拡張子なら true (空でもよい)。
bool ValidKindFilter(std::string_view filter);

inline constexpr std::string_view kMetaSuffix = ".meta";
bool IsGuid(std::string_view s);  // 小文字の 16 進 32 桁
std::string GuidFromNumbers(uint64_t hi, uint64_t lo);

// "a/b/c.png" -> "a/b" (一番上は "")。FileNameOf -> "c.png"。Extension -> ".png" (小文字、なければ "")。
std::string_view ParentOf(std::string_view path);
std::string_view FileNameOf(std::string_view path);
std::string Extension(std::string_view name);
std::string Join(std::string_view folder, std::string_view name);
// アセット一覧から外すファイル: '.' で始まる名前、desktop.ini、Thumbs.db、.meta ファイル、
// 書いている途中の .meta ファイル ("X.meta.tmp")、そして他のプログラムが保存中に作るファイル:
// '~' で始まる・終わる名前と ".tmp" ファイル。
bool Ignored(std::string_view name);

// .meta が言うこと。Ok: 正しい "guid" を持つ JSON (+ "name": 書かれたときのファイル名)。
// Damaged: JSON ではない (カンマが 1 つ多い、git の衝突マーク) が、id が 1 つ素直に書かれている -
// それを使い、ファイルには決して上書きしない (残りが失われる)。Unreadable: あるが
// id が読めない (または開けない) - これも決して上書きしない。項目は待つ。
enum class MetaState : uint8_t { None, Ok, Damaged, Unreadable };
struct MetaInfo {
    MetaState state = MetaState::Unreadable;
    std::string guid;
    std::string name;
};
MetaInfo ReadMeta(std::string_view text);
// .meta ファイルのテキストの中の id。MetaState::Ok でなければ nullopt。
std::optional<std::string> GuidInMeta(std::string_view text);
// `guid` (と、渡されたときはファイルの名前 `name`) を持つ .meta のテキスト。`old` の他のキーは
// 全部残す (空: 新しいファイル)。`old` が空か MetaState::Ok のときだけ。
std::string MetaText(std::string_view old, const std::string& guid, std::string_view name = {});

struct Entry {
    std::string path;
    std::string guid;
    bool folder = false;
    bool operator==(const Entry&) const = default;
};

// フォルダーの走査で見つかったもの: 全ファイル / フォルダー (.meta は除く) と、その .meta の id
// (なし、または読めないときは "") ...
struct Found {
    std::string path;
    bool folder = false;
    std::string metaGuid;                // その .meta が言う id (読めないときは "")
    MetaState meta = MetaState::None;    // None: 隣に .meta がない
    std::string metaProblem;             // Unreadable: 理由 (日本語、コンソール用)
    std::string metaName;                // .meta が書かれたときのファイル名 ("" = 記録なし)
    int64_t metaCreated = 0;             // .meta ファイルが作られた時刻 (コピーのものは新しい)。0 = 不明
};
// ... そして、もうないファイルが残した .meta ファイル (エディターの外、たとえば Explorer で、
// .meta を付けずに動かした、または名前を変えた)。`path`: それがあったファイル。
struct Orphan {
    std::string path;
    std::string guid;
};

struct Plan {
    std::vector<Entry> entries;  // 見つかった項目すべてとその id。見つけた順
    struct Write {
        std::string path;  // この項目の .meta を guid で書く (または書き直す)。他のキーは保つ
        std::string guid;
        std::string name;  // その中に記録するファイルの名前 (後で、元とコピーを見分ける)
    };
    struct Move {
        std::string from, to;  // `from` が残した .meta を `to` の .meta にする
    };
    std::vector<Write> writes;
    std::vector<Move> moves;
    std::vector<std::string> notes;  // 何が起きたか、コンソール用 (日本語)
};

// どの項目が今どの id か。`previous`: 前回の調べの id (最初は空)。
//   * あるが読めない .meta には決して上書きせず、新しい id にも替えない:
//     その項目は前回の調べの id を保ち、なければ (メモ付きで) 読めるまで外しておく
//   * .meta のある項目は、その id を保つ。同じ id のコピー (.meta ごとコピーしたファイル) は
//     新しい id をもらう。元になるのは、id の古いパスにあるもの、なければ .meta の名前が
//     名前と同じもの、なければ .meta が最初に作られたもの
//   * .meta のない項目は、消えたものの id を、明らかに同じものだと言えるときにもらう:
//       古いパスの id (.meta が消された) /
//       同じ名前の消えたものが 1 つだけ (別のフォルダーへ動いた) /
//       同じフォルダーに同じ拡張子の消えたものが 1 つだけで、そこの新しいものも 1 つだけのとき
//       (名前を変えた)
//     消えたものとは、残された .meta (Orphan)、それがなければ `previous` の id
//   * それ以外は新しい id をもらう (newGuid)
// もらわれなかった Orphan はそこに残る (後でファイルを戻すと、その id がまた見つかる)。
// 書き込みのたびに、ファイルの名前を .meta に記録する。
Plan Reconcile(std::span<const Entry> previous, std::span<const Found> found, std::span<const Orphan> orphans,
               const std::function<std::string()>& newGuid);

// フォルダーの木の順: フォルダーはその中身より前、フォルダーの中ではフォルダーが先でファイルが後、
// 名前は ASCII の大文字小文字を無視する。`entries` の添字。
std::vector<size_t> DisplayOrder(std::span<const Entry> entries);
// id に付ける番号 (アセットパネルの木): いつも同じで、0 にはならない。
uint64_t NumberOf(std::string_view guid);

// フォルダーの id。どちら向きでも引ける。
class Index {
public:
    void Set(std::vector<Entry> entries);
    std::span<const Entry> Entries() const { return entries_; }
    const Entry* ByGuid(std::string_view guid) const;
    const Entry* ByPath(std::string_view path) const;

private:
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byGuid_, byPath_;
};

}  // namespace rm::asset
