#pragma once
// Reading an Assets/ folder and writing its .meta files (the I/O half of AssetIndex.h).
// Standard library only: the game reads the same files to find an asset by its id (ReadIndex).

#include "core/asset/AssetIndex.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rm::asset {

struct Scan {
    std::vector<Found> found;
    std::vector<Orphan> orphans;
};
// Every file and folder under `root` (not the ignored ones) with the id of its .meta, and the .meta
// files whose file is gone. Paths in display order.
Scan ScanFolder(const std::filesystem::path& root);

// Writes / moves the .meta files of a Reconcile plan. Problems (Japanese, UTF-8) go to `errors`.
void ApplyPlan(const std::filesystem::path& root, const Plan& plan, std::vector<std::string>& errors);

// Scan + Reconcile + ApplyPlan: the ids of the folder now. `previous`: the last result (empty at first).
// `notes` gets what moved and every problem.
std::vector<Entry> Update(const std::filesystem::path& root, std::span<const Entry> previous, std::vector<std::string>& notes);

// The ids as the .meta files say, without writing anything (the game: an id -> its file).
std::vector<Entry> ReadIndex(const std::filesystem::path& root);

// A new random id.
std::string NewGuid();

// Where an item is on disk.
std::filesystem::path FullPath(const std::filesystem::path& root, std::string_view path);
std::filesystem::path MetaPath(const std::filesystem::path& root, std::string_view path);

}  // namespace rm::asset
