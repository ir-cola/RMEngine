#include "editor/plugin/PluginManifest.h"

#include "editor/lang/Lang.h"
#include "editor/plugin/PluginIndex.h"
#include <algorithm>
#include <format>

namespace rm::editor {

namespace {

std::optional<PluginId> ParseId(const json::Value& v, std::string* error) {
    const json::Value* a = v.IsObject() ? v.Find("author") : nullptr;
    const json::Value* n = v.IsObject() ? v.Find("name") : nullptr;
    if (!a || !n || !a->IsString() || !n->IsString()) {
        if (error) *error = TrU8("ID には author と name の両方を文字で書いてください");
        return std::nullopt;
    }
    PluginId id{a->AsString(), n->AsString()};
    if (const char* p = PluginIdProblem(id.author, id.name)) {
        if (error) *error = TrU8F("ID ({}) が正しくありません: {}", id.Text(), TrU8(p));
        return std::nullopt;
    }
    return id;
}

}  // namespace

std::optional<std::vector<PluginId>> ParsePluginIdList(const json::Value& v, std::string* error) {
    std::vector<PluginId> out;
    if (v.IsNull()) return out;
    if (!v.IsArray()) {
        if (error) *error = TrU8("プラグインの一覧は配列で書いてください");
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
        if (error) *error = TrU8("plugin.json の形が正しくありません");
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

    if (const json::Value* nc = v.Find("needsConnector")) {
        if (!nc->IsBool()) {
            if (error) *error = TrU8("needsConnector は true か false で書いてください");
            return std::nullopt;
        }
        m.needsConnector = nc->AsBool();
    }
    if (const json::Value* cf = v.Find("connectorFor")) {
        std::string cfError;
        auto target = ParseId(*cf, &cfError);
        if (!target) {
            if (error) *error = "connectorFor: " + cfError;
            return std::nullopt;
        }
        if (*target == m.id || m.needsConnector) {
            if (error) *error = TrU8("つなぎのプラグインは、自分自身のつなぎにも、つなぎが要るプラグインにもなれません");
            return std::nullopt;
        }
        if (std::find(m.requirements.begin(), m.requirements.end(), *target) == m.requirements.end()) {
            if (error) *error = TrU8F("connectorFor の相手 ({}) を requires にも書いてください", target->Text());
            return std::nullopt;
        }
        m.connectorFor = std::move(*target);
    }
    if (const json::Value* sw = v.Find("software")) {
        bool ok = sw->IsArray();
        for (const auto& item : sw->Items()) {
            ok = ok && item.IsString() && !item.AsString().empty();
            if (ok) m.softwares.push_back(item.AsString());
        }
        if (!ok) {
            if (error) *error = TrU8("software はソフトの ID (文字) の配列で書いてください");
            return std::nullopt;
        }
    }
    // (2026-11-21 より前の plugin.json は "version": 1 の数: 版を書いていないのと同じに扱う)
    if (const json::Value* ver = v.Find("version"); ver && ver->IsString()) {
        m.version = ver->AsString();
        if (!ParseVersion(m.version)) {
            if (error) *error = TrU8F("version「{}」は数 3 つ (例 0.0.1) で書いてください", m.version);
            return std::nullopt;
        }
    }
    if (const json::Value* eng = v.Find("engine")) {
        m.engineMin = eng->Get("min").AsString();
        if (!m.engineMin.empty() && !ParseVersion(m.engineMin)) {
            if (error) *error = TrU8F("engine.min「{}」は数 3 つで書いてください", m.engineMin);
            return std::nullopt;
        }
    }
    if (const json::Value* code = v.Find("code")) {
        if (!code->IsBool()) {
            if (error) *error = TrU8("code は true か false で書いてください");
            return std::nullopt;
        }
        m.code = code->AsBool();
    }
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
            s.reason = TrU8("インストールされていません");
        } else if (!c[i].manifestFound) {
            s.state = PluginState::NoManifest;
            s.reason = TrU8("plugin.json が見つかりません");
        } else if (!c[i].manifest) {
            s.state = PluginState::BadManifest;
            s.reason = c[i].manifestError.empty() ? TrU8("plugin.json を読めません") : c[i].manifestError;
        } else if (c[i].manifest->id != c[i].id) {
            s.state = PluginState::IdMismatch;
            s.reason = TrU8F("plugin.json の ID ({}) がプログラムの ID ({}) と違います", c[i].manifest->id.Text(),
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
                    r.statuses[k].reason = TrU8F("同じ ID ({}) のプラグインが 2 つあります", c[i].id.Text());
                }
            }
        }
    }

    // 前提を深さ優先でたどる: プラグインは、前提が全部有効になった後でだけ有効になる。
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
                // (未インストールの候補は「使える」にならないので、その前提はたどらない)
                const size_t j = find(req);
                if (j == n) {
                    s.state = PluginState::MissingRequirement;
                    s.reason = TrU8F("前提のプラグイン {} がありません", req.Text());
                    break;
                }
                if (mark[j] == Mark::Visiting) {
                    s.state = PluginState::Cycle;
                    s.reason = TrU8F("前提が輪になっています ({} → {})", c[i].id.Text(), req.Text());
                    break;
                }
                self(self, j);
                if (r.statuses[j].state != PluginState::Enabled) {
                    s.state = PluginState::RequirementDisabled;
                    s.reason = r.statuses[j].state == PluginState::NotInstalled
                                   ? TrU8F("前提のプラグイン「{}」がインストールされていません", TrU8(r.statuses[j].displayName))
                                   : TrU8F("前提のプラグイン「{}」が使えないため、使えません", TrU8(r.statuses[j].displayName));
                    break;
                }
            }
            if (s.state == PluginState::Enabled) r.order.push_back(i);
        }
        mark[i] = Mark::Done;
    };
    for (size_t i = 0; i < n; ++i) visit(visit, i);

    // コネクタ (設計 4-f、憲法 2): コネクタが必要なプラグインは、コネクタが 1 つある間だけ有効になる。
    // コネクタはそのプラグインを前提とするので、後でないとオフにできない - そして、それを
    // 前提とするもの (そのコネクタも) も、変わらなくなるまでオフになる。
    auto isEnabled = [&](size_t j) { return r.statuses[j].state == PluginState::Enabled; };
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < n; ++i) {
            if (!isEnabled(i) || !c[i].manifest->needsConnector) continue;
            bool any = false;
            std::string names;
            for (size_t j = 0; j < n; ++j) {
                const auto& mj = c[j].manifest;
                if (!mj || !mj->connectorFor || *mj->connectorFor != c[i].id) continue;
                any |= isEnabled(j);
                names += (names.empty() ? "「" : "」「") + TrU8(r.statuses[j].displayName);
            }
            if (any) continue;
            PluginStatus& s = r.statuses[i];
            s.state = PluginState::NoConnector;
            s.reason = names.empty() ? TrU8("つなぎのプラグイン (3D 空間 などと組み合わせるもの) がこのエンジンにありません")
                                     : TrU8F("つなぎのプラグイン ({}」) が使えないため、使えません。3D 空間 などのプラグインと一緒に入れてください", names);
            changed = true;
        }
        for (size_t i = 0; i < n; ++i) {
            if (!isEnabled(i)) continue;
            for (const PluginId& req : c[i].manifest->requirements) {
                const size_t j = find(req);
                if (j == n || isEnabled(j)) continue;
                r.statuses[i].state = PluginState::RequirementDisabled;
                r.statuses[i].reason = TrU8F("前提のプラグイン「{}」が使えないため、使えません", TrU8(r.statuses[j].displayName));
                changed = true;
                break;
            }
        }
    }
    std::erase_if(r.order, [&](size_t i) { return !isEnabled(i); });
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
        // 準備できたプラグインも通る: もっと先の前提が無いかもしれない。
        visiting.push_back(p);
        for (const PluginId& req : m->requirements) self(self, req);
        visiting.pop_back();
        walked.push_back(p);
        if (!isReady) plan.push_back(p);
    };
    add(add, id);

    // コネクタは付いてくる (設計 4-f、憲法 1: 手では選ばない)。
    auto have = [&](const PluginId& p) { return Contains(ready, p) || Contains(plan, p); };
    for (bool changed = true; changed;) {
        changed = false;
        // 前提が全部そろっているコネクタすべて。
        for (const PluginManifest& m : available) {
            if (!m.connectorFor || have(m.id)) continue;
            if (std::all_of(m.requirements.begin(), m.requirements.end(), have)) {
                plan.push_back(m.id);
                walked.push_back(m.id);
                changed = true;
            }
        }
        // コネクタが必要なプラグインをインストールするとき: そのコネクタ (すでにあるもの、
        // なければこのエンジンの最初のもの) と、そのコネクタが前提とする全部。インストール済みの
        // コネクタは、新しいエンジンが足したプラグインを必要とすることがある (前提が 1 つ増えたコネクタ)。
        std::vector<PluginId> targets = plan;
        if (!Contains(targets, id)) targets.push_back(id);
        for (const PluginId& t : targets) {
            const PluginManifest* tm = FindManifest(available, t);
            if (!tm || !tm->needsConnector) continue;
            const PluginManifest* chosen = nullptr;
            for (const PluginManifest& m : available) {
                if (!m.connectorFor || *m.connectorFor != t) continue;
                if (have(m.id)) {
                    chosen = &m;
                    break;
                }
                if (!chosen) chosen = &m;
            }
            if (!chosen || Contains(walked, chosen->id)) continue;
            const size_t before = plan.size();
            add(add, chosen->id);
            changed |= plan.size() != before;
        }
    }
    return plan;
}

std::vector<PluginId> ReadyPlugins(const std::vector<PluginManifest>& installed, const std::vector<PluginManifest>& available) {
    std::vector<PluginId> ready;
    for (const PluginManifest& m : installed) {
        const PluginManifest* engine = FindManifest(available, m.id);
        if (!engine || *engine == m || IsNewerVersion(m.version, engine->version)) ready.push_back(m.id);
    }
    return ready;
}

bool NewerThanEngine(const PluginManifest& installed, const std::vector<PluginManifest>& available) {
    const PluginManifest* engine = FindManifest(available, installed.id);
    return engine && IsNewerVersion(installed.version, engine->version);
}

std::vector<PluginId> ConnectorsToBring(const std::vector<PluginManifest>& installed, const std::vector<PluginManifest>& available) {
    std::vector<PluginId> out;
    auto have = [&](const PluginId& p) { return FindManifest(installed, p) != nullptr || Contains(out, p); };
    // (もう増えなくなるまで繰り返す: コネクタが別のコネクタにつながることがある)
    for (bool changed = true; changed;) {
        changed = false;
        for (const PluginManifest& m : available) {
            if (!m.connectorFor || have(m.id)) continue;
            if (std::all_of(m.requirements.begin(), m.requirements.end(), have)) {
                out.push_back(m.id);
                changed = true;
            }
        }
    }
    return out;
}

std::vector<PluginId> FirstStartInstalls(const std::vector<PluginId>& available, const std::vector<PluginId>& legacyInstalled) {
    if (legacyInstalled.empty()) return available;
    std::vector<PluginId> out;
    for (const PluginId& id : available) {
        if (Contains(legacyInstalled, id)) out.push_back(id);
    }
    return out;
}

std::vector<Dependent> Dependents(const PluginId& id, const std::vector<PluginManifest>& installed) {
    // `id` から幅優先: 前線にあるプラグインを前提とするものが次の輪。
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
                // 外れるコネクタ: 仕える先のプラグインも、それが最後の 1 つだったなら止まる。
                if (!m.connectorFor || Contains(seen, *m.connectorFor)) continue;
                const PluginManifest* target = FindManifest(installed, *m.connectorFor);
                if (!target || !target->needsConnector) continue;
                const bool another = std::any_of(installed.begin(), installed.end(), [&](const PluginManifest& o) {
                    return o.connectorFor && *o.connectorFor == target->id && !Contains(seen, o.id);
                });
                if (another) continue;
                seen.push_back(target->id);
                next.push_back(target->id);
                out.push_back({target->id, m.id});
            }
        }
        frontier = std::move(next);
    }
    return out;
}

}  // namespace rm::editor
