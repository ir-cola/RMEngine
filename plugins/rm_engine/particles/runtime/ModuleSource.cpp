#include "rm_engine/particles/runtime/ModuleSource.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <regex>

namespace rm::particles {

namespace {

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// コメント (行末までの // と /* ... */) を除いたコード: 本当に書いてあるもの。コメントにだけ出てくる
// 関数は無いものとする (2026-11-18 画面で: 新しいモジュールの "// void Spawn(...) { }" を Spawn と
// 取り違え、無い関数をエンジンが呼んでしまった)。
std::string WithoutComments(std::string_view code) {
    std::string out;
    out.reserve(code.size());
    for (size_t i = 0; i < code.size();) {
        if (code.compare(i, 2, "//") == 0) {
            i = code.find('\n', i);
            if (i == std::string_view::npos) break;
        } else if (code.compare(i, 2, "/*") == 0) {
            const size_t end = code.find("*/", i + 2);
            if (end == std::string_view::npos) break;
            out.push_back(' ');
            i = end + 2;
        } else {
            out.push_back(code[i++]);
        }
    }
    return out;
}

bool StartsWithWord(std::string_view s, std::string_view word) {
    return s.size() > word.size() && s.substr(0, word.size()) == word && (s[word.size()] == ' ' || s[word.size()] == '\t');
}

bool IsName(std::string_view s) {
    if (s.empty() || (s[0] >= '0' && s[0] <= '9')) return false;
    for (const char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool ParseFloat(std::string_view s, float& out) {
    s = Trim(s);
    if (s == "true" || s == "false") {
        out = s == "true" ? 1.f : 0.f;
        return true;
    }
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    if (!s.empty() && (s.back() == 'f' || s.back() == 'F')) s.remove_suffix(1);
    if (s.empty()) return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

// "1, 2, 3" / "(1, 2, 3)" / "float3(1, 2, 3)" / "{1, 2, 3}" -> 数値。
bool ParseValues(std::string_view s, std::vector<float>& out) {
    s = Trim(s);
    if (const size_t open = s.find_first_of("({"); open != std::string_view::npos) {
        const char close = s[open] == '(' ? ')' : '}';
        if (s.empty() || s.back() != close) return false;
        s = s.substr(open + 1, s.size() - open - 2);
    }
    out.clear();
    while (true) {
        const size_t comma = s.find(',');
        float v = 0;
        if (!ParseFloat(s.substr(0, comma), v)) return false;
        out.push_back(v);
        if (comma == std::string_view::npos) break;
        s.remove_prefix(comma + 1);
    }
    return true;
}

bool TypeOf(std::string_view t, ParamType& out) {
    static constexpr std::pair<std::string_view, ParamType> kTypes[] = {
        {"float", ParamType::Float}, {"float2", ParamType::Float2}, {"float3", ParamType::Float3}, {"float4", ParamType::Float4},
        {"color", ParamType::Color}, {"int", ParamType::Int},       {"bool", ParamType::Bool},
    };
    for (const auto& [name, type] : kTypes) {
        if (t == name) {
            out = type;
            return true;
        }
    }
    return false;
}

}  // namespace

int Components(ParamType type) {
    switch (type) {
        case ParamType::Float2: return 2;
        case ParamType::Float3: return 3;
        case ParamType::Float4:
        case ParamType::Color: return 4;
        default: return 1;
    }
}

std::string Message(const ModuleError& e) {
    std::string where = e.line > 0 ? std::format("{} 行目: ", e.line) : std::string();
    switch (e.kind) {
        case ModuleErrorKind::BadLine: return where + "この行を読めません (param 型 名前 = 値; / attr 型 名前; / module \"名前\")";
        case ModuleErrorKind::UnknownType: return where + std::format("「{}」という型はありません", e.detail);
        case ModuleErrorKind::BadName: return where + std::format("「{}」は名前に使えません (英字・数字・_ で、数字から始めない)", e.detail);
        case ModuleErrorKind::SameName: return where + std::format("「{}」が 2 つあります", e.detail);
        case ModuleErrorKind::BadValue: return where + std::format("値「{}」を読めません", e.detail);
        case ModuleErrorKind::NoFunction: return where + "Spawn も Update もありません (どちらかを書きます)";
    }
    return where;
}

ModuleSource ParseModule(std::string_view text) {
    ModuleSource m;
    int lineNo = 0;
    auto error = [&](ModuleErrorKind kind, std::string detail = {}) { m.errors.push_back({lineNo, kind, std::move(detail)}); };
    auto taken = [&](std::string_view name) {
        for (const auto& p : m.params) {
            if (p.name == name) return true;
        }
        for (const auto& a : m.attrs) {
            if (a.name == name) return true;
        }
        return false;
    };
    size_t at = 0;
    while (at <= text.size()) {
        const size_t end = text.find('\n', at);
        const std::string_view raw = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        ++lineNo;
        const std::string_view line = Trim(raw);
        bool ours = true;
        if (StartsWithWord(line, "module")) {
            const std::string_view rest = Trim(line.substr(6));
            if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') m.title = std::string(rest.substr(1, rest.size() - 2));
            else error(ModuleErrorKind::BadLine);
        } else if (StartsWithWord(line, "param")) {
            std::string_view s = line.substr(5);
            ModuleParam p;
            p.line = lineNo;
            if (const size_t slash = s.find("//"); slash != std::string_view::npos) {
                p.label = std::string(Trim(s.substr(slash + 2)));
                s = s.substr(0, slash);
            }
            s = Trim(s);
            if (s.empty() || s.back() != ';') {
                error(ModuleErrorKind::BadLine);
            } else {
                s = Trim(s.substr(0, s.size() - 1));
                bool ok = true;
                if (!s.empty() && s.back() == ']') {
                    const size_t open = s.rfind('[');
                    std::vector<float> range;
                    if (open == std::string_view::npos || !ParseValues(s.substr(open + 1, s.size() - open - 2), range) || range.size() != 2) {
                        error(ModuleErrorKind::BadValue, std::string(s.substr(open == std::string_view::npos ? 0 : open)));
                        ok = false;
                    } else {
                        p.hasRange = true;
                        p.min = range[0];
                        p.max = range[1];
                        s = Trim(s.substr(0, open));
                    }
                }
                std::string_view decl = s, value;
                if (const size_t eq = s.find('='); eq != std::string_view::npos) {
                    decl = Trim(s.substr(0, eq));
                    value = Trim(s.substr(eq + 1));
                }
                const size_t space = decl.find_first_of(" \t");
                const std::string_view type = decl.substr(0, space);
                const std::string_view name = space == std::string_view::npos ? std::string_view() : Trim(decl.substr(space));
                if (ok && !TypeOf(type, p.type)) {
                    error(ModuleErrorKind::UnknownType, std::string(type));
                    ok = false;
                }
                if (ok && !IsName(name)) {
                    error(ModuleErrorKind::BadName, std::string(name));
                    ok = false;
                }
                if (ok && taken(name)) {
                    error(ModuleErrorKind::SameName, std::string(name));
                    ok = false;
                }
                const int n = Components(p.type);
                if (p.type == ParamType::Color) p.value = {1, 1, 1, 1};
                if (ok && !value.empty()) {
                    std::vector<float> v;
                    if (!ParseValues(value, v) || (v.size() != 1 && static_cast<int>(v.size()) != n)) {
                        error(ModuleErrorKind::BadValue, std::string(value));
                        ok = false;
                    } else {
                        for (int i = 0; i < n; ++i) p.value[static_cast<size_t>(i)] = v.size() == 1 ? v[0] : v[static_cast<size_t>(i)];
                    }
                }
                if (ok) {
                    p.name = std::string(name);
                    m.params.push_back(std::move(p));
                }
            }
        } else if (StartsWithWord(line, "attr")) {
            std::string_view s = line.substr(4);
            if (const size_t slash = s.find("//"); slash != std::string_view::npos) s = s.substr(0, slash);
            s = Trim(s);
            if (s.empty() || s.back() != ';') {
                error(ModuleErrorKind::BadLine);
            } else {
                s = Trim(s.substr(0, s.size() - 1));
                const size_t space = s.find_first_of(" \t");
                const std::string_view type = s.substr(0, space);
                const std::string_view name = space == std::string_view::npos ? std::string_view() : Trim(s.substr(space));
                static constexpr std::string_view kTypes[] = {"float", "float2", "float3", "float4", "int", "uint"};
                if (std::find(std::begin(kTypes), std::end(kTypes), type) == std::end(kTypes)) error(ModuleErrorKind::UnknownType, std::string(type));
                else if (!IsName(name)) error(ModuleErrorKind::BadName, std::string(name));
                else if (taken(name)) error(ModuleErrorKind::SameName, std::string(name));
                else m.attrs.push_back({std::string(name), std::string(type), lineNo});
            }
        } else {
            ours = false;
        }
        if (!ours) m.code.append(raw);
        if (end == std::string_view::npos) break;
        m.code.push_back('\n');
        at = end + 1;
    }
    static const std::regex kSpawn(R"(\bvoid\s+Spawn\s*\()"), kUpdate(R"(\bvoid\s+Update\s*\()");
    const std::string written = WithoutComments(m.code);
    m.hasSpawn = std::regex_search(written, kSpawn);
    m.hasUpdate = std::regex_search(written, kUpdate);
    if (!m.hasSpawn && !m.hasUpdate) {
        lineNo = 0;
        error(ModuleErrorKind::NoFunction);
    }
    return m;
}

const ModuleParam* FindParam(const ModuleSource& m, std::string_view name) {
    for (const auto& p : m.params) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

}  // namespace rm::particles
