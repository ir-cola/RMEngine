#include "core/asset/AssetIndex.h"

#include "base/Json.h"

#include <algorithm>
#include <format>
#include <map>
#include <set>

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
        {".png", Kind::Image},  {".jpg", Kind::Image},  {".jpeg", Kind::Image}, {".bmp", Kind::Image},  {".tga", Kind::Image},
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
    return lower.size() > kMetaSuffix.size() && lower.ends_with(kMetaSuffix);
}

std::optional<std::string> GuidInMeta(std::string_view text) {
    const auto v = json::Parse(text);
    if (!v || !v->IsObject()) return std::nullopt;
    const json::Value& g = v->Get("guid");
    if (!g.IsString() || !IsGuid(g.AsString())) return std::nullopt;
    return g.AsString();
}

std::string MetaText(std::string_view old, const std::string& guid) {
    json::Value doc = json::Value::MakeObject();
    if (auto v = json::Parse(old); v && v->IsObject()) doc = std::move(*v);  // the other keys stay
    doc.Set("guid", guid);
    return json::Write(doc) + "\n";
}

// ---------------------------------------------------------------------------
// Reconcile
// ---------------------------------------------------------------------------

Plan Reconcile(std::span<const Entry> previous, std::span<const Found> found, std::span<const Orphan> orphans,
               const std::function<std::string()>& newGuid) {
    Plan plan;
    const size_t n = found.size();
    std::vector<std::string> ids(n);
    std::set<std::string, std::less<>> taken;
    std::map<std::string, std::string, std::less<>> previousPath;  // guid -> where it was
    for (const Entry& e : previous) previousPath[e.guid] = e.path;

    // 1. Items with their own .meta. A guid claimed twice (a copy made with its .meta) stays with the
    //    one at the guid's old path, else the first; the others get new ids below.
    std::map<std::string, std::vector<size_t>, std::less<>> claims;
    for (size_t i = 0; i < n; ++i) {
        if (IsGuid(found[i].metaGuid)) claims[found[i].metaGuid].push_back(i);
    }
    std::vector<bool> copied(n, false);
    for (const auto& [guid, list] : claims) {
        size_t winner = list.front();
        if (const auto it = previousPath.find(guid); it != previousPath.end()) {
            for (const size_t i : list) {
                if (found[i].path == it->second) winner = i;
            }
        }
        ids[winner] = guid;
        taken.insert(guid);
        for (const size_t i : list) {
            if (i != winner) copied[i] = true;
        }
    }

    // 2. Items without one: the id of something that vanished, when it is clearly the same thing.
    struct Gone {
        std::string path, guid;
        bool hasMeta = false;  // an Orphan (its .meta moves along) / only in `previous` (a .meta is written)
        bool used = false;
    };
    std::vector<Gone> gone;
    for (const Orphan& o : orphans) {
        if (IsGuid(o.guid) && !taken.count(o.guid)) gone.push_back({o.path, o.guid, true});
    }
    for (const Entry& e : previous) {
        if (taken.count(e.guid)) continue;  // (its path may be found: the .meta was deleted - a. below)
        const bool known = std::any_of(gone.begin(), gone.end(), [&](const Gone& g) { return g.guid == e.guid; });
        if (!known) gone.push_back({e.path, e.guid, false});
    }
    // The same guid may be left twice (two .meta copies): use it once.
    auto give = [&](size_t i, Gone& g, const char* why) {
        ids[i] = g.guid;
        taken.insert(g.guid);
        for (Gone& other : gone) {
            if (other.guid == g.guid) other.used = true;
        }
        if (g.hasMeta) {
            plan.moves.push_back({g.path, found[i].path});
        } else {
            plan.writes.push_back({found[i].path, g.guid});
        }
        if (g.path != found[i].path) plan.notes.push_back(std::format("{} → {} ({}。ID はそのまま)", g.path, found[i].path, why));
    };
    std::vector<size_t> open;  // items still without an id
    for (size_t i = 0; i < n; ++i) {
        if (ids[i].empty() && !copied[i]) open.push_back(i);
    }
    // a. Its own old path (the .meta was deleted, the file is where it was).
    for (const size_t i : open) {
        for (Gone& g : gone) {
            if (!g.used && g.path == found[i].path) {
                give(i, g, "同じ場所");
                break;
            }
        }
    }
    std::erase_if(open, [&](size_t i) { return !ids[i].empty(); });
    // b. The only vanished thing with the same name, and the only new one with it (moved).
    for (const size_t i : open) {
        const std::string_view name = FileNameOf(found[i].path);
        Gone* match = nullptr;
        int count = 0;
        for (Gone& g : gone) {
            if (!g.used && FileNameOf(g.path) == name) {
                match = &g;
                ++count;
            }
        }
        const auto rivals = std::count_if(open.begin(), open.end(), [&](size_t k) { return ids[k].empty() && FileNameOf(found[k].path) == name; });
        if (count == 1 && rivals == 1) give(i, *match, "移動");
    }
    std::erase_if(open, [&](size_t i) { return !ids[i].empty(); });
    // c. The only vanished thing in the same folder with the same extension, and the only new one there (renamed).
    for (const size_t i : open) {
        const std::string_view folder = ParentOf(found[i].path);
        const std::string ext = Extension(found[i].path);
        auto same = [&](std::string_view p) { return ParentOf(p) == folder && Extension(p) == ext; };
        Gone* match = nullptr;
        int count = 0;
        for (Gone& g : gone) {
            if (!g.used && same(g.path)) {
                match = &g;
                ++count;
            }
        }
        const auto rivals = std::count_if(open.begin(), open.end(), [&](size_t k) { return ids[k].empty() && same(found[k].path); });
        if (count == 1 && rivals == 1) give(i, *match, "名前の変更");
    }

    // 3. Everything else: a new id (copies keep their other .meta keys - the write keeps them).
    for (size_t i = 0; i < n; ++i) {
        if (!ids[i].empty()) continue;
        std::string guid = newGuid();
        while (!IsGuid(guid) || taken.count(guid)) guid = newGuid();
        ids[i] = guid;
        taken.insert(guid);
        plan.writes.push_back({found[i].path, guid});
        if (copied[i]) plan.notes.push_back(std::format("{}: ほかのファイルと同じ ID だったので、新しい ID にしました (.meta ごと写したもの)", found[i].path));
    }

    plan.entries.reserve(n);
    for (size_t i = 0; i < n; ++i) plan.entries.push_back({found[i].path, ids[i], found[i].folder});
    return plan;
}

// ---------------------------------------------------------------------------
// Display order, numbers, index
// ---------------------------------------------------------------------------

std::vector<size_t> DisplayOrder(std::span<const Entry> entries) {
    std::map<std::string, std::vector<size_t>, std::less<>> children;  // folder path -> items in it
    for (size_t i = 0; i < entries.size(); ++i) children[std::string(ParentOf(entries[i].path))].push_back(i);
    for (auto& [folder, list] : children) {
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) {
            if (entries[a].folder != entries[b].folder) return entries[a].folder;
            const std::string la = LowerText(FileNameOf(entries[a].path)), lb = LowerText(FileNameOf(entries[b].path));
            if (la != lb) return la < lb;
            return entries[a].path < entries[b].path;
        });
    }
    std::vector<size_t> out;
    out.reserve(entries.size());
    std::vector<bool> placed(entries.size(), false);
    auto walk = [&](auto&& self, std::string_view folder, int depth) -> void {
        const auto it = children.find(folder);
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
