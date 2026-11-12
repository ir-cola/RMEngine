#include "core/asset/AssetIndex.h"

#include "base/Json.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace rm::asset {

namespace {

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string LowerText(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = Lower(c);
    return out;
}

struct KindName {
    Kind kind;
    std::string_view key;
};
constexpr KindName kKinds[] = {{Kind::Folder, "folder"}, {Kind::Image, "image"}, {Kind::Model, "model"}, {Kind::Audio, "audio"},
                               {Kind::Font, "font"},     {Kind::Text, "text"},   {Kind::Other, "other"}};

// Splits "a,b, c" into its keys (spaces around them ignored).
std::vector<std::string_view> Keys(std::string_view filter) {
    std::vector<std::string_view> out;
    while (!filter.empty()) {
        const size_t comma = filter.find(',');
        std::string_view k = filter.substr(0, comma);
        while (!k.empty() && k.front() == ' ') k.remove_prefix(1);
        while (!k.empty() && k.back() == ' ') k.remove_suffix(1);
        out.push_back(k);
        if (comma == std::string_view::npos) break;
        filter.remove_prefix(comma + 1);
    }
    return out;
}

}  // namespace

Kind KindOf(std::string_view fileName, bool folder) {
    if (folder) return Kind::Folder;
    static const std::map<std::string, Kind, std::less<>> kExt = {
        {".png", Kind::Image},  {".ico", Kind::Image},  {".jpg", Kind::Image},  {".jpeg", Kind::Image}, {".bmp", Kind::Image},  {".tga", Kind::Image},
        {".gif", Kind::Image},  {".psd", Kind::Image},  {".dds", Kind::Image},  {".hdr", Kind::Image},  {".exr", Kind::Image},
        {".webp", Kind::Image}, {".tif", Kind::Image},  {".tiff", Kind::Image}, {".obj", Kind::Model},  {".fbx", Kind::Model},
        {".gltf", Kind::Model}, {".glb", Kind::Model},  {".dae", Kind::Model},  {".blend", Kind::Model}, {".3ds", Kind::Model},
        {".ply", Kind::Model},  {".stl", Kind::Model},  {".wav", Kind::Audio},  {".ogg", Kind::Audio},  {".mp3", Kind::Audio},
        {".flac", Kind::Audio}, {".aif", Kind::Audio},  {".aiff", Kind::Audio}, {".mid", Kind::Audio},  {".midi", Kind::Audio},
        {".ttf", Kind::Font},   {".otf", Kind::Font},   {".txt", Kind::Text},   {".md", Kind::Text},    {".json", Kind::Text},
        {".csv", Kind::Text},   {".xml", Kind::Text},   {".yaml", Kind::Text},  {".yml", Kind::Text},   {".lua", Kind::Text},
    };
    const auto it = kExt.find(Extension(fileName));
    return it != kExt.end() ? it->second : Kind::Other;
}

std::string_view KindKey(Kind k) {
    for (const auto& n : kKinds) {
        if (n.kind == k) return n.key;
    }
    return "other";
}

std::optional<Kind> KindFromKey(std::string_view key) {
    for (const auto& n : kKinds) {
        if (n.key == key) return n.kind;
    }
    return std::nullopt;
}

bool KindMatches(Kind k, std::string_view filter) {
    const std::vector<std::string_view> keys = Keys(filter);
    if (keys.empty()) return k != Kind::Folder;
    return std::any_of(keys.begin(), keys.end(), [&](std::string_view key) { return KindFromKey(key) == k; });
}

bool ValidKindFilter(std::string_view filter) {
    for (const std::string_view key : Keys(filter)) {
        if (!KindFromKey(key)) return false;
    }
    return true;
}

bool IsGuid(std::string_view s) {
    if (s.size() != 32) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string GuidFromNumbers(uint64_t hi, uint64_t lo) { return std::format("{:016x}{:016x}", hi, lo); }

std::string_view ParentOf(std::string_view path) {
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
}

std::string_view FileNameOf(std::string_view path) {
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string Extension(std::string_view name) {
    name = FileNameOf(name);
    const size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return {};
    return LowerText(name.substr(dot));
}

std::string Join(std::string_view folder, std::string_view name) {
    if (folder.empty()) return std::string(name);
    return std::string(folder) + "/" + std::string(name);
}

bool Ignored(std::string_view name) {
    if (name.empty() || name.front() == '.') return true;
    const std::string lower = LowerText(name);
    if (lower == "desktop.ini" || lower == "thumbs.db") return true;
    if (lower.ends_with(".meta.tmp")) return true;  // a .meta being written (or left by a failed write)
    return lower.size() > kMetaSuffix.size() && lower.ends_with(kMetaSuffix);
}

MetaInfo ReadMeta(std::string_view text) {
    MetaInfo m;
    if (const auto v = json::Parse(text); v && v->IsObject()) {
        const json::Value& g = v->Get("guid");
        if (g.IsString() && IsGuid(g.AsString())) {
            m.state = MetaState::Ok;
            m.guid = g.AsString();
            if (v->Get("name").IsString()) m.name = v->Get("name").AsString();
            return m;
        }
        m.state = MetaState::Unreadable;  // JSON, but no id in it
        return m;
    }
    // Not JSON (a comma too many, git's conflict marks ...): the id is still picked out when it is
    // written plainly - but such a file is never written over (what else it held would be lost).
    m.state = MetaState::Unreadable;
    const size_t key = text.find("\"guid\"");
    if (key == std::string_view::npos) return m;
    size_t i = key + 6;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size() || text[i] != ':') return m;
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size() || text[i] != '"' || i + 33 >= text.size() || text[i + 33] != '"') return m;
    const std::string_view guid = text.substr(i + 1, 32);
    if (!IsGuid(guid)) return m;
    // Only one id in it (two - a merge conflict between two of them - is not clear).
    if (text.find("\"guid\"", key + 6) != std::string_view::npos) return m;
    m.state = MetaState::Damaged;
    m.guid = std::string(guid);
    return m;
}

std::optional<std::string> GuidInMeta(std::string_view text) {
    const MetaInfo m = ReadMeta(text);
    if (m.state != MetaState::Ok) return std::nullopt;
    return m.guid;
}

std::string MetaText(std::string_view old, const std::string& guid, std::string_view name) {
    json::Value doc = json::Value::MakeObject();
    if (auto v = json::Parse(old); v && v->IsObject()) doc = std::move(*v);  // the other keys stay
    doc.Set("guid", guid);
    if (!name.empty()) doc.Set("name", std::string(name));
    return json::Write(doc) + "\n";
}

// ---------------------------------------------------------------------------
// Reconcile
// ---------------------------------------------------------------------------

namespace {

bool HasId(const Found& f) { return f.meta != MetaState::Unreadable && IsGuid(f.metaGuid); }

// Several items whose .meta has the same id (a file copied with its .meta): which is the original.
// `sure` = false when nothing tells them apart (the first in the list is taken).
size_t Original(std::span<const Found> found, const std::vector<size_t>& list, const std::string* previousPath, bool& sure) {
    sure = true;
    // a. Where the id was at the last look (the editor saw it there).
    if (previousPath) {
        for (const size_t i : list) {
            if (found[i].path == *previousPath) return i;
        }
    }
    // b. The name the .meta was written for: a copy ("X - コピー.png") keeps the original's name.
    std::vector<size_t> named;
    for (const size_t i : list) {
        if (!found[i].metaName.empty() && FileNameOf(found[i].path) == found[i].metaName) named.push_back(i);
    }
    if (named.size() == 1) return named[0];
    // c. The .meta made first (a copy's is made when it is copied; moving keeps the time). Only when
    //    every time is known and the earliest is one alone.
    const std::vector<size_t>& pool = named.empty() ? list : named;
    if (pool.size() == 1) return pool.front();
    const bool known = std::all_of(pool.begin(), pool.end(), [&](size_t i) { return found[i].metaCreated > 0; });
    if (known) {
        const size_t first = *std::min_element(pool.begin(), pool.end(), [&](size_t a, size_t b) { return found[a].metaCreated < found[b].metaCreated; });
        const auto ties = std::count_if(pool.begin(), pool.end(), [&](size_t i) { return found[i].metaCreated == found[first].metaCreated; });
        if (ties == 1) return first;
    }
    sure = false;
    return pool.front();
}

}  // namespace

Plan Reconcile(std::span<const Entry> previous, std::span<const Found> found, std::span<const Orphan> orphans,
               const std::function<std::string()>& newGuid) {
    Plan plan;
    const size_t n = found.size();
    std::vector<std::string> ids(n);
    std::vector<bool> skipped(n, false);  // no id now (its .meta cannot be read / cannot be given its own)
    std::set<std::string, std::less<>> taken;
    std::map<std::string, std::string, std::less<>> previousPath;  // guid -> where it was
    std::map<std::string, std::string, std::less<>> previousId;    // path -> its guid
    for (const Entry& e : previous) {
        previousPath[e.guid] = e.path;
        previousId[e.path] = e.guid;
    }

    // 0. A .meta that is there but cannot be read (another program has it open, its text is broken
    //    and no id can be picked out): never written over, never given a new id. The item keeps the
    //    id it had at the last look, else it waits (not listed) until the .meta can be read.
    for (size_t i = 0; i < n; ++i) {
        if (found[i].meta != MetaState::Unreadable) continue;
        const std::string why = found[i].metaProblem.empty() ? std::string("読めません") : found[i].metaProblem;
        if (const auto it = previousId.find(found[i].path); it != previousId.end()) {
            ids[i] = it->second;
            taken.insert(it->second);
            plan.notes.push_back(std::format("{}.meta を読めません ({})。前の ID のまま使い、.meta は書き換えません", found[i].path, why));
        } else {
            skipped[i] = true;
            plan.notes.push_back(std::format("{}.meta を読めないので、ID が分からないまま一覧から外しています ({})。.meta は書き換えません。読めるようになれば元どおり",
                                             found[i].path, why));
        }
    }

    // 1. Items with their own .meta. A guid claimed twice (a copy made with its .meta) stays with the
    //    original (Original); the others get new ids below.
    std::map<std::string, std::vector<size_t>, std::less<>> claims;
    for (size_t i = 0; i < n; ++i) {
        if (ids[i].empty() && !skipped[i] && HasId(found[i])) claims[found[i].metaGuid].push_back(i);
    }
    std::vector<bool> copied(n, false);
    for (const auto& [guid, list] : claims) {
        if (taken.count(guid)) {  // (kept by an item whose .meta cannot be read now)
            for (const size_t i : list) copied[i] = true;
            continue;
        }
        const auto prev = previousPath.find(guid);
        bool sure = true;
        const size_t winner = Original(found, list, prev != previousPath.end() ? &prev->second : nullptr, sure);
        ids[winner] = guid;
        taken.insert(guid);
        for (const size_t i : list) {
            if (i != winner) copied[i] = true;
        }
        if (!sure) plan.notes.push_back(std::format("同じ ID の .meta が {} 個あり、どれが元か分からないので {} を元にしました", list.size(), found[winner].path));
    }

    // 2. Items without one: the id of something that vanished, when it is clearly the same thing.
    //    Everything is looked up through indexes made once (by path, by name, by folder + extension,
    //    by id): the time grows with the number of files, not with its square.
    struct Gone {
        std::string path, guid;
        bool hasMeta = false;  // an Orphan (its .meta moves along) / only in `previous` (a .meta is written)
        bool used = false;
        std::string name, place;  // FileNameOf(path) / folder + '\0' + extension (made once)
    };
    std::vector<Gone> gone;
    std::unordered_set<std::string> goneGuids;
    auto addGone = [&](const std::string& path, const std::string& guid, bool hasMeta) {
        if (!goneGuids.insert(guid).second && !hasMeta) return;  // (an id of `previous` already left as a .meta)
        Gone g{path, guid, hasMeta};
        g.name = std::string(FileNameOf(path));
        g.place = std::string(ParentOf(path)) + '\0' + Extension(path);
        gone.push_back(std::move(g));
    };
    for (const Orphan& o : orphans) {
        if (IsGuid(o.guid) && !taken.count(o.guid)) addGone(o.path, o.guid, true);
    }
    for (const Entry& e : previous) {
        if (!taken.count(e.guid) && !goneGuids.count(e.guid)) addGone(e.path, e.guid, false);  // (its path may be found: the .meta was deleted - a. below)
    }
    std::unordered_map<std::string, std::vector<size_t>> goneByPath, goneByName, goneByPlace, goneByGuid;
    std::unordered_map<std::string, int> unusedByName, unusedByPlace;  // how many not used yet
    for (size_t k = 0; k < gone.size(); ++k) {
        goneByPath[gone[k].path].push_back(k);
        goneByName[gone[k].name].push_back(k);
        goneByPlace[gone[k].place].push_back(k);
        goneByGuid[gone[k].guid].push_back(k);
        ++unusedByName[gone[k].name];
        ++unusedByPlace[gone[k].place];
    }
    // The same guid may be left twice (two .meta copies): use it once.
    auto give = [&](size_t i, Gone& g, const char* why) {
        ids[i] = g.guid;
        taken.insert(g.guid);
        for (const size_t k : goneByGuid[g.guid]) {
            if (gone[k].used) continue;
            gone[k].used = true;
            --unusedByName[gone[k].name];
            --unusedByPlace[gone[k].place];
        }
        if (g.hasMeta) plan.moves.push_back({g.path, found[i].path});
        // (after a move: the .meta now says the new name - a copy of it later is told apart by it)
        plan.writes.push_back({found[i].path, g.guid, std::string(FileNameOf(found[i].path))});
        if (g.path != found[i].path) plan.notes.push_back(std::format("{} → {} ({}。ID はそのまま)", g.path, found[i].path, why));
    };
    // The unused one of a list (when the count says there is exactly one).
    auto unusedIn = [&](const std::vector<size_t>& list) -> Gone* {
        for (const size_t k : list) {
            if (!gone[k].used) return &gone[k];
        }
        return nullptr;
    };
    std::vector<size_t> open;  // items still without an id
    for (size_t i = 0; i < n; ++i) {
        if (ids[i].empty() && !copied[i] && !skipped[i]) open.push_back(i);
    }
    // a. Its own old path (the .meta was deleted, the file is where it was).
    for (const size_t i : open) {
        if (const auto it = goneByPath.find(found[i].path); it != goneByPath.end()) {
            if (Gone* g = unusedIn(it->second)) give(i, *g, "同じ場所");
        }
    }
    std::erase_if(open, [&](size_t i) { return !ids[i].empty(); });
    // b. The only vanished thing with the same name, and the only new one with it (moved).
    {
        std::unordered_map<std::string, int> rivals;  // new ones by name
        for (const size_t i : open) ++rivals[std::string(FileNameOf(found[i].path))];
        for (const size_t i : open) {
            const std::string name(FileNameOf(found[i].path));
            if (rivals[name] != 1) continue;
            const auto c = unusedByName.find(name);
            if (c == unusedByName.end() || c->second != 1) continue;
            if (Gone* g = unusedIn(goneByName[name])) give(i, *g, "移動");
        }
    }
    std::erase_if(open, [&](size_t i) { return !ids[i].empty(); });
    // c. The only vanished thing in the same folder with the same extension, and the only new one there (renamed).
    {
        std::vector<std::string> place(n);
        std::unordered_map<std::string, int> rivals;
        for (const size_t i : open) {
            place[i] = std::string(ParentOf(found[i].path)) + '\0' + Extension(found[i].path);
            ++rivals[place[i]];
        }
        for (const size_t i : open) {
            if (rivals[place[i]] != 1) continue;
            const auto c = unusedByPlace.find(place[i]);
            if (c == unusedByPlace.end() || c->second != 1) continue;
            if (Gone* g = unusedIn(goneByPlace[place[i]])) give(i, *g, "名前の変更");
        }
    }

    // 3. Everything else: a new id (copies keep their other .meta keys - the write keeps them). A copy
    //    whose .meta is broken cannot be given its own id without losing what it holds: left out.
    for (size_t i = 0; i < n; ++i) {
        if (!ids[i].empty() || skipped[i]) continue;
        if (copied[i] && found[i].meta == MetaState::Damaged) {
            skipped[i] = true;
            plan.notes.push_back(std::format("{}: ほかのファイルと同じ ID で、.meta が壊れていて書き直せないので、一覧から外しています。.meta を直すか消してください", found[i].path));
            continue;
        }
        std::string guid = newGuid();
        while (!IsGuid(guid) || taken.count(guid)) guid = newGuid();
        ids[i] = guid;
        taken.insert(guid);
        plan.writes.push_back({found[i].path, guid, std::string(FileNameOf(found[i].path))});
        if (copied[i]) plan.notes.push_back(std::format("{}: ほかのファイルと同じ ID だったので、新しい ID にしました (.meta ごと写したもの)", found[i].path));
    }

    plan.entries.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!ids[i].empty()) plan.entries.push_back({found[i].path, ids[i], found[i].folder});
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Display order, numbers, index
// ---------------------------------------------------------------------------

std::vector<size_t> DisplayOrder(std::span<const Entry> entries) {
    std::unordered_map<std::string, std::vector<size_t>> children;  // folder path -> items in it
    std::vector<std::string> lower(entries.size());                  // names ignoring case (made once)
    for (size_t i = 0; i < entries.size(); ++i) {
        children[std::string(ParentOf(entries[i].path))].push_back(i);
        lower[i] = LowerText(FileNameOf(entries[i].path));
    }
    for (auto& [folder, list] : children) {
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) {
            if (entries[a].folder != entries[b].folder) return entries[a].folder;
            if (lower[a] != lower[b]) return lower[a] < lower[b];
            return entries[a].path < entries[b].path;
        });
    }
    std::vector<size_t> out;
    out.reserve(entries.size());
    std::vector<bool> placed(entries.size(), false);
    auto walk = [&](auto&& self, std::string_view folder, int depth) -> void {
        const auto it = children.find(std::string(folder));
        if (it == children.end() || depth > 256) return;
        for (const size_t i : it->second) {
            if (placed[i]) continue;
            placed[i] = true;
            out.push_back(i);
            if (entries[i].folder) self(self, entries[i].path, depth + 1);
        }
    };
    walk(walk, "", 0);
    // Items whose folder is not in the list (should not happen): at the end, in path order.
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!placed[i]) out.push_back(i);
    }
    return out;
}

uint64_t NumberOf(std::string_view guid) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (const char c : guid) {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h == 0 ? 1 : h;
}

void Index::Set(std::vector<Entry> entries) {
    entries_ = std::move(entries);
    byGuid_.clear();
    byPath_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        byGuid_.emplace(entries_[i].guid, i);
        byPath_.emplace(entries_[i].path, i);
    }
}

const Entry* Index::ByGuid(std::string_view guid) const {
    const auto it = byGuid_.find(std::string(guid));
    return it != byGuid_.end() ? &entries_[it->second] : nullptr;
}

const Entry* Index::ByPath(std::string_view path) const {
    const auto it = byPath_.find(std::string(path));
    return it != byPath_.end() ? &entries_[it->second] : nullptr;
}

}  // namespace rm::asset
