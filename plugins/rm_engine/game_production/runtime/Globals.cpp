#include "rm_engine/game_production/runtime/Globals.h"

#include "base/Utf.h"
#include "platform/Shell.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <system_error>

namespace rm::game_production {

namespace fs = std::filesystem;

namespace {

int Components(GlobalType t) { return t == GlobalType::Vec3 ? 3 : t == GlobalType::Vec2 ? 2 : 1; }

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

std::optional<double> Number(std::string_view s) {
    s = Trim(s);
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    double d = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), d);
    if (ec != std::errc() || end != s.data() + s.size() || !std::isfinite(d)) return std::nullopt;
    return d;
}

double WholeInt(double d) { return std::clamp(std::round(d), static_cast<double>(std::numeric_limits<int32_t>::min()), static_cast<double>(std::numeric_limits<int32_t>::max())); }

// Fits a number to the type (a bool 0 / 1, an int whole).
double Fit(GlobalType t, double d) {
    if (t == GlobalType::Bool) return d != 0 ? 1 : 0;
    if (t == GlobalType::Int) return WholeInt(d);
    return d;
}

std::string Num(double d) { return std::format("{}", d); }

constexpr std::string_view kKeywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t",
    "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue",
    "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
    "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
    "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
    "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"};

}  // namespace

std::optional<GlobalValue> ParseGlobalValue(GlobalType type, std::string_view text) {
    GlobalValue v;
    v.type = type;
    if (type == GlobalType::String) {
        v.s = std::string(text);
        return v;
    }
    const std::string_view t = Trim(text);
    if (t.empty()) return v;
    if (type == GlobalType::Bool) {
        if (t == "true" || t == "1") {
            v.v[0] = 1;
            return v;
        }
        if (t == "false" || t == "0") return v;
        return std::nullopt;
    }
    // Numbers: one, or 2 / 3 split by , (or spaces). Fewer than the type has: the rest 0.
    std::vector<std::string_view> parts;
    size_t at = 0;
    while (at <= t.size()) {
        size_t next = t.find_first_of(", \t", at);
        if (next == std::string_view::npos) next = t.size();
        if (const std::string_view p = t.substr(at, next - at); !p.empty()) parts.push_back(p);
        at = next + 1;
    }
    if (parts.empty() || static_cast<int>(parts.size()) > Components(type)) return std::nullopt;
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto d = Number(parts[i]);
        if (!d) return std::nullopt;
        v.v[i] = Fit(type, *d);
    }
    return v;
}

std::string FormatGlobalValue(const GlobalValue& v) {
    switch (v.type) {
        case GlobalType::Bool: return v.v[0] != 0 ? "true" : "false";
        case GlobalType::String: return v.s;
        case GlobalType::Vec2: return Num(v.v[0]) + ", " + Num(v.v[1]);
        case GlobalType::Vec3: return Num(v.v[0]) + ", " + Num(v.v[1]) + ", " + Num(v.v[2]);
        default: return Num(v.v[0]);
    }
}

json::Value GlobalToJson(const GlobalValue& v) {
    switch (v.type) {
        case GlobalType::Bool: return json::Value(v.v[0] != 0);
        case GlobalType::Int: return json::Value(static_cast<int64_t>(v.v[0]));
        case GlobalType::Float: return json::Value(v.v[0]);
        case GlobalType::String: return json::Value(v.s);
        default: {
            json::Value a = json::Value::MakeArray();
            for (int i = 0; i < Components(v.type); ++i) a.Push(v.v[i]);
            return a;
        }
    }
}

std::optional<GlobalValue> GlobalFromJson(GlobalType type, const json::Value& j) {
    GlobalValue v;
    v.type = type;
    if (type == GlobalType::String) {
        if (!j.IsString()) return std::nullopt;
        v.s = j.AsString();
        return v;
    }
    if (type == GlobalType::Bool && j.IsBool()) {
        v.v[0] = j.AsBool() ? 1 : 0;
        return v;
    }
    if (j.IsNumber()) {
        v.v[0] = Fit(type, j.AsDouble());
        return v;
    }
    if (j.IsArray() && j.Size() >= 1 && static_cast<int>(j.Size()) <= Components(type)) {
        for (size_t i = 0; i < j.Size(); ++i) {
            if (!j.At(i).IsNumber()) return std::nullopt;
            v.v[i] = Fit(type, j.At(i).AsDouble());
        }
        return v;
    }
    return std::nullopt;
}

bool IsGlobalName(std::string_view name) {
    if (name.empty() || (name[0] >= '0' && name[0] <= '9')) return false;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return std::find(std::begin(kKeywords), std::end(kKeywords), name) == std::end(kKeywords);
}

void GlobalStore::Reset(std::span<const GlobalVariable> list, std::vector<std::string>* warnings) {
    values_.clear();
    lastRead = json::Value();
    for (const GlobalVariable& g : list) {
        if (g.name.empty() || Find(g.name)) continue;
        auto v = ParseGlobalValue(g.type, g.initial);
        if (!v) {
            if (warnings) warnings->push_back(std::format("グローバル変数「{}」の初めの値「{}」は型に合わないので 0 にします", g.name, g.initial));
            v = GlobalValue{};
            v->type = g.type;
        }
        values_.emplace_back(g.name, std::move(*v));
    }
}

GlobalValue* GlobalStore::Find(std::string_view name) {
    for (auto& [n, v] : values_) {
        if (n == name) return &v;
    }
    return nullptr;
}

const GlobalValue* GlobalStore::Find(std::string_view name) const { return const_cast<GlobalStore*>(this)->Find(name); }

json::Value GlobalStore::ToSave(json::Value base) const {
    if (!base.IsObject()) base = json::Value::MakeObject();
    base.Set("kind", json::Value(kSaveKind));
    base.Set("format", json::Value(kSaveFormat));
    json::Value globals = base.Get("globals").IsObject() ? base.Get("globals") : json::Value::MakeObject();
    for (const auto& [name, v] : values_) globals.Set(name, GlobalToJson(v));
    base.Set("globals", std::move(globals));
    return base;
}

bool GlobalStore::FromSave(const json::Value& doc, std::vector<std::string>* warnings) {
    if (!doc.IsObject() || doc.Get("kind").AsString() != kSaveKind) return false;
    if (doc.Get("format").AsInt(0) > kSaveFormat && warnings) warnings->push_back("新しい版のエンジンで書いたセーブです (読める所だけ読みます)");
    for (auto& [name, v] : values_) {
        const json::Value* j = doc.Get("globals").Find(name);
        if (!j) continue;  // (not in the save: as it is now)
        if (auto read = GlobalFromJson(v.type, *j)) {
            v = std::move(*read);
        } else if (warnings) {
            warnings->push_back(std::format("セーブのグローバル変数「{}」は型が違うので読みませんでした", name));
        }
    }
    lastRead = doc;
    return true;
}

bool IsSaveSlot(std::string_view slot) {
    if (slot.empty() || slot.size() > 64) return false;
    return std::all_of(slot.begin(), slot.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

fs::path SaveFile(const fs::path& folder, std::string_view slot) { return folder / Utf8ToWide("save_" + std::string(slot) + ".json"); }

namespace {

bool CheckSlot(const fs::path& folder, std::string_view slot, std::string* why) {
    if (folder.empty()) {
        if (why) *why = "セーブの場所が決まっていません";
        return false;
    }
    if (!IsSaveSlot(slot)) {
        if (why) *why = std::format("セーブの名前「{}」は使えません (英字・数字・_・- で 64 文字まで)", slot);
        return false;
    }
    return true;
}

std::optional<json::Value> ReadDoc(const fs::path& file, std::string* why) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        if (why) *why = "そのセーブはありません";
        return std::nullopt;
    }
    const auto bytes = platform::ReadFileBytes(file, why);
    if (!bytes) return std::nullopt;
    auto doc = json::Parse(*bytes);
    if (!doc) {
        if (why) *why = "セーブのファイルが壊れています";
        return std::nullopt;
    }
    return doc;
}

}  // namespace

bool WriteSave(GlobalStore& store, const fs::path& folder, std::string_view slot, std::string* why) {
    if (!CheckSlot(folder, slot, why)) return false;
    const fs::path file = SaveFile(folder, slot);
    std::error_code ec;
    fs::create_directories(folder, ec);
    // From the file there (its keys kept); a new one from the last read (its unknown keys go along).
    json::Value base = store.lastRead;
    if (fs::exists(file, ec)) {
        std::string ignored;
        if (auto doc = ReadDoc(file, &ignored); doc && doc->IsObject()) base = std::move(*doc);
    }
    return platform::WriteFileAtomic(file, json::Write(store.ToSave(std::move(base))), why);
}

bool ReadSave(GlobalStore& store, const fs::path& folder, std::string_view slot, std::vector<std::string>* warnings, std::string* why) {
    if (!CheckSlot(folder, slot, why)) return false;
    auto doc = ReadDoc(SaveFile(folder, slot), why);
    if (!doc) return false;
    if (!store.FromSave(*doc, warnings)) {
        if (why) *why = "セーブのファイルではありません";
        return false;
    }
    return true;
}

bool SaveExists(const fs::path& folder, std::string_view slot) {
    std::error_code ec;
    return !folder.empty() && IsSaveSlot(slot) && fs::is_regular_file(SaveFile(folder, slot), ec);
}

bool DeleteSave(const fs::path& folder, std::string_view slot, std::string* why) {
    if (!CheckSlot(folder, slot, why)) return false;
    std::error_code ec;
    fs::remove(SaveFile(folder, slot), ec);
    if (ec) {
        if (why) *why = ec.message();
        return false;
    }
    return true;
}

fs::path ProjectSaveFolder(const fs::path& project) { return project / L"Library" / L"Saves"; }

fs::path GameSaveFolder(std::wstring_view exeName) {
    return platform::GetKnownFolder(platform::KnownFolder::LocalAppData) / std::wstring(exeName) / L"Saves";
}

}  // namespace rm::game_production
