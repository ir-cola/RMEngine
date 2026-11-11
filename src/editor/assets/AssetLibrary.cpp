#include "editor/assets/AssetLibrary.h"

#include "base/FileName.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/AssetFiles.h"
#include "editor/lang/Lang.h"

#include <algorithm>
#include <format>

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

// Renames `from` to `to` together with its .meta (the id goes along). Undoes the file when the
// .meta cannot follow, so the two never part.
bool MoveWithMeta(const fs::path& from, const fs::path& to, std::error_code& ec) {
    fs::path metaFrom = from, metaTo = to;
    metaFrom += L".meta";
    metaTo += L".meta";
    fs::rename(from, to, ec);
    if (ec) return false;
    std::error_code metaEc;
    if (fs::exists(metaFrom, metaEc)) {
        fs::rename(metaFrom, metaTo, metaEc);
        if (metaEc) {
            std::error_code back;
            fs::rename(to, from, back);
            ec = metaEc;
            return false;
        }
    }
    return true;
}

}  // namespace

AssetLibrary::AssetLibrary(fs::path root) : root_(std::move(root)) {}

void AssetLibrary::Refresh() {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (!watching_) watching_ = watch_.Start(root_);
    std::vector<std::string> notes;
    std::vector<asset::Entry> before(index_.Entries().begin(), index_.Entries().end());
    std::vector<asset::Entry> now = asset::Update(root_, before, notes);
    for (const std::string& n : notes) log::Info("アセット: {}", n);
    index_.Set(std::move(now));
    order_ = asset::DisplayOrder(index_.Entries());
    ++version_;
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
    if (!MoveWithMeta(from, to, ec)) {
        return Problem(TrKey(L"「{}」の名前を変えられませんでした: {}"), NameOf(*e), Why(ec));
    }
    log::Info("アセットの名前を変えました: {} → {}", e->path, newName);
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
        if (!MoveWithMeta(FullPath(e), to, ec)) {
            problem = Problem(TrKey(L"「{}」を動かせませんでした: {}"), NameOf(e), Why(ec));
            continue;
        }
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
    Refresh();
    if (made) {
        const asset::Entry* e = index_.ByPath(path);
        *made = e ? e->guid : std::string();
    }
    return {};
}

std::wstring AssetLibrary::Delete(std::span<const std::string> guids) {
    std::vector<fs::path> paths;
    std::vector<std::string> names;
    for (const std::string& g : guids) {
        const asset::Entry* e = index_.ByGuid(g);
        if (!e) continue;
        // Something inside a folder that goes too is not listed twice.
        const bool inside = std::any_of(guids.begin(), guids.end(), [&](const std::string& other) {
            const asset::Entry* o = index_.ByGuid(other);
            return o && o != e && o->folder && e->path.starts_with(o->path + "/");
        });
        if (inside) continue;
        paths.push_back(FullPath(*e));
        std::error_code ec;
        fs::path meta = FullPath(*e);
        meta += L".meta";
        if (fs::exists(meta, ec)) paths.push_back(meta);
        names.push_back(e->path);
    }
    if (paths.empty()) return {};
    std::string why;
    const bool ok = platform::MoveToRecycleBin(paths, &why);
    Refresh();
    if (!ok) return Problem(TrKey(L"ごみ箱へ移せませんでした: {}"), Utf8ToWide(why));
    for (const std::string& n : names) log::Info("ごみ箱へ移しました: {}", n);
    return {};
}

}  // namespace rm::editor
