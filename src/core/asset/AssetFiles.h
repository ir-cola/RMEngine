#pragma once
// Reading an Assets/ folder and writing its .meta files (the I/O half of AssetIndex.h).
// Standard library only: the game reads the same files to find an asset by its id (ReadIndex).

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
    bool stopped = false;  // asked to stop part way: what it has is not the whole folder - not to be used
    // The folder could not be listed to its end (a folder of OneDrive's that is only online while
    // OneDrive is not running ...): not the whole folder either - not to be used (stopped is set too).
    std::string problem;
};
// What the last look read from each .meta, with its size and time: a .meta that is the same size
// and has the same time is not read again (looking at a big folder again reads only what changed).
struct MetaCache {
    struct Seen {
        uintmax_t size = 0;
        std::filesystem::file_time_type time{};
        MetaInfo info;
        int64_t created = 0;
    };
    std::unordered_map<std::wstring, Seen> byPath;
    size_t reads = 0;  // .meta files read by the last scan (for the tests)
};
// Every file and folder under `root` (not the ignored ones) with the id of its .meta, and the .meta
// files whose file is gone. Paths in display order. `cache` (optional): kept between looks. `stop`:
// looked at every few entries - asked, it ends early with Scan::stopped.
Scan ScanFolder(const std::filesystem::path& root, MetaCache* cache = nullptr, std::stop_token stop = {});

// One look at the folder: ScanFolder + Reconcile, WITHOUT writing anything - safe on a thread of its
// own (the editor's Worker). The plan's .meta writes are made afterwards by ApplyPlan, on the thread
// that owns the folder's ids. *stopped: it was asked to stop, or the folder could not be listed whole
// (*problem says why) - the plan is empty, not to be used: the ids stay as they were.
Plan Look(const std::filesystem::path& root, std::span<const Entry> previous, MetaCache* cache, std::stop_token stop, bool* stopped,
          std::string* problem = nullptr);
// Writes a .meta: next to it first, on the disk (flushed), then moved in place - neither a crash of the
// program nor of Windows (a power cut) leaves half a .meta, or an empty one with its id lost.
bool WriteMetaFile(const std::filesystem::path& p, const std::string& text);
// A path as Windows compares it (letters' case does not count): "Tree.png" and "tree.png" are one file.
std::wstring FoldCase(std::wstring_view path);

// Which look's result may be used. Asking for a look - or changing the folder from the editor (a
// rename, a move ...) - takes a new number; a result is used only while its number is the newest. A
// look that was overtaken (the folder changed again while it ran) is thrown away, never applied.
class LookNumber {
public:
    uint64_t Next() { return ++now_; }
    bool Current(uint64_t n) const { return n == now_; }
    uint64_t Now() const { return now_; }

private:
    uint64_t now_ = 0;
};

// Gives one item its id at once (a file the editor has just written - a prefab saved): the id of the
// .meta next to it when it has a readable one, else `guid` written into a new / readable .meta (with
// its name). Never writes over an unreadable one. The id it has now ("" when it could not be given).
std::string GiveId(const std::filesystem::path& root, std::string_view path, const std::string& guid);

// Writes / moves the .meta files of a Reconcile plan. Problems (Japanese, UTF-8) go to `errors`.
void ApplyPlan(const std::filesystem::path& root, const Plan& plan, std::vector<std::string>& errors);

// Scan + Reconcile + ApplyPlan: the ids of the folder now. `previous`: the last result (empty at first).
// `notes` gets what moved and every problem.
// True in *wrote when it wrote or moved a .meta (the folder watch will tell of it).
std::vector<Entry> Update(const std::filesystem::path& root, std::span<const Entry> previous, std::vector<std::string>& notes,
                          MetaCache* cache = nullptr, bool* wrote = nullptr);

// The ids as the .meta files say, without writing anything (the game: an id -> its file).
std::vector<Entry> ReadIndex(const std::filesystem::path& root);

// Records the file's current name in its .meta (after a rename / move) - only into a .meta that can
// be read whole. False when it was not written.
bool RecordName(const std::filesystem::path& file);

// A new random id (from any thread).
std::string NewGuid();

// Where an item is on disk.
std::filesystem::path FullPath(const std::filesystem::path& root, std::string_view path);
std::filesystem::path MetaPath(const std::filesystem::path& root, std::string_view path);

}  // namespace rm::asset
