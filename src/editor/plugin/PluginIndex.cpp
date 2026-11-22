#include "editor/plugin/PluginIndex.h"

#include "editor/lang/Lang.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace rm::editor {

namespace {

bool StartsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

bool IsHex64(std::string_view s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

std::optional<uint64_t> Size(const json::Value& v) {
    if (!v.IsInt() || v.AsInt() < 0) return std::nullopt;
    return static_cast<uint64_t>(v.AsInt());
}

std::optional<IndexPackage> ParsePackage(const json::Value& v, std::string* why) {
    if (!v.IsObject()) {
        *why = TrU8("版の形が正しくありません");
        return std::nullopt;
    }
    IndexPackage p;
    p.version = v.Get("version").AsString();
    if (!ParseVersion(p.version)) {
        *why = TrU8F("版「{}」は数 3 つ (例 0.0.1) で書いてください", p.version);
        return std::nullopt;
    }
    if (const json::Value* e = v.Find("engine")) {
        p.engineMin = e->Get("min").AsString();
        if (!p.engineMin.empty() && !ParseVersion(p.engineMin)) {
            *why = TrU8F("engine.min「{}」は数 3 つで書いてください", p.engineMin);
            return std::nullopt;
        }
    }
    p.code = v.Get("code").AsBool(false);
    p.url = v.Get("url").AsString();
    p.sha256 = v.Get("sha256").AsString();
    const auto size = Size(v.Get("size"));
    if (p.url.empty() || !size || !IsHex64(p.sha256)) {
        *why = TrU8F("版 {} の url・size・sha256 が足りないか、形が違います", p.version);
        return std::nullopt;
    }
    p.size = *size;
    std::string reqError;
    auto reqs = ParsePluginIdList(v.Get("requires"), &reqError);
    if (!reqs) {
        *why = "requires: " + reqError;
        return std::nullopt;
    }
    p.requirements = std::move(*reqs);
    return p;
}

}  // namespace

std::optional<std::array<uint32_t, 3>> ParseVersion(std::string_view text) {
    std::array<uint32_t, 3> out{};
    size_t part = 0, digits = 0;
    uint64_t value = 0;
    for (const char ch : text) {
        if (ch == '.') {
            if (digits == 0 || part >= 2) return std::nullopt;
            out[part++] = static_cast<uint32_t>(value);
            value = 0;
            digits = 0;
        } else if (ch >= '0' && ch <= '9') {
            value = value * 10 + static_cast<uint64_t>(ch - '0');
            if (++digits > 9) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    if (digits == 0 || part != 2) return std::nullopt;
    out[2] = static_cast<uint32_t>(value);
    return out;
}

std::optional<int> CompareVersions(std::string_view a, std::string_view b) {
    const auto va = ParseVersion(a), vb = ParseVersion(b);
    if (!va || !vb) return std::nullopt;
    return *va < *vb ? -1 : *va > *vb ? 1 : 0;
}

bool IsNewerVersion(std::string_view a, std::string_view than) {
    if (than.empty()) return ParseVersion(a).has_value();
    const auto c = CompareVersions(a, than);
    return c && *c > 0;
}

std::optional<PluginIndex> ParsePluginIndex(const json::Value& v, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    if (!v.IsObject() || v.Get("kind").AsString() != "rm_engine.plugin_index") return fail(TrU8("プラグインの一覧 (index.json) ではありません"));
    const json::Value& format = v.Get("formatVersion");
    if (!format.IsNumber() || format.AsDouble() < 1) return fail(TrU8("一覧の formatVersion がありません"));
    if (format.AsDouble() > 1) return fail(TrU8("一覧が新しい形で書かれています。エンジンを新しい版にしてください"));
    PluginIndex index;
    for (const json::Value& item : v.Get("plugins").Items()) {
        std::string why;
        json::Value one = json::Value::MakeArray();
        one.Push(item);
        std::optional<std::vector<PluginId>> idList = ParsePluginIdList(one, &why);
        if (!idList || idList->size() != 1) {
            index.skipped.push_back(TrU8F("プラグインを飛ばしました: {}", why));
            continue;
        }
        IndexPlugin plugin;
        plugin.id = (*idList)[0];
        plugin.displayName = item.Get("displayName").AsString();
        if (plugin.displayName.empty()) plugin.displayName = plugin.id.name;
        for (const json::Value& ver : item.Get("versions").Items()) {
            if (auto p = ParsePackage(ver, &why)) {
                plugin.versions.push_back(std::move(*p));
            } else {
                index.skipped.push_back(TrU8F("{} の版を飛ばしました: {}", plugin.id.Text(), why));
            }
        }
        if (std::any_of(index.plugins.begin(), index.plugins.end(), [&](const IndexPlugin& o) { return o.id == plugin.id; })) {
            index.skipped.push_back(TrU8F("{} が 2 回書かれています (後のほうを飛ばしました)", plugin.id.Text()));
            continue;
        }
        index.plugins.push_back(std::move(plugin));
    }
    if (const json::Value* e = v.Find("engine"); e && e->IsObject()) {
        IndexEngine engine;
        engine.version = e->Get("version").AsString();
        engine.url = e->Get("url").AsString();
        engine.sha256 = e->Get("sha256").AsString();
        engine.size = Size(e->Get("size")).value_or(0);
        if (ParseVersion(engine.version)) {
            index.engine = std::move(engine);
        } else {
            index.skipped.push_back(TrU8F("エンジンの版「{}」を読めません", engine.version));
        }
    }
    return index;
}

bool EngineFits(const IndexPackage& package, std::string_view engineVersion) {
    if (package.engineMin.empty()) return true;
    const auto c = CompareVersions(engineVersion, package.engineMin);
    return c && *c >= 0;
}

const IndexPackage* NewestFitting(const IndexPlugin& plugin, std::string_view engineVersion, bool allowCode) {
    const IndexPackage* best = nullptr;
    for (const IndexPackage& p : plugin.versions) {
        if ((p.code && !allowCode) || !EngineFits(p, engineVersion)) continue;
        if (!best || IsNewerVersion(p.version, best->version)) best = &p;
    }
    return best;
}

std::string ResolveUrl(std::string_view base, std::string_view ref) {
    const bool absolute = StartsWithNoCase(ref, "http://") || StartsWithNoCase(ref, "https://") || StartsWithNoCase(ref, "file:") ||
                          (ref.size() >= 2 && ref[1] == ':') || StartsWithNoCase(ref, "\\\\");
    if (absolute || base.empty()) return std::string(ref);
    const size_t cut = base.find_last_of("/\\");
    if (cut == std::string_view::npos) return std::string(ref);
    std::string out(base.substr(0, cut + 1));
    if (!ref.empty() && (ref[0] == '/' || ref[0] == '\\')) {
        // サイトの根から (/plugins/x.zip): https://host/ まで
        const size_t scheme = base.find("://");
        if (scheme != std::string_view::npos) {
            const size_t host = base.find('/', scheme + 3);
            out = std::string(base.substr(0, host == std::string_view::npos ? base.size() : host));
        }
    }
    out += ref;
    return out;
}

bool SameSha256(std::string_view a, std::string_view b) {
    if (!IsHex64(a) || !IsHex64(b)) return false;
    for (size_t i = 0; i < 64; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

std::string Sha256Hex(const std::array<uint8_t, 32>& digest) {
    std::string out;
    out.reserve(64);
    for (const uint8_t b : digest) out += std::format("{:02x}", b);
    return out;
}

std::string NormalizeZipPath(std::string_view name) {
    std::string out(name);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

const wchar_t* ZipPathProblem(std::string_view name) {
    std::string_view s = name;
    if (!s.empty() && s.back() == '/') s.remove_suffix(1);  // (フォルダー)
    if (s.empty()) return TrKey(L"空の名前");
    if (s.size() > 240) return TrKey(L"名前が長すぎます");
    if (s.front() == '/') return TrKey(L"絶対パスです");
    for (const char ch : s) {
        const auto u = static_cast<unsigned char>(ch);
        if (ch == ':') return TrKey(L"ドライブやストリームの名前 (:) が入っています");
        if (u < 0x20 || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || ch == '\\') return TrKey(L"使えない文字が入っています");
    }
    size_t start = 0;
    while (start <= s.size()) {
        const size_t slash = s.find('/', start);
        const std::string_view part = s.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part.empty()) return TrKey(L"空の段 (//) があります");
        if (part == "." || part == "..") return TrKey(L"上のフォルダー (..) を指しています");
        if (part.back() == '.' || part.back() == ' ') return TrKey(L"末尾が . か空白の名前があります");
        std::string stem;
        for (const char ch : part.substr(0, part.find('.'))) stem += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        static constexpr std::string_view kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                                         "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
        if (std::find(std::begin(kReserved), std::end(kReserved), stem) != std::end(kReserved)) return TrKey(L"Windows の予約名 (con・nul など) です");
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return nullptr;
}

std::vector<PluginOffer> PluginOffers(const PluginIndex& index, const std::vector<PluginManifest>& installed,
                                      const std::vector<PluginManifest>& available, std::string_view engineVersion) {
    auto find = [](const std::vector<PluginManifest>& list, const PluginId& id) -> const PluginManifest* {
        for (const auto& m : list) {
            if (m.id == id) return &m;
        }
        return nullptr;
    };
    std::vector<PluginOffer> out;
    for (const IndexPlugin& plugin : index.plugins) {
        const IndexPackage* best = NewestFitting(plugin, engineVersion, false);
        if (!best) continue;
        const PluginManifest* mine = find(installed, plugin.id);
        const PluginManifest* engine = find(available, plugin.id);
        PluginOffer offer{&plugin, best, mine ? OfferKind::Update : OfferKind::Install, mine ? mine->version : std::string{}};
        // 入っていない: エンジンの写しより新しいときだけ (同じならふつうの「インストール」でよい)。
        const std::string_view current = mine ? std::string_view(mine->version) : engine ? std::string_view(engine->version) : std::string_view{};
        if ((mine || engine) && !IsNewerVersion(best->version, current)) continue;
        out.push_back(std::move(offer));
    }
    return out;
}

bool EngineUpdateAvailable(const PluginIndex& index, std::string_view engineVersion) {
    return index.engine && IsNewerVersion(index.engine->version, engineVersion);
}

}  // namespace rm::editor
