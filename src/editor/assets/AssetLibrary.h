#pragma once
// The assets of the open project (its Assets/ folder) for the editor: their ids (core/asset), kept up
// to date with the disk, and the file operations of the アセット panel. One per open project - every
// software's アセット panel shows the same folder.
//
// The ids follow the files: an operation here moves / renames the .meta with its file, and a change
// made outside (Explorer) is seen the next time the editor draws (platform::FolderWatch) and
// reconciled (asset::Reconcile).
//
// Looking at the folder runs on a Worker (editor/app/Worker): reading the folder and the .meta files
// and working out the plan (asset::Look) never stop the drawing. The plan comes back to the UI thread,
// which writes its .meta files and puts the new ids in place in one go (ApplyLook). Every look and every
// change made here takes a new number (asset::LookNumber): a result whose number is no longer the
// newest - the folder changed again, or the editor renamed / moved / deleted something meanwhile - is
// thrown away, and a look is asked again. Meanwhile the panel and the インスペクター go on with the
// last result (Looking(): the panel says 調べています). An operation here puts its own change into the
// ids at once (the panel follows without waiting), then asks for a look. File operations are not project data: they happen on the disk at
// once, are not undone with Ctrl+Z and do not make the project unsaved (deleting goes to the
// Recycle Bin - that is the way back).

#include "base/Json.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/AssetIndex.h"
#include "editor/app/Worker.h"
#include "platform/Shell.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace rm::editor {

class AssetLibrary {
public:
    // `onChange`: called (from any thread) when something changes in the folder while no one asks -
    // the owner wakes the UI thread with it (editor::UiWake) so a change made in Explorer shows at once.
    explicit AssetLibrary(std::filesystem::path root, std::function<void()> onChange = {});
    ~AssetLibrary();  // writes the import settings still waiting

    const std::filesystem::path& Root() const { return root_; }
    const asset::Index& Index() const { return index_; }
    // Indices into Index().Entries() in the order of the folder tree.
    std::span<const size_t> Order() const { return order_; }
    // Changes every time the ids or paths may have changed (a refresh).
    uint64_t Version() const { return version_; }

    // Asks for a look at the folder (creating it when it is missing), on the Worker; a look still going
    // is dropped. A .meta that did not change since the last look (same size and time) is not read
    // again. The folder watch's news of the .meta files it writes itself is dropped.
    void Refresh();
    // The first look, here and now (on the UI thread), when the project is opened: the softwares read
    // their data right after and find their prefabs, models ... by id - with no ids yet, every one of
    // them would be "missing". Every later look runs on the Worker.
    void LookNow();
    // A look is going on (or none has finished yet): the ids are the last look's.
    bool Looking() const { return looking_; }
    // A look has been used (the ids are the folder's, not empty for not having looked yet).
    bool Ready() const { return ready_; }
    // Called on the UI thread after a look's result was put in place (redraw what shows the ids).
    std::function<void()> onChanged;
    // Asks for a look when something under the folder was added, removed or renamed since the last
    // one - once it has been quiet for a moment (a program saving through a file of its own for a
    // moment is seen after it is done). True when it asked.
    bool Poll();
    // A change was seen and the folder is not quiet yet: the owner asks for the next frames.
    bool Settling() const { return changedAt_ >= 0; }
    // The id of a file the editor has just written inside Assets/ (`path` relative to it): given at
    // once - its .meta written now - without waiting for a look. "" when it cannot be given.
    std::string AdoptFile(const std::string& path);

    // An asset's import settings as they are now - with a change of the インスペクター that is still
    // waiting to be written.
    json::Value Import(const std::string& guid) const;
    // A change of them: kept, and written to the .meta by FlushImports - when the edit is finished (a
    // drag let go, typing done), not at every step of it. Whoever reads the asset reads it again then.
    void QueueImport(const std::string& guid, json::Value import);
    // An asset's contents as the editor has them now (a software's own kind of file - a particle
    // effect - edited and not written yet), else its file; nullopt when it is not there.
    std::optional<std::string> Content(const std::string& guid) const;
    // An edit of them: kept, and written to the file by FlushImports (as an import setting is).
    void QueueContent(const std::string& guid, std::string bytes);
    bool HasQueuedImports() const { return !queuedImports_.empty() || !queuedContents_.empty(); }
    // Writes what waits, into each .meta and file (the folder is not looked at again for it).
    void FlushImports();

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
    std::function<void()> onChange_;
    platform::FolderWatch watch_;
    bool watching_ = false;
    double changedAt_ = -1;  // when the watch last told of a change not looked at yet
    double lastAliveCheck_ = 0;  // when the watch was last asked whether it still sees the folder
    // After writing .meta files here: their news asks for one more look once quiet (not thrown away -
    // a change made outside at the same moment would go with it).
    void SettleAfterOwnWrites();
    uint64_t version_ = 0;
    std::set<std::string> lastNotes_;  // what the last look told the console
    std::map<std::string, json::Value> queuedImports_;
    std::map<std::string, std::string> queuedContents_;  // guid -> the file's contents waiting to be written  // guid -> import settings waiting to be written

    // The result of a look, on the UI thread: used only while `number` is the newest.
    void ApplyLook(uint64_t number, asset::Plan plan);
    // Ids changed here (an operation): put in place now; the look asked afterwards confirms them.
    void SetEntries(std::vector<asset::Entry> entries);
    void MovedInIndex(const std::string& from, const std::string& to);  // `from` and what is under it
    void RemovedFromIndex(const std::string& path);                     // `path` and what is under it

    // The .meta files as the last look read them. Shared with the look on the Worker (only one look
    // reads it at a time: an older one still running holds the lock until it sees it was stopped).
    struct CacheBox {
        std::mutex mutex;
        asset::MetaCache cache;
    };
    std::shared_ptr<CacheBox> cache_ = std::make_shared<CacheBox>();
    asset::LookNumber looks_;
    bool looking_ = false;
    bool ready_ = false;
    Worker worker_;  // last: destroyed first - what a look still posts is dropped, never applied to a gone library
};

}  // namespace rm::editor
