#include "editor/plugin/PluginManifest.h"

#include <algorithm>
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
        if (!c[i].installed) {
            s.state = PluginState::NotInstalled;
            s.reason = "インストールされていません";
        } else if (!c[i].manifestFound) {
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
                // (a not-installed candidate is never "usable", so its requirements are not followed)
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
                    s.reason = r.statuses[j].state == PluginState::NotInstalled
                                   ? std::format("前提のプラグイン「{}」がインストールされていません", r.statuses[j].displayName)
                                   : std::format("前提のプラグイン「{}」が使えないため、使えません", r.statuses[j].displayName);
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

namespace {

const PluginManifest* FindManifest(const std::vector<PluginManifest>& list, const PluginId& id) {
    for (const auto& m : list) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

bool Contains(const std::vector<PluginId>& list, const PluginId& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

}  // namespace

std::vector<PluginId> InstallPlan(const PluginId& id, const std::vector<PluginManifest>& available,
                                  const std::vector<PluginId>& ready, std::vector<PluginId>* unavailable) {
    std::vector<PluginId> plan;
    std::vector<PluginId> visiting;
    std::vector<PluginId> walked;
    auto add = [&](auto&& self, const PluginId& p) -> void {
        if (Contains(walked, p) || Contains(visiting, p)) return;
        const bool isReady = Contains(ready, p);
        const PluginManifest* m = FindManifest(available, p);
        if (!m) {
            if (!isReady && unavailable && !Contains(*unavailable, p)) unavailable->push_back(p);
            return;
        }
        // Ready plugins are walked through too: a requirement further down may be missing.
        visiting.push_back(p);
        for (const PluginId& req : m->requirements) self(self, req);
        visiting.pop_back();
        walked.push_back(p);
        if (!isReady) plan.push_back(p);
    };
    add(add, id);
    return plan;
}

std::vector<Dependent> Dependents(const PluginId& id, const std::vector<PluginManifest>& installed) {
    // Breadth first from `id`: whoever requires a plugin in the frontier is the next ring.
    std::vector<Dependent> out;
    std::vector<PluginId> frontier{id};
    std::vector<PluginId> seen{id};
    while (!frontier.empty()) {
        std::vector<PluginId> next;
        for (const PluginId& f : frontier) {
            for (const PluginManifest& m : installed) {
                if (Contains(seen, m.id) || !Contains(m.requirements, f)) continue;
                seen.push_back(m.id);
                next.push_back(m.id);
                Dependent d;
                d.id = m.id;
                if (f != id) d.via = f;
                out.push_back(std::move(d));
            }
        }
        frontier = std::move(next);
    }
    return out;
}

}  // namespace rm::editor
