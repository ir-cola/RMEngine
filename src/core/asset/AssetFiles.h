#pragma once
// Reading an Assets/ folder and writing its .meta files (the I/O half of AssetIndex.h).
// Standard library only: the game reads the same files to find an asset by its id (ReadIndex).

#include "core/asset/AssetIndex.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::asset {

struct Scan {
    std::vector<Found> found;
    std::vector<Orphan> orphans;
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
// files whose file is gone. Paths in display order. `cache` (optional): kept between looks.
Scan ScanFolder(const std::filesystem::path& root, MetaCache* cache = nullptr);

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

// A new random id.
std::string NewGuid();

// Where an item is on disk.
std::filesystem::path FullPath(const std::filesystem::path& root, std::string_view path);
std::filesystem::path MetaPath(const std::filesystem::path& root, std::string_view path);

}  // namespace rm::asset
