#pragma once
// The assets of the open project (its Assets/ folder) for the editor: their ids (core/asset), kept up
// to date with the disk, and the file operations of the アセット panel. One per open project - every
// software's アセット panel shows the same folder.
//
// The ids follow the files: an operation here moves / renames the .meta with its file, and a change
// made outside (Explorer) is seen the next time the editor draws (platform::FolderWatch) and
// reconciled (asset::Reconcile). File operations are not project data: they happen on the disk at
// once, are not undone with Ctrl+Z and do not make the project unsaved (deleting goes to the
// Recycle Bin - that is the way back).

#include "core/asset/AssetIndex.h"
#include "platform/Shell.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rm::editor {

class AssetLibrary {
public:
    explicit AssetLibrary(std::filesystem::path root);

    const std::filesystem::path& Root() const { return root_; }
    const asset::Index& Index() const { return index_; }
    // Indices into Index().Entries() in the order of the folder tree.
    std::span<const size_t> Order() const { return order_; }
    // Changes every time the ids or paths may have changed (a refresh).
    uint64_t Version() const { return version_; }

    // Looks at the folder again (creating it when it is missing).
    void Refresh();
    // Refresh when something under the folder was added, removed or renamed since the last look.
    // True when it refreshed.
    bool Poll();

    std::filesystem::path FullPath(const asset::Entry& e) const;
    std::wstring NameOf(const asset::Entry& e) const;      // the file name
    std::wstring FolderOf(const asset::Entry& e) const;    // "Assets/Models" (where it is)

    // File operations. Each returns what went wrong, in the language in use (empty = done); the
    // console gets it in Japanese. The ids stay with the files.
    std::wstring Rename(const std::string& guid, const std::string& newNameUtf8);
    // Into the folder `folderGuid` ("" = the top of Assets/).
    std::wstring Move(std::span<const std::string> guids, const std::string& folderGuid);
    // A new folder "新しいフォルダ" (2, 3 ... when taken) in `whereGuid` (a folder, or the folder of a
    // file; "" = the top). *made = its id.
    std::wstring NewFolder(const std::string& whereGuid, std::string* made);
    // To the Recycle Bin, with their .meta files.
    std::wstring Delete(std::span<const std::string> guids);

private:
    std::filesystem::path root_;
    asset::Index index_;
    std::vector<size_t> order_;
    platform::FolderWatch watch_;
    bool watching_ = false;
    uint64_t version_ = 0;
};

}  // namespace rm::editor
