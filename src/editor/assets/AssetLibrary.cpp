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

// 同じメッセージを、コンソール (日本語) と、画面 (使っている言語) に出す。
// `japanese` は TrKey(L"...") (英語があるかは check_lang.ps1 が見る)。
template <class... A>
std::wstring Problem(const wchar_t* japanese, const A&... args) {
    log::Warn("{}", WideToUtf8(std::vformat(std::wstring_view(japanese), std::make_wformat_args(args...))));
    return TrF(japanese, args...);
}

std::wstring Why(const std::error_code& ec) { return Utf8ToWide(ec.message()); }

// ファイルやフォルダーの名前: Windows の名前として 1 つで、一覧が省くものではないこと。
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

// `from` を `to` に、その .meta と一緒に改名し (id は引き継ぐ)、新しい名前を .meta に記録する。
// その .meta が行く場所にもう .meta があるときは断る (上書きされるか、動かしたファイルの
// ものと見なされてしまう)。.meta がついて行けないときはファイルを元に戻し、2 つが離れない
// ようにする。それさえ失敗したらそう伝える。何が悪かったかを (使っている言語で) 返す。空 = 済んだ。
std::wstring MoveWithMeta(const fs::path& from, const fs::path& to, const std::wstring& name) {
    fs::path metaFrom = from, metaTo = to;
    metaFrom += L".meta";
    metaTo += L".meta";
    std::error_code ec;
    const bool sameMeta = fs::equivalent(metaFrom, metaTo, ec);  // (大文字小文字だけが変わる)
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
    asset::RecordName(to);  // (そのコピーにはない名前)
    return {};
}

}  // namespace

AssetLibrary::AssetLibrary(fs::path root, std::function<void()> onChange) : root_(std::move(root)), onChange_(std::move(onChange)) {}

AssetLibrary::~AssetLibrary() { FlushImports(); }

json::Value AssetLibrary::Import(const std::string& guid) const {
    if (const auto it = queuedImports_.find(guid); it != queuedImports_.end()) return it->second;
    const asset::Entry* e = index_.ByGuid(guid);
    return e ? asset::ReadImportJson(FullPath(*e)) : json::Value::MakeObject();
}

void AssetLibrary::QueueImport(const std::string& guid, json::Value import) { queuedImports_[guid] = std::move(import); }

std::optional<std::string> AssetLibrary::Content(const std::string& guid) const {
    if (const auto it = queuedContents_.find(guid); it != queuedContents_.end()) return it->second;
    const asset::Entry* e = index_.ByGuid(guid);
    if (!e) return std::nullopt;
    std::string err;
    return platform::ReadFileBytes(FullPath(*e), &err);
}

void AssetLibrary::QueueContent(const std::string& guid, std::string bytes) { queuedContents_[guid] = std::move(bytes); }

void AssetLibrary::FlushImports() {
    if (queuedImports_.empty() && queuedContents_.empty()) return;
    for (auto& [guid, bytes] : queuedContents_) {
        const asset::Entry* e = index_.ByGuid(guid);
        if (!e) continue;  // (その間に消えた)
        std::string err;
        if (!platform::WriteFileAtomic(FullPath(*e), bytes, &err)) log::Warn("{} に書けませんでした: {}", e->path, err);
    }
    queuedContents_.clear();
    for (auto& [guid, import] : queuedImports_) {
        const asset::Entry* e = index_.ByGuid(guid);
        if (!e) continue;  // (その間に消えた)
        if (!asset::WriteImportJson(FullPath(*e), import, e->guid)) log::Warn("取り込みの設定を {}.meta に書けませんでした (開けない・壊れている)", e->path);
    }
    queuedImports_.clear();
    // これらの .meta ファイルについての監視の知らせ: 静かになった後にもう 1 度調べる (そこからは
    // 何も新しい物は見つからない - キャッシュのおかげで軽い - が、同じ時に外から足されたファイルを
    // 取りこぼさない)。
    SettleAfterOwnWrites();
}

void AssetLibrary::SettleAfterOwnWrites() {
    if (!watching_ || !watch_.Changed()) return;
    while (watch_.Changed()) {
    }
    changedAt_ = platform::NowSeconds();
}

void AssetLibrary::Refresh() {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (!watching_) watching_ = watch_.Start(root_, false, onChange_);
    const uint64_t number = looks_.Next();
    looking_ = true;
    // 調べるときはコピーだけを受け取る (Worker の決まり): フォルダー、最後の id、共有のキャッシュ。
    std::vector<asset::Entry> previous(index_.Entries().begin(), index_.Entries().end());
    worker_.Run([this, number, root = root_, previous = std::move(previous), box = cache_](std::stop_token stop, const Worker::Post& post) {
        std::lock_guard lock(box->mutex);
        if (stop.stop_requested()) return;
        bool stopped = false;
        std::string problem;
        asset::Plan plan = asset::Look(root, previous, &box->cache, stop, &stopped, &problem);
        if (!problem.empty()) {
            // フォルダー全体ではない: id はそのまま (フォルダーが変わるとまた調べる)。
            post([problem] { log::Warn("アセットのフォルダを最後まで読めませんでした (今のままにします): {}", problem); });
            return;
        }
        if (stopped) return;
        // (`this` は post の中でだけ使う: UI スレッドで、ライブラリーがなくなっていたら捨てられる)
        post([this, number, plan = std::move(plan)]() mutable { ApplyLook(number, std::move(plan)); });
    });
}

void AssetLibrary::LookNow() {
    std::error_code ec;
    fs::create_directories(root_, ec);
    if (!watching_) watching_ = watch_.Start(root_, false, onChange_);
    worker_.Stop();  // (まだ続いている調べは追い越される)
    const uint64_t number = looks_.Next();
    looking_ = true;
    asset::Plan plan;
    {
        std::lock_guard lock(cache_->mutex);
        const std::vector<asset::Entry> previous(index_.Entries().begin(), index_.Entries().end());
        bool stopped = false;
        std::string problem;
        plan = asset::Look(root_, previous, &cache_->cache, {}, &stopped, &problem);
        if (stopped) {
            looking_ = false;
            log::Warn("アセットのフォルダを最後まで読めませんでした (今のままにします): {}", problem);
            return;
        }
    }
    ApplyLook(number, std::move(plan));
}

void AssetLibrary::ApplyLook(uint64_t number, asset::Plan plan) {
    if (!looks_.Current(number)) return;  // 追い越された: もっと新しい調べが頼まれた (または、操作が id を変えた)
    // 調べている間にフォルダーが変わった: この調べは見逃したかもしれない - もう一度調べる。
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
    // 自分で書いた .meta ファイル: 静かになったらもう 1 度調べる。その調べは何も書かない
    // (id はもう .meta ファイルの中にある) ので、そこで終わる。その間に外から足されたファイルも見える。
    if (wrote) SettleAfterOwnWrites();
    // (ずっと変わらない知らせ - まだ読めない .meta など - は、調べるたびではなく 1 度だけ伝える)
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
    if (guid.empty() || index_.ByGuid(guid)) return {};  // (別のファイルの id を持った .meta をコピーした: 調べが片付ける)
    std::vector<asset::Entry> entries(index_.Entries().begin(), index_.Entries().end());
    entries.push_back({path, guid, fs::is_directory(full, ec)});
    SetEntries(std::move(entries));
    Refresh();  // (それを見なかった古い調べは捨てられる)
    return guid;
}

bool AssetLibrary::Poll() {
    const double now = platform::NowSeconds();
    // フォルダーが消された / 作り直された / 最初は監視できなかった: また監視して、調べる。
    if (now - lastAliveCheck_ >= 1.0) {
        lastAliveCheck_ = now;
        if (!watching_ || watch_.Lost()) {
            const bool was = watching_;
            watching_ = watch_.Start(root_, false, onChange_);
            if (was || watching_) changedAt_ = now;
        }
    }
    if (!watching_) return false;
    if (watch_.Changed()) {
        while (watch_.Changed()) {
        }
        changedAt_ = now;
    }
    constexpr double kQuietSeconds = 0.3;
    if (changedAt_ < 0 || now - changedAt_ < kQuietSeconds) return false;
    changedAt_ = -1;
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
    // (大文字小文字だけが違う名前は Windows では同じファイル: それでも改名する)
    const bool sameFile = fs::equivalent(from, to, ec);
    if (!sameFile && fs::exists(to, ec)) return Problem(TrKey(L"「{}」はもうあります。"), Utf8ToWide(newName));
    if (std::wstring problem = MoveWithMeta(from, to, NameOf(*e)); !problem.empty()) {
        Refresh();
        return problem;
    }
    log::Info("アセットの名前を変えました: {} → {}", e->path, newName);
    const std::string oldPath = e->path;
    MovedInIndex(oldPath, asset::Join(asset::ParentOf(oldPath), newName));  // すぐに見せる。調べが確かめる
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
    // 先にコピーしておく: 進むにつれて索引が変わる。
    std::vector<asset::Entry> moving;
    for (const std::string& g : guids) {
        if (const asset::Entry* e = index_.ByGuid(g)) moving.push_back(*e);
    }
    std::wstring problem;
    int moved = 0;
    for (const asset::Entry& e : moving) {
        if (asset::ParentOf(e.path) == folder) continue;  // もうそこにある
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
    const std::string guid = AdoptFile(path);  // その id (パネルが選んで、名前を付けさせる)
    if (made) *made = guid;
    return {};
}

std::wstring AssetLibrary::NewFile(const std::string& whereGuid, const std::string& defaultFolder, const std::string& baseNameUtf8,
                                   const std::string& extension, const std::string& bytes, std::string* made) {
    std::string folder;
    std::error_code ec;
    if (const asset::Entry* e = whereGuid.empty() ? nullptr : index_.ByGuid(whereGuid)) {
        folder = e->folder ? e->path : std::string(asset::ParentOf(e->path));
    } else if (!defaultFolder.empty()) {
        // (決まったフォルダ: 無ければ作り、先にその id を付ける - ファイルがその下に並ぶように)
        folder = defaultFolder;
        if (!fs::is_directory(asset::FullPath(root_, folder), ec)) {
            fs::create_directories(asset::FullPath(root_, folder), ec);
            if (ec) return Problem(TrKey(L"フォルダを作れませんでした: {}"), Why(ec));
            log::Info("フォルダを作りました: {}", folder);
        }
        AdoptFile(folder);
    }
    std::string name = baseNameUtf8 + extension;
    for (int i = 2; fs::exists(asset::FullPath(root_, asset::Join(folder, name)), ec) && i < 1000; ++i) name = std::format("{} {}{}", baseNameUtf8, i, extension);
    const std::string path = asset::Join(folder, name);
    std::string err;
    if (!platform::WriteFileAtomic(asset::FullPath(root_, path), bytes, &err)) {
        return Problem(TrKey(L"ファイルを作れませんでした: {}"), Utf8ToWide(err));
    }
    log::Info("ファイルを作りました: {}", path);
    const std::string guid = AdoptFile(path);  // その id (パネルが選んで、名前を付けさせる)
    if (made) *made = guid;
    return {};
}

std::wstring AssetLibrary::Delete(std::span<const std::string> guids) {
    // アセットを 1 つずつ (ファイルとその .meta を一緒に): ごみ箱へ行けない物は残り、
    // ほかは消える。
    struct One {
        std::vector<fs::path> paths;
        std::string name;
    };
    std::vector<One> items;
    for (const std::string& g : guids) {
        const asset::Entry* e = index_.ByGuid(g);
        if (!e) continue;
        // 一緒に消えるフォルダーの中の物は、2 度は並べない。
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
            // 完全には消さない (そこにごみ箱がない、または大きすぎて入らない)。
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
