#include "editor/export/ExportPlan.h"

#include "base/FileName.h"
#include "editor/lang/Lang.h"
#include "editor/project/Templates.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>

namespace rm::editor::exporter {

namespace {

// UTF-8 の文章の C 文字列リテラル (ASCII 以外のバイトは全部 \x.. で書き、
// 次の文字がエスケープに取り込まれないよう分ける)。
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
    // 最大 64 文字 (UTF-16 の単位): 文字の先頭で切る。
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
    // まだ何も入っていない PC に入れるのと同じ: 前提が先、コネクタはそれと一緒。
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
    // src/game/Player.h と同じ宣言。
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
            // 定義 (" = {") であって、宣言 (";") ではない。
            size_t next = end;
            while (next < text.size() && text[next] == ' ') ++next;
            if (end > start && next < text.size() && text[next] == '=') {
                std::string sym(text.substr(start, end - start));
                if (std::find(out->begin(), out->end(), sym) == out->end()) out->push_back(std::move(sym));
            }
        }
    }
}

std::string GameResourceSource(std::string_view title, std::string_view exeName, bool icon) {
    // スクリプトの文字列: " は 2 つに、\ も 2 つにする (rc は \ をエスケープの始まりと読む)。
    // ほかは変えない (/c65001 で UTF-8)。
    auto quoted = [](std::string_view s) {
        std::string out = "\"";
        for (const char c : s) {
            if (c == '"') out += '"';
            if (c == '\\') out += '\\';
            if (static_cast<unsigned char>(c) >= 0x20 || c < 0) out += c;
        }
        return out + "\"";
    };
    std::string s = "// RM Engine が書き出しのときに作りました。\n";
    if (icon) s += "1 ICON \"game.ico\"\n";
    s += "1 VERSIONINFO\n";
    s += "FILEVERSION 1,0,0,0\nPRODUCTVERSION 1,0,0,0\nFILEOS 0x40004\nFILETYPE 0x1\n";
    s += "BEGIN\n  BLOCK \"StringFileInfo\"\n  BEGIN\n    BLOCK \"041104B0\"\n    BEGIN\n";
    s += "      VALUE \"FileDescription\", " + quoted(title) + "\n";
    s += "      VALUE \"ProductName\", " + quoted(title) + "\n";
    s += "      VALUE \"OriginalFilename\", " + quoted(std::string(exeName) + ".exe") + "\n";
    s += "      VALUE \"FileVersion\", \"1.0.0.0\"\n      VALUE \"ProductVersion\", \"1.0.0.0\"\n";
    s += "    END\n  END\n  BLOCK \"VarFileInfo\"\n  BEGIN\n    VALUE \"Translation\", 0x411, 1200\n  END\nEND\n";
    return s;
}

std::optional<std::string> IcoFromPng(std::string_view png) {
    static constexpr unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (png.size() < 24 || std::memcmp(png.data(), kSig, 8) != 0 || png.substr(12, 4) != "IHDR") return std::nullopt;
    auto be32 = [&](size_t at) {
        return (static_cast<uint32_t>(static_cast<unsigned char>(png[at])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(png[at + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(png[at + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(png[at + 3]));
    };
    const uint32_t w = be32(16), h = be32(20);
    std::string ico;
    auto put16 = [&](uint32_t v) {
        ico += static_cast<char>(v & 0xff);
        ico += static_cast<char>((v >> 8) & 0xff);
    };
    auto put32 = [&](uint32_t v) {
        put16(v & 0xffff);
        put16(v >> 16);
    };
    put16(0);  // 予約
    put16(1);  // アイコン
    put16(1);  // 絵は 1 枚
    ico += static_cast<char>(w >= 256 ? 0 : w);  // 0 = 256 (以上)
    ico += static_cast<char>(h >= 256 ? 0 : h);
    ico += '\0';  // パレットなし
    ico += '\0';
    put16(1);   // プレーン数
    put16(32);  // ビット数
    put32(static_cast<uint32_t>(png.size()));
    put32(6 + 16);  // 絵が始まる位置
    ico.append(png);
    return ico;
}

std::optional<std::string> IconFile(std::string_view bytes) {
    // .ico: 予約 0、種類 1、絵が 1 枚以上 (とそこにある 16 バイトの項目)。
    if (bytes.size() >= 22 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1 && bytes[3] == 0 && (bytes[4] != 0 || bytes[5] != 0)) {
        const size_t count = static_cast<unsigned char>(bytes[4]) | (static_cast<size_t>(static_cast<unsigned char>(bytes[5])) << 8);
        if (bytes.size() < 6 + 16 * count) return std::nullopt;
        // 各画像 (大きさは +8、位置は +12) がファイルの中にある。
        auto u32 = [&](size_t at) {
            uint32_t v = 0;
            for (size_t i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
            return v;
        };
        for (size_t i = 0; i < count; ++i) {
            const uint64_t size = u32(6 + 16 * i + 8), at = u32(6 + 16 * i + 12);
            if (size == 0 || at < 6 + 16 * count || at + size > bytes.size()) return std::nullopt;
        }
        return std::string(bytes);
    }
    return IcoFromPng(bytes);
}

std::string LicensesText(std::string_view titleUtf8, const std::vector<LicenseNotice>& notices) {
    if (notices.empty()) return {};
    const std::string rule(72, '=');
    std::string out;
    out += std::string(titleUtf8) + "\n\n";
    // (遊ぶ人が読むファイル: どの言語のゲームでも読めるよう、英語と日本語の両方で書く)
    out += "This game uses the following software. Their licenses are below.\n";
    out += "このゲームは次のソフトウェアを使っています。そのライセンスは下のとおりです。\n";
    std::vector<std::string_view> done;
    for (const LicenseNotice& n : notices) {
        if (std::find(done.begin(), done.end(), n.name) != done.end()) continue;
        done.push_back(n.name);
        out += "\n" + rule + "\n" + n.name + "\n" + rule + "\n\n";
        std::string text = n.text;
        if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);  // (UTF-8 の印)
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        out += text + "\n";
    }
    return out;
}

uint64_t BuildNumber(const std::vector<std::pair<std::string, std::string>>& entries) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::string_view s) {
        for (const unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= 0xff;
        h *= 1099511628211ull;
    };
    for (const auto& [path, bytes] : entries) {
        mix(path);
        mix(bytes);
    }
    return h != 0 ? h : 1;
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
