#include "editor/assets/AssetLibrary.h"

#include "base/FileName.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/ImportSettings.h"
#include "editor/lang/Lang.h"

#include <algorithm>
#include <format>
#include <set>

namespace fs = std::filesystem;

namespace rm::editor {

namespace {

// The same message to the console (in Japanese) and back to the screen (in the language in use).
// `japanese` is a TrKey(L"...") (check_lang.ps1 sees the English is there).
template <class... A>
std::wstring Problem(const wchar_t* japanese, const A&... args) {
    log::Warn("{}", WideToUtf8(std::vformat(std::wstring_view(japanese), std::make_wformat_args(args...))));
    return TrF(japanese, args...);
}

std::wstring Why(const std::error_code& ec) { return Utf8ToWide(ec.message()); }

// A name for a file or folder: one Windows name, and not one the list leaves out.
const wchar_t* NameProblem(const std::string& name) {
    switch (ValidateFileName(name, 255)) {
        case FileNameError::None: break;
        case FileNameError::Empty: return TrKey(L"名前を入力してください。");
        case FileNameError::TooLong: return TrKey(L"名前が長すぎます。");
        case FileNameError::InvalidChar: return TrKey(L"使えない文字が入っています ( < > : \" / \\ | ? * )。");
        case FileNameError::ReservedName: return TrKey(L"Windows が予約している名前なので使えません (CON / NUL / COM1 など)。");
        case FileNameError::LeadingSpace: return TrKey(L"先頭に空白は使えません。");
        case FileNameError::TrailingDotOrSpace: return TrKey(L"末尾にピリオドや空白は使えません。");
    }
    if (asset::Ignored(name)) return TrKey(L"「.」で始まる名前と .meta で終わる名前は使えません。");
    return nullptr;
}

// Renames `from` to `to` together with its .meta (the id goes along), and records the new name in the
// .meta. Refuses when a .meta is already where its .meta would go (it would be written over, or
// taken as the moved file's). Puts the file back when the .meta cannot follow, so the two never part
// - and says so when even that fails. Returns what went wrong (in the language in use), empty = done.
std::wstring MoveWithMeta(const fs::path& from, const fs::path& to, const std::wstring& name) {
    fs::path metaFrom = from, metaTo = to;
    metaFrom += L".meta";
    metaTo += L".meta";
    std::error_code ec;
    const bool sameMeta = fs::equivalent(metaFrom, metaTo, ec);  // (only the letter case changes)
    if (!sameMeta && fs::exists(metaTo, ec)) {
        return Problem(TrKey(L"行き先にもう「{}」があるので、「{}」を動かしていません (ID がぶつかります)。"), metaTo.filename().native(), name);
    }
    fs::rename(from, to, ec);
    if (ec) return Problem(TrKey(L"「{}」を動かせませんでした: {}"), name, Why(ec));
    std::error_code metaEc;
    if (fs::exists(metaFrom, metaEc)) {
        fs::rename(metaFrom, metaTo, metaEc);
        if (metaEc) {
            std::error_code back;
            fs::rename(to, from, back);
            if (back) {
                log::Error("「{}」の .meta を動かせず、ファイルを元に戻すこともできませんでした。ファイルは {}、.meta は {} にあります: {} / {}",
                           WideToUtf8(name), PathToUtf8(to), PathToUtf8(metaFrom), metaEc.message(), back.message());
                return TrF(L"「{}」の .meta を動かせず、ファイルを元の場所に戻すこともできませんでした。ファイルは「{}」、.meta は「{}」にあります。エクスプローラーで .meta をファイルの横へ動かしてください",
                           name, to.native(), metaFrom.native());
            }
            return Problem(TrKey(L"「{}」の .meta を動かせなかったので、動かしていません: {}"), name, Why(metaEc));
        }
    }
    asset::RecordName(to);  // (the name a copy of it would not have)
    return {};
}

}  // namespace

AssetLibrary::AssetLibrary(fs::path root) : root_(std::move(root)) {}

AssetLibrary::~AssetLibrary() { FlushImports(); }

json::Value AssetLibrary::Import(const std::string& guid) const {
    if (const auto it = queuedImports_.find(guid); it != queuedImports_.end()) return it->second;
    const asset::Entry* e = index_.ByGuid(guid);
    return e ? asset::ReadImportJson(FullPath(*e)) : json::Value::MakeObject();
}

void AssetLibrary::QueueImport(const std::string& guid, json::Value import) { queuedImports_[guid] = std::move(import); }

void AssetLibrary::FlushImports() {
    if (queuedImports_.empty()) return;
    for (auto& [guid, import] : queuedImports_) {
        const asset::Entry* e = index_.ByGuid(guid);
        if (!e) continue;  // (deleted meanwhile)
        if (!asset::WriteImportJson(FullPath(*e), import)) log::Warn("取り込みの設定を {}.meta に書けませんでした (開けない・壊れている)", e->path);
    }
    queuedImports_.clear();
    // Only .meta files changed (no file came or went): the watch's news of them needs no look at the
    // whole folder. Whoever reads those assets sees the .meta's new time and reads them again.
    if (watching_) {
        while (watch_.Changed()) {
        }
    }
}

void AssetLibrary::Refresh() {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (!watching_) watching_ = watch_.Start(root_);
    const uint64_t number = looks_.Next();
    looking_ = true;
    // The look gets copies only (the Worker's rule): the folder, the last ids, the shared cache.
    std::vector<asset::Entry> previous(index_.Entries().begin(), index_.Entries().end());
    worker_.Run([this, number, root = root_, previous = std::move(previous), box = cache_](std::stop_token stop, const Worker::Post& post) {
        std::lock_guard lock(box->mutex);
        if (stop.stop_requested()) return;
        bool stopped = false;
        asset::Plan plan = asset::Look(root, previous, &box->cache, stop, &stopped);
        if (stopped) return;
        // (`this` is used only inside post: on the UI thread, and dropped if the library is gone)
        post([this, number, plan = std::move(plan)]() mutable { ApplyLook(number, std::move(plan)); });
    });
}

void AssetLibrary::LookNow() {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (!watching_) watching_ = watch_.Start(root_);
    worker_.Stop();  // (a look still going is overtaken)
    const uint64_t number = looks_.Next();
    looking_ = true;
    asset::Plan plan;
    {
        std::lock_guard lock(cache_->mutex);
        const std::vector<asset::Entry> previous(index_.Entries().begin(), index_.Entries().end());
        plan = asset::Look(root_, previous, &cache_->cache, {}, nullptr);
    }
    ApplyLook(number, std::move(plan));
}

void AssetLibrary::ApplyLook(uint64_t number, asset::Plan plan) {
    if (!looks_.Current(number)) return;  // overtaken: a newer look was asked (or an operation changed the ids)
    // The folder changed while it was looked at: this look may have missed it - look again.
    if (watching_ && watch_.Changed()) {
        while (watch_.Changed()) {
        }
        Refresh();
        return;
    }
    looking_ = false;
    std::vector<std::string> notes;
    const bool wrote = !plan.writes.empty() || !plan.moves.empty();
    asset::ApplyPlan(root_, plan, notes);
    for (auto& n : plan.notes) notes.push_back(std::move(n));
    // The .meta files it wrote itself: the watch would only bring them back as news.
    if (wrote && watching_) {
        while (watch_.Changed()) {
        }
    }
    // (a note that stays true - a .meta that still cannot be read - is told once, not at every look)
    std::set<std::string> told;
    for (std::string& n : notes) {
        if (!lastNotes_.contains(n)) log::Warn("アセット: {}", n);
        told.insert(std::move(n));
    }
    lastNotes_ = std::move(told);
    ready_ = true;
    SetEntries(std::move(plan.entries));
    if (onChanged) onChanged();
}

void AssetLibrary::SetEntries(std::vector<asset::Entry> entries) {
    index_.Set(std::move(entries));
    order_ = asset::DisplayOrder(index_.Entries());
    ++version_;
}

void AssetLibrary::MovedInIndex(const std::string& from, const std::string& to) {
    std::vector<asset::Entry> entries(index_.Entries().begin(), index_.Entries().end());
    for (asset::Entry& e : entries) {
        if (e.path == from) {
            e.path = to;
        } else if (e.path.starts_with(from + "/")) {
            e.path = to + e.path.substr(from.size());
        }
    }
    SetEntries(std::move(entries));
}

void AssetLibrary::RemovedFromIndex(const std::string& path) {
    std::vector<asset::Entry> entries(index_.Entries().begin(), index_.Entries().end());
    std::erase_if(entries, [&](const asset::Entry& e) { return e.path == path || e.path.starts_with(path + "/"); });
    SetEntries(std::move(entries));
}

std::string AssetLibrary::AdoptFile(const std::string& path) {
    if (const asset::Entry* e = index_.ByPath(path)) return e->guid;
    std::error_code ec;
    const fs::path full = asset::FullPath(root_, path);
    if (!fs::exists(full, ec)) return {};
    std::string guid = asset::GiveId(root_, path, asset::NewGuid());
    if (guid.empty() || index_.ByGuid(guid)) return {};  // (a .meta copied with another file's id: the look sorts it out)
    std::vector<asset::Entry> entries(index_.Entries().begin(), index_.Entries().end());
    entries.push_back({path, guid, fs::is_directory(full, ec)});
    SetEntries(std::move(entries));
    Refresh();  // (and an older look that did not see it is dropped)
    return guid;
}

bool AssetLibrary::Poll() {
    if (!watching_ || !watch_.Changed()) return false;
    while (watch_.Changed()) {
    }
    Refresh();
    return true;
}

fs::path AssetLibrary::FullPath(const asset::Entry& e) const { return asset::FullPath(root_, e.path); }

std::wstring AssetLibrary::NameOf(const asset::Entry& e) const { return Utf8ToWide(asset::FileNameOf(e.path)); }

std::wstring AssetLibrary::FolderOf(const asset::Entry& e) const {
    const std::string_view parent = asset::ParentOf(e.path);
    return parent.empty() ? std::wstring(L"Assets") : L"Assets/" + Utf8ToWide(parent);
}

std::wstring AssetLibrary::Rename(const std::string& guid, const std::string& newName) {
    const asset::Entry* e = index_.ByGuid(guid);
    if (!e) return {};
    if (const wchar_t* bad = NameProblem(newName)) return Problem(bad);
    const fs::path from = FullPath(*e);
    const fs::path to = from.parent_path() / PathFromUtf8(newName);
    std::error_code ec;
    // (a different name only in letter case is the same file to Windows: rename it all the same)
    const bool sameFile = fs::equivalent(from, to, ec);
    if (!sameFile && fs::exists(to, ec)) return Problem(TrKey(L"「{}」はもうあります。"), Utf8ToWide(newName));
    if (std::wstring problem = MoveWithMeta(from, to, NameOf(*e)); !problem.empty()) {
        Refresh();
        return problem;
    }
    log::Info("アセットの名前を変えました: {} → {}", e->path, newName);
    const std::string oldPath = e->path;
    MovedInIndex(oldPath, asset::Join(asset::ParentOf(oldPath), newName));  // shown at once; the look confirms it
    Refresh();
    return {};
}

std::wstring AssetLibrary::Move(std::span<const std::string> guids, const std::string& folderGuid) {
    std::string folder;
    if (!folderGuid.empty()) {
        const asset::Entry* f = index_.ByGuid(folderGuid);
        if (!f || !f->folder) return {};
        folder = f->path;
    }
    // Copied first: the index changes as they go.
    std::vector<asset::Entry> moving;
    for (const std::string& g : guids) {
        if (const asset::Entry* e = index_.ByGuid(g)) moving.push_back(*e);
    }
    std::wstring problem;
    int moved = 0;
    for (const asset::Entry& e : moving) {
        if (asset::ParentOf(e.path) == folder) continue;  // already there
        if (folder == e.path || folder.starts_with(e.path + "/")) {
            problem = Problem(TrKey(L"「{}」を自分の中には動かせません。"), NameOf(e));
            continue;
        }
        const fs::path to = asset::FullPath(root_, asset::Join(folder, asset::FileNameOf(e.path)));
        std::error_code ec;
        if (fs::exists(to, ec)) {
            problem = Problem(TrKey(L"動かす先に同じ名前の「{}」があります。"), NameOf(e));
            continue;
        }
        if (std::wstring p = MoveWithMeta(FullPath(e), to, NameOf(e)); !p.empty()) {
            problem = std::move(p);
            continue;
        }
        MovedInIndex(e.path, asset::Join(folder, asset::FileNameOf(e.path)));
        ++moved;
    }
    if (moved > 0) log::Info("アセットを {} 個、{} へ動かしました", moved, folder.empty() ? std::string("Assets") : "Assets/" + folder);
    Refresh();
    return problem;
}

std::wstring AssetLibrary::NewFolder(const std::string& whereGuid, std::string* made) {
    std::string folder;
    if (const asset::Entry* e = whereGuid.empty() ? nullptr : index_.ByGuid(whereGuid)) {
        folder = e->folder ? e->path : std::string(asset::ParentOf(e->path));
    }
    const std::string base = TrU8("新しいフォルダ");
    std::string name = base;
    std::error_code ec;
    for (int i = 2; fs::exists(asset::FullPath(root_, asset::Join(folder, name)), ec) && i < 1000; ++i) name = std::format("{} {}", base, i);
    const std::string path = asset::Join(folder, name);
    if (!fs::create_directory(asset::FullPath(root_, path), ec)) {
        return Problem(TrKey(L"フォルダを作れませんでした: {}"), Why(ec));
    }
    log::Info("フォルダを作りました: {}", path);
    const std::string guid = AdoptFile(path);  // its id now (the panel selects it to be named)
    if (made) *made = guid;
    return {};
}

std::wstring AssetLibrary::Delete(std::span<const std::string> guids) {
    // One asset at a time (the file and its .meta together): one that cannot go to the Recycle Bin
    // stays, and the others still go.
    struct One {
        std::vector<fs::path> paths;
        std::string name;
    };
    std::vector<One> items;
    for (const std::string& g : guids) {
        const asset::Entry* e = index_.ByGuid(g);
        if (!e) continue;
        // Something inside a folder that goes too is not listed twice.
        const bool inside = std::any_of(guids.begin(), guids.end(), [&](const std::string& other) {
            const asset::Entry* o = index_.ByGuid(other);
            return o && o != e && o->folder && e->path.starts_with(o->path + "/");
        });
        if (inside) continue;
        One one;
        one.paths.push_back(FullPath(*e));
        std::error_code ec;
        fs::path meta = FullPath(*e);
        meta += L".meta";
        if (fs::exists(meta, ec)) one.paths.push_back(meta);
        one.name = e->path;
        items.push_back(std::move(one));
    }
    if (items.empty()) return {};
    std::wstring problem;
    for (const One& one : items) {
        std::string why;
        bool notRecyclable = false;
        if (platform::MoveToRecycleBin(one.paths, &why, &notRecyclable)) {
            log::Info("ごみ箱へ移しました: {}", one.name);
            RemovedFromIndex(one.name);
        } else if (notRecyclable) {
            // Never deleted for good (no Recycle Bin there, or it is too big for it).
            problem = Problem(TrKey(L"「{}」はごみ箱に入れられない場所にある (USB メモリ・ネットワーク・ごみ箱より大きい) ので、消していません。消すならエクスプローラーで消してください。"),
                              Utf8ToWide(one.name));
        } else {
            problem = Problem(TrKey(L"「{}」をごみ箱へ移せませんでした: {}"), Utf8ToWide(one.name), Utf8ToWide(why));
        }
    }
    Refresh();
    return problem;
}

}  // namespace rm::editor
