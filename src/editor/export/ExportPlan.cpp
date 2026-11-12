#include "editor/export/ExportPlan.h"

#include "base/FileName.h"
#include "editor/lang/Lang.h"
#include "editor/project/Templates.h"

#include <algorithm>
#include <format>

namespace rm::editor::exporter {

namespace {

// A C string literal of UTF-8 text (every byte outside plain ASCII written as \x.., split so the
// next character is never taken into the escape).
std::string CString(std::string_view s) {
    std::string out = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c >= 0x20 && c < 0x7f) {
            out += static_cast<char>(c);
        } else {
            out += std::format("\\x{:02x}\" \"", c);
        }
    }
    return out + "\"";
}

}  // namespace

std::string ExeNameFrom(std::string_view gameName) {
    std::string s;
    for (const char c : gameName) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) continue;
        s += c;
    }
    auto trim = [](std::string& t) {
        while (!t.empty() && (t.back() == ' ' || t.back() == '.')) t.pop_back();
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    };
    trim(s);
    // At most 64 characters (UTF-16 units): cut at a character's start.
    while (ValidateFileName(s) == FileNameError::TooLong) {
        size_t cut = s.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
        trim(s);
    }
    if (s.empty() || ValidateFileName(s) != FileNameError::None) return "Game";
    return s;
}

std::string ExeNameProblem(std::string_view name) {
    switch (ValidateFileName(name)) {
        case FileNameError::None: return {};
        case FileNameError::Empty: return TrU8("exe の名前を入れてください。");
        case FileNameError::TooLong: return TrU8("exe の名前が長すぎます (64 文字まで)。");
        case FileNameError::InvalidChar: return TrU8("exe の名前に使えない文字が入っています ( < > : \" / \\ | ? * )。");
        case FileNameError::ReservedName: return TrU8("Windows が予約している名前なので使えません (CON / NUL / COM1 など)。");
        case FileNameError::LeadingSpace: return TrU8("先頭に空白は使えません。");
        case FileNameError::TrailingDotOrSpace: return TrU8("末尾にピリオドや空白は使えません。");
    }
    return {};
}

std::vector<PluginId> ExportPlugins(const std::vector<PluginId>& recorded, const std::vector<PluginManifest>& available,
                                    std::vector<PluginId>* missing) {
    // The same as installing them on a PC that has nothing yet: requirements first, connectors with them.
    return TemplateInstallPlan(recorded, available, {}, missing);
}

std::string RuntimeEntryName(const PluginId& id) { return "rm_runtime__" + id.author + "__" + id.name; }

std::string GameMainSource(const std::vector<GameLib>& libs, std::string_view titleUtf8, uint64_t build) {
    std::string s;
    s += "// RM Engine が書き出しのときに作りました (書き出しのたびに作り直します)。手で直さないでください。\n";
    s += "// このゲームに入れたプラグインを、前提が先の順に並べるだけのファイルです。名前を書いたものだけが\n";
    s += "// 静的ライブラリからリンクされます (登録が黙って捨てられないように)。\n\n";
    s += "#include <span>\n\n";
    s += "namespace rm { class RuntimeRegistry; }\n";
    s += "namespace rm::reflect { struct TypeDesc; struct EnumDesc; }\n\n";
    // The same declarations as src/game/Player.h.
    s += "extern \"C\" {\n";
    s += "struct RmLinkedPlugin { const char* author; const char* name; void (*registerRuntime)(rm::RuntimeRegistry&); };\n";
    s += "struct RmGameImage { const char* title; unsigned long long build; const RmLinkedPlugin* plugins; unsigned long long count; };\n";
    s += "const RmGameImage* rm_game_image();\n";
    for (const GameLib& lib : libs) {
        if (lib.plugin && lib.registers) s += "void " + RuntimeEntryName(*lib.plugin) + "(rm::RuntimeRegistry&);\n";
    }
    s += "}\n\n";
    s += "namespace rmgen_generated {\n";
    for (const GameLib& lib : libs) {
        for (const std::string& t : lib.types) s += "extern const ::rm::reflect::TypeDesc " + t + ";\n";
        for (const std::string& e : lib.enums) s += "extern const ::rm::reflect::EnumDesc " + e + ";\n";
    }
    s += "}  // namespace rmgen_generated\n\n";
    s += "namespace rm::reflect {\n";
    auto list = [&](const char* fn, const char* type, bool types) {
        s += std::format("std::span<const {}* const> {}() {{\n", type, fn);
        std::string items;
        for (const GameLib& lib : libs) {
            for (const std::string& sym : types ? lib.types : lib.enums) items += "        &rmgen_generated::" + sym + ",\n";
        }
        if (items.empty()) {
            s += "    return {};\n}\n";
            return;
        }
        s += std::format("    static const {}* const kItems[] = {{\n", type) + items + "    };\n    return kItems;\n}\n";
    };
    list("AllTypes", "TypeDesc", true);
    list("AllEnums", "EnumDesc", false);
    s += "}  // namespace rm::reflect\n\n";
    s += "namespace {\n";
    s += "const RmLinkedPlugin kPlugins[] = {\n";
    size_t count = 0;
    for (const GameLib& lib : libs) {
        if (!lib.plugin) continue;
        s += "    {" + CString(lib.plugin->author) + ", " + CString(lib.plugin->name) + ", " +
             (lib.registers ? "&" + RuntimeEntryName(*lib.plugin) : std::string("nullptr")) + "},\n";
        ++count;
    }
    if (count == 0) s += "    {nullptr, nullptr, nullptr},\n";
    s += "};\n";
    s += std::format("const RmGameImage kImage{{{}, {}ull, kPlugins, {}}};\n", CString(titleUtf8), build, count);
    s += "}  // namespace\n\n";
    s += "extern \"C\" const RmGameImage* rm_game_image() { return &kImage; }\n";
    return s;
}

void ReadSymbols(std::string_view text, std::vector<std::string>& types, std::vector<std::string>& enums) {
    for (const auto& [prefix, out] : {std::pair<std::string_view, std::vector<std::string>*>{"extern const ::rm::reflect::TypeDesc ", &types},
                                      std::pair<std::string_view, std::vector<std::string>*>{"extern const ::rm::reflect::EnumDesc ", &enums}}) {
        for (size_t at = text.find(prefix); at != std::string_view::npos; at = text.find(prefix, at + 1)) {
            const size_t start = at + prefix.size();
            size_t end = start;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) ++end;
            // A definition (" = {"), not a declaration (";").
            size_t next = end;
            while (next < text.size() && text[next] == ' ') ++next;
            if (end > start && next < text.size() && text[next] == '=') {
                std::string sym(text.substr(start, end - start));
                if (std::find(out->begin(), out->end(), sym) == out->end()) out->push_back(std::move(sym));
            }
        }
    }
}

bool PortablePath(std::string_view p) {
    if (p.empty() || p.front() == '/' || p.front() == '\\' || p.find(':') != std::string_view::npos) return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = p.size();
        const std::string_view part = p.substr(start, end - start);
        if (part == ".." || part.empty()) return false;
        start = end + 1;
    }
    return true;
}

std::vector<std::string> AbsolutePathsIn(const json::Value& v) {
    std::vector<std::string> out;
    auto visit = [&](auto&& self, const json::Value& x) -> void {
        if (x.IsString()) {
            const std::string& s = x.AsString();
            const bool drive = s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && (s[2] == '\\' || s[2] == '/');
            const bool unc = s.size() >= 2 && s[0] == '\\' && s[1] == '\\';
            if (drive || unc) out.push_back(s);
        } else if (x.IsArray()) {
            for (const auto& i : x.Items()) self(self, i);
        } else if (x.IsObject()) {
            for (const auto& [k, i] : x.Members()) self(self, i);
        }
    };
    visit(visit, v);
    return out;
}

}  // namespace rm::editor::exporter
