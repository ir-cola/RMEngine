#include "core/asset/AssetFiles.h"

#include "base/Utf.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
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

// Written next to the file and moved in place, so a crash never leaves half a .meta.
bool WriteText(const fs::path& p, const std::string& text) {
    fs::path tmp = p;
    tmp += L".tmp";
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

// What the .meta next to an item says, read with care: one that is there but cannot be opened or
// read is Unreadable (never "missing" - that would give the item a new id and write over it).
void LookAtMeta(const fs::path& meta, Found& f) {
    std::error_code ec;
    if (!fs::exists(meta, ec)) {
        f.meta = ec ? MetaState::Unreadable : MetaState::None;
        if (ec) f.metaProblem = "あるかどうか調べられません";
        return;
    }
    std::string text;
    if (!ReadText(meta, text)) {
        f.meta = MetaState::Unreadable;
        f.metaProblem = "開けません (ほかのアプリが使っている・同期中など)";
        return;
    }
    const MetaInfo info = ReadMeta(text);
    f.meta = info.state;
    f.metaGuid = info.guid;
    f.metaName = info.name;
    f.metaCreated = CreatedAt(meta);
    if (info.state == MetaState::Unreadable) f.metaProblem = "中身を JSON として読めず、ID も見つかりません";
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

fs::path FullPath(const fs::path& root, std::string_view path) { return path.empty() ? root : root / PathFromUtf8(path); }

fs::path MetaPath(const fs::path& root, std::string_view path) {
    fs::path p = FullPath(root, path);
    p += PathFromUtf8(kMetaSuffix);
    return p;
}

Scan ScanFolder(const fs::path& root) {
    Scan out;
    std::error_code ec;
    std::vector<fs::path> metas;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const fs::path& p = it->path();
        const std::string name = PathToUtf8(p.filename());
        std::error_code e2;
        const bool folder = it->is_directory(e2);
        if (!folder && name.size() > kMetaSuffix.size() && Extension(name) == kMetaSuffix) {
            metas.push_back(p);
            continue;
        }
        if (Ignored(name)) {
            if (folder) it.disable_recursion_pending();
            continue;
        }
        Found f;
        f.path = Rel(root, p);
        f.folder = folder;
        LookAtMeta(MetaPath(root, f.path), f);
        out.found.push_back(std::move(f));
    }
    for (const fs::path& m : metas) {
        fs::path target = m;
        target.replace_extension();  // "a.png.meta" -> "a.png"
        std::error_code e2;
        if (fs::exists(target, e2)) continue;
        std::string text;
        if (!ReadText(m, text)) continue;
        if (const MetaInfo info = ReadMeta(text); info.state == MetaState::Ok || info.state == MetaState::Damaged) {
            out.orphans.push_back({Rel(root, target), info.guid});
        }
    }
    std::vector<Entry> tmp;
    for (const Found& f : out.found) tmp.push_back({f.path, {}, f.folder});
    std::vector<Found> ordered;
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

std::vector<Entry> Update(const fs::path& root, std::span<const Entry> previous, std::vector<std::string>& notes) {
    const Scan scan = ScanFolder(root);
    Plan plan = Reconcile(previous, scan.found, scan.orphans, NewGuid);
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
    static std::mt19937_64 rng = [] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }();
    const uint64_t hi = rng(), lo = rng();
    return GuidFromNumbers(hi, lo);
}

}  // namespace rm::asset
