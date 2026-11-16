#include "core/asset/AssetFiles.h"

#include "base/Utf.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace rm::asset {

namespace {

bool ReadText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

}  // namespace

bool WriteMetaFile(const fs::path& p, const std::string& text) {
    fs::path tmp = p;
    tmp += L".tmp";
#ifdef _WIN32
    {
        const HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD wrote = 0;
        const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) && wrote == text.size() && FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }
    return true;
#else
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
#endif
}

namespace {

bool WriteText(const fs::path& p, const std::string& text) { return WriteMetaFile(p, text); }

// Windows' words for an error (UTF-8; std::error_code::message is in the ANSI code page).
std::string ErrorWords(const std::error_code& ec) {
#ifdef _WIN32
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                   static_cast<DWORD>(ec.value()), 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring w = n && buf ? std::wstring(buf, n) : std::wstring();
    if (buf) LocalFree(buf);
    while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ')) w.pop_back();
    if (!w.empty()) return std::format("{} ({})", WideToUtf8(w), ec.value());
#endif
    return std::format("エラー {}", ec.value());
}

// When a file was made (Windows: the creation time; a copy gets a new one, a move keeps it). 0 = unknown.
int64_t CreatedAt(const fs::path& p) {
#ifdef _WIN32
    struct _stat64 st {};
    if (_wstat64(p.c_str(), &st) == 0) return static_cast<int64_t>(st.st_ctime);
#endif
    (void)p;
    return 0;
}

// May the .meta at `p` be written? Only when it is not there, or it is one this engine can read
// whole - a broken or locked one is never written over (what it holds would be lost).
bool SafeToWrite(const fs::path& p, std::string& old, std::string& why) {
    old.clear();
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        if (ec) why = "あるかどうか調べられません";
        return !ec;
    }
    if (!ReadText(p, old)) {
        why = "開けません";
        return false;
    }
    if (ReadMeta(old).state != MetaState::Ok) {
        why = "中身が壊れているので書き換えません";
        return false;
    }
    return true;
}

std::string Rel(const fs::path& root, const fs::path& p) {
    std::string s = PathToUtf8(p.lexically_relative(root));
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

}  // namespace

std::wstring FoldCase(std::wstring_view path) {
    std::wstring s(path);
#ifdef _WIN32
    // Upper case, as the file system compares names (not the user's language's rules).
    if (!s.empty()) CharUpperBuffW(s.data(), static_cast<DWORD>(s.size()));
#endif
    return s;
}

fs::path FullPath(const fs::path& root, std::string_view path) { return path.empty() ? root : root / PathFromUtf8(path); }

fs::path MetaPath(const fs::path& root, std::string_view path) {
    fs::path p = FullPath(root, path);
    p += PathFromUtf8(kMetaSuffix);
    return p;
}

namespace {

// What a .meta says: from the cache when it is the same size with the same time, else read (and
// kept). One that cannot be read is not kept (tried again next time).
bool ReadMetaCached(const fs::path& meta, uintmax_t size, fs::file_time_type time, MetaCache* cache, MetaInfo& info, int64_t& created) {
    if (cache) {
        const auto it = cache->byPath.find(meta.native());
        if (it != cache->byPath.end() && it->second.size == size && it->second.time == time) {
            info = it->second.info;
            created = it->second.created;
            return true;
        }
    }
    std::string text;
    if (!ReadText(meta, text)) return false;
    info = ReadMeta(text);
    created = CreatedAt(meta);
    if (cache) {
        cache->byPath[meta.native()] = {size, time, info, created};
        ++cache->reads;
    }
    return true;
}

}  // namespace

Scan ScanFolder(const fs::path& root, MetaCache* cache, std::stop_token stop) {
    Scan out;
    size_t step = 0;
    auto stopping = [&] {
        if ((++step & 255) != 0 || !stop.stop_requested()) return false;
        out = {};
        out.stopped = true;
        return true;
    };
    if (cache) cache->reads = 0;
    std::error_code ec;
    struct MetaFile {
        uintmax_t size = 0;
        fs::file_time_type time{};
        bool known = false;  // size and time could be read from the listing
        fs::path path;       // as it is spelled in the folder
    };
    // Every .meta under root, by its path with the case folded: Windows sees "tree.png.meta" as the
    // .meta of "Tree.png" too (a file renamed only in its case keeps its .meta - and its id).
    std::unordered_map<std::wstring, MetaFile> metas;
    std::vector<std::pair<fs::path, bool>> items;       // every file / folder (not ignored)
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (stopping()) return out;
        const fs::path& p = it->path();
        const std::string name = PathToUtf8(p.filename());
        std::error_code e2;
        const bool folder = it->is_directory(e2);
        if (!folder && name.size() > kMetaSuffix.size() && Extension(name) == kMetaSuffix) {
            // (the listing already has the size and the time: no more calls to Windows for them)
            MetaFile m;
            std::error_code e3, e4;
            m.size = it->file_size(e3);
            m.time = it->last_write_time(e4);
            m.known = !e3 && !e4;
            m.path = p;
            metas.emplace(FoldCase(p.native()), m);
            continue;
        }
        if (Ignored(name)) {
            if (folder) it.disable_recursion_pending();
            continue;
        }
        items.emplace_back(p, folder);
    }
    if (ec) {
        // Listed only in part (not a folder it may not enter: those are skipped). Without the rest, the
        // files after it would look gone and a folder without its .meta would get a new id: not used.
        out = {};
        out.stopped = true;
        out.problem = ErrorWords(ec);
        return out;
    }
    std::unordered_map<std::wstring, bool> itemSet;
    for (const auto& [p, folder] : items) itemSet.emplace(FoldCase(p.native()), folder);
    std::unordered_map<std::wstring, bool> seen;  // the .meta files used by this look (the rest leave the cache)

    for (const auto& [p, folder] : items) {
        if (stopping()) return out;
        Found f;
        f.path = Rel(root, p);
        f.folder = folder;
        fs::path meta = p;
        meta += PathFromUtf8(kMetaSuffix);
        const auto m = metas.find(FoldCase(meta.native()));
        if (m == metas.end()) {
            f.meta = MetaState::None;
        } else {
            meta = m->second.path;  // (its own spelling: the cache's key)
            seen[meta.native()] = true;
            MetaInfo info;
            int64_t created = 0;
            if (!m->second.known || !ReadMetaCached(meta, m->second.size, m->second.time, cache, info, created)) {
                f.meta = MetaState::Unreadable;
                f.metaProblem = "開けません (ほかのアプリが使っている・同期中など)";
            } else {
                f.meta = info.state;
                f.metaGuid = info.guid;
                f.metaName = info.name;
                f.metaCreated = created;
                if (info.state == MetaState::Unreadable) f.metaProblem = "中身を JSON として読めず、ID も見つかりません";
            }
        }
        out.found.push_back(std::move(f));
    }
    for (const auto& [folded, m] : metas) {
        if (stopping()) return out;
        const std::wstring& metaPath = m.path.native();
        fs::path target(metaPath);
        target.replace_extension();  // "a.png.meta" -> "a.png"
        if (itemSet.contains(FoldCase(target.native()))) continue;
        std::error_code e2;
        if (fs::exists(target, e2)) continue;  // (an ignored file's .meta)
        seen[metaPath] = true;
        MetaInfo info;
        int64_t created = 0;
        if (!m.known || !ReadMetaCached(fs::path(metaPath), m.size, m.time, cache, info, created)) continue;
        if (info.state == MetaState::Ok || info.state == MetaState::Damaged) out.orphans.push_back({Rel(root, target), info.guid});
    }
    if (cache) std::erase_if(cache->byPath, [&](const auto& kv) { return !seen.contains(kv.first); });
    // (a stable order for the orphans: the map has none)
    std::sort(out.orphans.begin(), out.orphans.end(), [](const Orphan& a, const Orphan& b) { return a.path < b.path; });
    std::vector<Entry> tmp;
    tmp.reserve(out.found.size());
    for (const Found& f : out.found) tmp.push_back({f.path, {}, f.folder});
    std::vector<Found> ordered;
    ordered.reserve(out.found.size());
    for (const size_t i : DisplayOrder(tmp)) ordered.push_back(std::move(out.found[i]));
    out.found = std::move(ordered);
    return out;
}

void ApplyPlan(const fs::path& root, const Plan& plan, std::vector<std::string>& errors) {
    for (const Plan::Move& m : plan.moves) {
        std::error_code ec;
        const fs::path to = MetaPath(root, m.to);
        fs::rename(MetaPath(root, m.from), to, ec);
        if (ec) {
            // Could not move it (in use?): write a new one with the same id instead (never over one there).
            std::string old, why, there;
            const bool readable = ReadText(MetaPath(root, m.from), old) && ReadMeta(old).state == MetaState::Ok;
            if (!readable || !SafeToWrite(to, there, why) || !there.empty() || !WriteText(to, MetaText(old, *GuidInMeta(old)))) {
                errors.push_back(std::format("{} の .meta を {} へ動かせませんでした", m.from, m.to));
            }
        }
    }
    for (const Plan::Write& w : plan.writes) {
        const fs::path p = MetaPath(root, w.path);
        std::string old, why;
        if (!SafeToWrite(p, old, why)) {
            errors.push_back(std::format("{}.meta: {}", w.path, why));
            continue;
        }
        if (!WriteText(p, MetaText(old, w.guid, w.name))) errors.push_back(std::format("{} の .meta を書けませんでした", w.path));
    }
}

Plan Look(const fs::path& root, std::span<const Entry> previous, MetaCache* cache, std::stop_token stop, bool* stopped, std::string* problem) {
    const Scan scan = ScanFolder(root, cache, stop);
    if (stopped) *stopped = scan.stopped || stop.stop_requested();
    if (problem) *problem = scan.problem;
    if (scan.stopped || stop.stop_requested()) return {};
    return Reconcile(previous, scan.found, scan.orphans, NewGuid);
}

std::string GiveId(const fs::path& root, std::string_view path, const std::string& guid) {
    const fs::path meta = MetaPath(root, path);
    std::string old, why;
    if (!SafeToWrite(meta, old, why)) {
        // There, but not readable whole: its id when one can be picked out, never written over.
        std::string text;
        if (ReadText(meta, text)) {
            if (const MetaInfo info = ReadMeta(text); info.state == MetaState::Damaged) return info.guid;
        }
        return {};
    }
    if (!old.empty()) return ReadMeta(old).guid;  // it has one already
    if (!WriteText(meta, MetaText(old, guid, FileNameOf(path)))) return {};
    return guid;
}

std::vector<Entry> Update(const fs::path& root, std::span<const Entry> previous, std::vector<std::string>& notes, MetaCache* cache,
                          bool* wrote) {
    const Scan scan = ScanFolder(root, cache);
    Plan plan = Reconcile(previous, scan.found, scan.orphans, NewGuid);
    if (wrote) *wrote = !plan.writes.empty() || !plan.moves.empty();
    ApplyPlan(root, plan, notes);
    for (auto& n : plan.notes) notes.push_back(std::move(n));
    return std::move(plan.entries);
}

bool RecordName(const fs::path& file) {
    fs::path meta = file;
    meta += PathFromUtf8(kMetaSuffix);
    std::string old, why;
    if (!SafeToWrite(meta, old, why) || old.empty()) return false;
    const MetaInfo info = ReadMeta(old);
    const std::string name = PathToUtf8(file.filename());
    if (info.name == name) return true;
    return WriteText(meta, MetaText(old, info.guid, name));
}

std::vector<Entry> ReadIndex(const fs::path& root) {
    std::vector<Entry> out;
    for (const Found& f : ScanFolder(root).found) {
        if (IsGuid(f.metaGuid)) out.push_back({f.path, f.metaGuid, f.folder});
    }
    return out;
}

std::string NewGuid() {
    static std::mutex mutex;  // (looks run on the editor's Worker; the UI thread makes ids too)
    std::lock_guard lock(mutex);
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    const uint64_t hi = rng(), lo = rng();
    return GuidFromNumbers(hi, lo);
}

}  // namespace rm::asset
