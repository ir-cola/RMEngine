#include "editor/dock/NamedLayouts.h"

#include <algorithm>
#include <format>

namespace rm::editor::dock {

namespace {

constexpr char kLayoutKey[] = "layout";

bool IsSpaceAt(std::string_view s, size_t i, size_t* len) {
    if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
        *len = 1;
        return true;
    }
    if (s.substr(i, 3) == "\xE3\x80\x80") {  // U+3000 (full-width space)
        *len = 3;
        return true;
    }
    return false;
}

}  // namespace

std::string NamedLayouts::CleanName(std::string_view name) {
    size_t len = 0;
    while (!name.empty() && IsSpaceAt(name, 0, &len)) name.remove_prefix(len);
    for (bool trimmed = true; trimmed && !name.empty();) {
        trimmed = false;
        for (size_t n : {size_t{1}, size_t{3}}) {
            if (name.size() >= n && IsSpaceAt(name, name.size() - n, &len) && len == n) {
                name.remove_suffix(n);
                trimmed = true;
                break;
            }
        }
    }
    return std::string(name);
}

const NamedLayouts::Entry* NamedLayouts::Find(std::string_view name) const {
    const std::string clean = CleanName(name);
    for (const auto& e : entries_) {
        if (e.info.name == clean) return &e;
    }
    return nullptr;
}

NamedLayouts::NameCheck NamedLayouts::Check(std::string_view name, std::string_view self) const {
    const std::string clean = CleanName(name);
    if (clean.empty()) return NameCheck::Empty;
    if (Find(clean) && clean != CleanName(self)) return NameCheck::Taken;
    return NameCheck::Ok;
}

std::string NamedLayouts::FreeName(std::string_view base) const {
    std::string name = CleanName(base);
    for (int i = 2; Find(name); ++i) name = std::format("{} {}", CleanName(base), i);
    return name;
}

bool NamedLayouts::Put(std::string_view name, const Layout& layout) {
    const std::string clean = CleanName(name);
    if (clean.empty()) return false;
    for (auto& e : entries_) {
        if (e.info.name != clean) continue;
        e.layout = layout.ToJson();
        return true;
    }
    Entry e;
    e.info.name = clean;
    e.layout = layout.ToJson();
    entries_.push_back(std::move(e));
    return true;
}

bool NamedLayouts::Rename(std::string_view from, std::string_view to) {
    const std::string src = CleanName(from);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.info.name == src; });
    if (it == entries_.end() || Check(to, src) != NameCheck::Ok) return false;
    it->info.name = CleanName(to);
    return true;
}

bool NamedLayouts::Remove(std::string_view name) {
    const std::string clean = CleanName(name);
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.info.name == clean; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

bool NamedLayouts::Move(NamedLayouts& from, std::string_view name, NamedLayouts& to) {
    const std::string clean = CleanName(name);
    const auto it = std::find_if(from.entries_.begin(), from.entries_.end(), [&](const Entry& e) { return e.info.name == clean; });
    if (it == from.entries_.end() || &from == &to || to.Find(clean)) return false;
    to.entries_.push_back(std::move(*it));
    from.entries_.erase(it);
    return true;
}

std::optional<Layout> NamedLayouts::Get(std::string_view name, const std::vector<std::string>& knownPanels) const {
    const Entry* e = Find(name);
    if (!e) return std::nullopt;
    std::optional<Layout> l = Layout::FromJson(e->layout);
    if (l) l->Sanitize(knownPanels);
    return l;
}

json::Value NamedLayouts::ToJson() const {
    json::Value list = json::Value::MakeArray();
    for (const auto& e : entries_) {
        json::Value v = reflect::Save(e.info, &e.unknowns);
        v.Set(kLayoutKey, e.layout);
        list.Push(std::move(v));
    }
    for (const auto& o : others_) list.Push(o);
    return list;
}

NamedLayouts NamedLayouts::FromJson(const json::Value& v, reflect::Warnings* warnings) {
    NamedLayouts out;
    for (const auto& item : v.Items()) {
        Entry e;
        if (item.IsObject()) reflect::Load(e.info, item, &e.unknowns, warnings);
        e.info.name = CleanName(e.info.name);
        if (e.info.name.empty() || out.Find(e.info.name)) {
            out.others_.push_back(item);  // not an entry (or a second one of a name): kept, not shown
            continue;
        }
        // "layout" is not a reflected field: it is ours, not an unknown key.
        auto& keys = e.unknowns.keys;
        keys.erase(std::remove_if(keys.begin(), keys.end(), [](const json::Value::Member& m) { return m.first == kLayoutKey; }),
                   keys.end());
        e.layout = item.Get(kLayoutKey);
        out.entries_.push_back(std::move(e));
    }
    return out;
}

}  // namespace rm::editor::dock
