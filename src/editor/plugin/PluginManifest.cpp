#include "editor/plugin/PluginManifest.h"

#include <format>

namespace rm::editor {

namespace {

std::optional<PluginId> ParseId(const json::Value& v, std::string* error) {
    const json::Value* a = v.IsObject() ? v.Find("author") : nullptr;
    const json::Value* n = v.IsObject() ? v.Find("name") : nullptr;
    if (!a || !n || !a->IsString() || !n->IsString()) {
        if (error) *error = "ID には author と name の両方を文字で書いてください";
        return std::nullopt;
    }
    PluginId id{a->AsString(), n->AsString()};
    if (const char* p = PluginIdProblem(id.author, id.name)) {
        if (error) *error = std::format("ID ({}) が正しくありません: {}", id.Text(), p);
        return std::nullopt;
    }
    return id;
}

}  // namespace

std::optional<std::vector<PluginId>> ParsePluginIdList(const json::Value& v, std::string* error) {
    std::vector<PluginId> out;
    if (v.IsNull()) return out;
    if (!v.IsArray()) {
        if (error) *error = "プラグインの一覧は配列で書いてください";
        return std::nullopt;
    }
    for (const auto& item : v.Items()) {
        auto id = ParseId(item, error);
        if (!id) return std::nullopt;
        out.push_back(std::move(*id));
    }
    return out;
}

std::optional<PluginManifest> ParsePluginManifest(const json::Value& v, std::string* error) {
    if (!v.IsObject()) {
        if (error) *error = "plugin.json の形が正しくありません";
        return std::nullopt;
    }
    auto id = ParseId(v, error);
    if (!id) return std::nullopt;
    std::string reqError;
    auto requirements = ParsePluginIdList(v.Get("requires"), &reqError);
    if (!requirements) {
        if (error) *error = "requires: " + reqError;
        return std::nullopt;
    }
    PluginManifest m;
    m.id = std::move(*id);
    m.displayName = v.Get("displayName").AsString();
    if (m.displayName.empty()) m.displayName = m.id.name;
    m.requirements = std::move(*requirements);
    return m;
}

PluginResolution ResolvePlugins(const std::vector<PluginCandidate>& c) {
    const size_t n = c.size();
    PluginResolution r;
    r.statuses.resize(n);
    std::vector<bool> usable(n, false);
    for (size_t i = 0; i < n; ++i) {
        PluginStatus& s = r.statuses[i];
        s.id = c[i].id;
        s.displayName = c[i].manifest ? c[i].manifest->displayName : c[i].id.Text();
        if (!c[i].manifestFound) {
            s.state = PluginState::NoManifest;
            s.reason = "plugin.json が見つかりません";
        } else if (!c[i].manifest) {
            s.state = PluginState::BadManifest;
            s.reason = c[i].manifestError.empty() ? "plugin.json を読めません" : c[i].manifestError;
        } else if (c[i].manifest->id != c[i].id) {
            s.state = PluginState::IdMismatch;
            s.reason = std::format("plugin.json の ID ({}) がプログラムの ID ({}) と違います", c[i].manifest->id.Text(),
                                   c[i].id.Text());
        } else {
            usable[i] = true;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (c[i].id == c[j].id) {
                for (const size_t k : {i, j}) {
                    usable[k] = false;
                    r.statuses[k].state = PluginState::BadManifest;
                    r.statuses[k].reason = std::format("同じ ID ({}) のプラグインが 2 つあります", c[i].id.Text());
                }
            }
        }
    }

    // Depth-first over the requirements: a plugin is enabled only after all of its requirements are.
    enum class Mark { None, Visiting, Done };
    std::vector<Mark> mark(n, Mark::None);
    auto find = [&](const PluginId& id) -> size_t {
        for (size_t j = 0; j < n; ++j) {
            if (c[j].id == id) return j;
        }
        return n;
    };
    auto visit = [&](auto&& self, size_t i) -> void {
        if (mark[i] != Mark::None) return;
        mark[i] = Mark::Visiting;
        PluginStatus& s = r.statuses[i];
        if (usable[i]) {
            for (const PluginId& req : c[i].manifest->requirements) {
                const size_t j = find(req);
                if (j == n) {
                    s.state = PluginState::MissingRequirement;
                    s.reason = std::format("前提のプラグイン {} がありません", req.Text());
                    break;
                }
                if (mark[j] == Mark::Visiting) {
                    s.state = PluginState::Cycle;
                    s.reason = std::format("前提が輪になっています ({} → {})", c[i].id.Text(), req.Text());
                    break;
                }
                self(self, j);
                if (r.statuses[j].state != PluginState::Enabled) {
                    s.state = PluginState::RequirementDisabled;
                    s.reason = std::format("前提のプラグイン「{}」が使えないため、使えません", r.statuses[j].displayName);
                    break;
                }
            }
            if (s.state == PluginState::Enabled) r.order.push_back(i);
        }
        mark[i] = Mark::Done;
    };
    for (size_t i = 0; i < n; ++i) visit(visit, i);
    return r;
}

}  // namespace rm::editor
