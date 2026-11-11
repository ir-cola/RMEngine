#pragma once
// Assets: the files of a project's Assets/ folder, each with an id that stays when the file is moved
// or renamed (an AssetRef holds the id, never the path). The id is kept in a small JSON file next to
// it - "<name>.meta" { "guid": "..." } - which goes wherever the file goes (folders have one too).
// Other keys in a .meta are kept as they are (rule 6).
//
// This header is the PURE part (tested in tests/TestAsset.cpp): kinds, .meta text, and Reconcile -
// which file has which id after files were added, removed, moved or renamed. Reading the folder and
// writing .meta files is AssetFiles.h; the editor's panel and file operations are editor/assets.
//
// Paths are relative to Assets/, UTF-8, with '/' between folders ("Models/Tree.fbx").

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::asset {

// What a file is (from its extension). An AssetRef field may ask for some kinds only
// (RM_PROPERTY(AssetKind="model") or "image,model"); the keys are KindKey's.
enum class Kind : uint8_t { Folder, Image, Model, Audio, Font, Text, Other };
Kind KindOf(std::string_view fileName, bool folder);
std::string_view KindKey(Kind k);  // "folder" "image" "model" "audio" "font" "text" "other"
std::optional<Kind> KindFromKey(std::string_view key);
// `filter`: comma-separated keys; empty = any file (never a folder unless "folder" is listed).
bool KindMatches(Kind k, std::string_view filter);
// True when every key of `filter` is a kind (empty is fine).
bool ValidKindFilter(std::string_view filter);

inline constexpr std::string_view kMetaSuffix = ".meta";
bool IsGuid(std::string_view s);  // 32 lower-case hex digits
std::string GuidFromNumbers(uint64_t hi, uint64_t lo);

// "a/b/c.png" -> "a/b" ("" at the top); FileNameOf -> "c.png"; Extension -> ".png" (lower case, "" when none).
std::string_view ParentOf(std::string_view path);
std::string_view FileNameOf(std::string_view path);
std::string Extension(std::string_view name);
std::string Join(std::string_view folder, std::string_view name);
// Files the asset list leaves out: names starting with '.', desktop.ini, Thumbs.db, .meta files and
// .meta files being written ("X.meta.tmp").
bool Ignored(std::string_view name);

// What a .meta says. Ok: JSON with a valid "guid" (+ "name": the file name it was written for).
// Damaged: not JSON (a comma too many, git's conflict marks), but one id is written plainly in it -
// it is used, and the file is NEVER written over (the rest of it would be lost). Unreadable: there,
// but no id can be read (or it cannot be opened) - never written over either; the item waits.
enum class MetaState : uint8_t { None, Ok, Damaged, Unreadable };
struct MetaInfo {
    MetaState state = MetaState::Unreadable;
    std::string guid;
    std::string name;
};
MetaInfo ReadMeta(std::string_view text);
// The id in a .meta file's text; nullopt unless it is MetaState::Ok.
std::optional<std::string> GuidInMeta(std::string_view text);
// The text of a .meta with `guid` (and `name`, the file's name, when given), keeping every other
// key of `old` (empty: a new file). Only for an `old` that is empty or MetaState::Ok.
std::string MetaText(std::string_view old, const std::string& guid, std::string_view name = {});

struct Entry {
    std::string path;
    std::string guid;
    bool folder = false;
    bool operator==(const Entry&) const = default;
};

// What a scan of the folder found: every file / folder (not .meta) and the id of its .meta ("" when it
// has none or it cannot be read) ...
struct Found {
    std::string path;
    bool folder = false;
    std::string metaGuid;                // the id its .meta says ("" when none can be read)
    MetaState meta = MetaState::None;    // None: no .meta next to it
    std::string metaProblem;             // Unreadable: why (Japanese, for the console)
    std::string metaName;                // the file name the .meta was written for ("" = not recorded)
    int64_t metaCreated = 0;             // when the .meta file was made (a copy's is newer); 0 = unknown
};
// ... and the .meta files left behind by a file that is no longer there (moved or renamed outside the
// editor, say in Explorer - without its .meta). `path`: the file it was for.
struct Orphan {
    std::string path;
    std::string guid;
};

struct Plan {
    std::vector<Entry> entries;  // every found item with its id, in the order found
    struct Write {
        std::string path;  // write (or rewrite) this item's .meta with guid, keeping its other keys
        std::string guid;
        std::string name;  // the file's name, recorded in it (tells the original from a copy later)
    };
    struct Move {
        std::string from, to;  // the .meta left by `from` becomes the .meta of `to`
    };
    std::vector<Write> writes;
    std::vector<Move> moves;
    std::vector<std::string> notes;  // what happened, for the console (Japanese)
};

// Which item has which id now. `previous`: the ids of the last look (empty at first).
//   * a .meta that is there but cannot be read is never written over and never replaced by a new id:
//     its item keeps its id of the last look, else it is left out (with a note) until it can be read
//   * an item with its .meta keeps that id. A copy with the same id (a file copied with its .meta)
//     gets a new one; the original is the one at the id's old path, else the one whose name is the
//     name in the .meta, else the one whose .meta was made first
//   * an item without one takes the id of what vanished, when it is clearly the same thing:
//       its old path's id (the .meta was deleted) /
//       the only vanished thing with the same name (moved to another folder) /
//       the only vanished thing in the same folder with the same extension, when it is also the only
//       new one there (renamed)
//     a vanished thing is a .meta left behind (Orphan) or, without one, an id of `previous`
//   * anything else gets a new id (newGuid)
// Orphans not taken stay where they are (a file put back later finds its id again). Every write
// records the file's name in the .meta.
Plan Reconcile(std::span<const Entry> previous, std::span<const Found> found, std::span<const Orphan> orphans,
               const std::function<std::string()>& newGuid);

// The order of a folder tree: a folder before what is in it, in a folder its folders first and then
// its files, names ignoring ASCII case. Indices into `entries`.
std::vector<size_t> DisplayOrder(std::span<const Entry> entries);
// A number for an id (the tree of the アセット panel): the same every time, never 0.
uint64_t NumberOf(std::string_view guid);

// The ids of a folder, looked up both ways.
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
