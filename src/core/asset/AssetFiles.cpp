#include "core/asset/AssetFiles.h"

#include "base/Utf.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

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
        std::string text;
        if (ReadText(MetaPath(root, f.path), text)) f.metaGuid = GuidInMeta(text).value_or("");
        out.found.push_back(std::move(f));
    }
    for (const fs::path& m : metas) {
        fs::path target = m;
        target.replace_extension();  // "a.png.meta" -> "a.png"
        std::error_code e2;
        if (fs::exists(target, e2)) continue;
        std::string text;
        if (!ReadText(m, text)) continue;
        if (auto guid = GuidInMeta(text)) out.orphans.push_back({Rel(root, target), *guid});
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
            // Could not move it (in use?): write a new one with the same id instead.
            std::string old;
            ReadText(MetaPath(root, m.from), old);
            if (const auto guid = GuidInMeta(old); !guid || !WriteText(to, MetaText(old, *guid))) {
                errors.push_back(std::format("{} の .meta を {} へ動かせませんでした", m.from, m.to));
            }
        }
    }
    for (const Plan::Write& w : plan.writes) {
        const fs::path p = MetaPath(root, w.path);
        std::string old;
        ReadText(p, old);
        if (!WriteText(p, MetaText(old, w.guid))) errors.push_back(std::format("{} の .meta を書けませんでした", w.path));
    }
}

std::vector<Entry> Update(const fs::path& root, std::span<const Entry> previous, std::vector<std::string>& notes) {
    const Scan scan = ScanFolder(root);
    Plan plan = Reconcile(previous, scan.found, scan.orphans, NewGuid);
    ApplyPlan(root, plan, notes);
    for (auto& n : plan.notes) notes.push_back(std::move(n));
    return std::move(plan.entries);
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
