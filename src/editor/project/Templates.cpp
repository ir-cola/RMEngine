#include "editor/project/Templates.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"

#include <algorithm>
#include <format>

namespace rm::editor {

namespace {

constexpr size_t kNpos = static_cast<size_t>(-1);

void AppendUnique(std::vector<PluginId>& to, const PluginId& id) {
    if (std::find(to.begin(), to.end(), id) == to.end()) to.push_back(id);
}

// テンプレートの ID、またはパスの一部: a-z 0-9 _ (書き方は 1 通り: "3d" と "3D" が 2 つの枝にならない)、
// 1〜32 文字。数字で始めてよい ("2d") が、_ で始めたり終わったりはできず、"__" も使えない。
std::string PartError(const std::string& s) {
    if (s.empty()) return TrU8("空の ID は使えません");
    if (s.size() > 32) return TrU8("ID は 32 文字までです");
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (!plugin_id_detail::IsLower(c) && !plugin_id_detail::IsDigit(c) && c != '_') return TrU8("使える文字は英小文字・数字・_ だけです");
        if (c == '_' && (i == 0 || i + 1 == s.size() || s[i + 1] == '_')) return TrU8("_ は先頭・末尾・続けて書くことはできません");
    }
    return {};
}

std::string JoinPath(const std::vector<std::string>& path) {
    std::string s;
    for (const auto& p : path) s += (s.empty() ? "" : "/") + p;
    return s;
}

bool IsKey(const json::Value& v) {
    return v.IsObject() && v.Get("author").IsString() && v.Get("plugin").IsString() && v.Get("software").IsString();
}

bool SameKey(const json::Value& a, const json::Value& b) {
    for (const char* f : {"author", "plugin", "software"}) {
        if (a.Get(f).AsString() != b.Get(f).AsString()) return false;
    }
    return true;
}

}  // namespace

const ProjectTemplate& BlankTemplate() {
    static const ProjectTemplate kBlank = [] {
        ProjectTemplate t;
        t.id = "blank";
        t.path = {"blank"};
        t.name = WideToUtf8(TrKey(L"白紙"));
        t.description = WideToUtf8(TrKey(L"何も入っていないプロジェクト。プラグインを足して育てます。"));
        t.order = -1000;  // いつも最初
        return t;
    }();
    return kBlank;
}

std::optional<ProjectTemplate> ParseTemplate(const json::Value& v, const PluginId& owner, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<ProjectTemplate> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    if (!v.IsObject() || v.Get("kind").AsString() != kTemplateKind) return fail(TrU8("テンプレートのファイルではありません (kind)"));
    const int64_t format = v.Get("format").AsInt(0);
    if (format < 1 || format > kTemplateFormat) {
        return fail(TrU8F("このエンジンの読めない形式のテンプレートです (形式 {} / このエンジンは {} まで)", format, kTemplateFormat));
    }
    ProjectTemplate t;
    t.plugin = owner;
    t.id = v.Get("id").AsString();
    if (std::string e = PartError(t.id); !e.empty()) return fail(TrU8F("id: {}", e));
    const json::Value& path = v.Get("path");
    if (!path.IsArray() || path.Items().empty()) return fail(TrU8("path は木の場所 (ID の配列) で書いてください"));
    for (const auto& part : path.Items()) {
        if (!part.IsString()) return fail(TrU8("path は木の場所 (ID の配列) で書いてください"));
        if (std::string e = PartError(part.AsString()); !e.empty()) return fail(TrU8F("path ({}): {}", part.AsString(), e));
        t.path.push_back(part.AsString());
    }
    t.name = v.Get("name").AsString();
    if (t.name.empty()) return fail(TrU8("name (表示名) を書いてください"));
    t.description = v.Get("description").AsString();
    t.order = static_cast<int>(v.Get("order").AsInt(0));

    t.plugins.push_back(owner);
    std::string listError;
    auto plugins = ParsePluginIdList(v.Get("plugins"), &listError);
    if (!plugins) return fail("plugins: " + listError);
    for (const PluginId& id : *plugins) AppendUnique(t.plugins, id);

    if (const json::Value* open = v.Find("open"); open && !open->IsNull()) {
        if (!IsKey(*open)) return fail(TrU8("open は { author, plugin, software } で書いてください"));
        t.open = *open;
    }
    for (const char* key : {"project", "userSettings"}) {
        const json::Value& part = v.Get(key);
        if (!part.IsNull() && !part.IsObject()) return fail(TrU8F("{} はオブジェクトで書いてください", key));
    }
    t.project = v.Get("project");
    t.userSettings = v.Get("userSettings");
    if (!t.userSettings.IsNull()) {
        const json::Value& list = t.userSettings.Get("softwares");
        if (!list.IsNull() && !list.IsArray()) return fail(TrU8("userSettings.softwares は配列で書いてください"));
        for (const auto& e : list.Items()) {
            if (!IsKey(e)) return fail(TrU8("userSettings.softwares の 1 つずつに author / plugin / software を書いてください"));
        }
    }
    return t;
}

std::string TemplateTree::Label(size_t node) const {
    if (const ProjectTemplate* t = At(node)) return t->name;
    return node < nodes.size() ? nodes[node].path.back() : std::string();
}

size_t TemplateTree::Find(const std::vector<std::string>& path) const {
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].path == path) return i;
    }
    return kNpos;
}

TemplateTree BuildTemplateTree(std::vector<ProjectTemplate> templates) {
    TemplateTree tree;
    // 同じ場所の 2 つ (または 1 つのプラグインの同じ ID) のうち、最初の 1 つを残す。
    for (ProjectTemplate& t : templates) {
        bool clash = false;
        for (const ProjectTemplate& kept : tree.templates) {
            if (kept.path == t.path) {
                tree.warnings.push_back(TrU8F("テンプレート「{}」は「{}」と同じ場所 ({}) にあるので使いません", t.name, kept.name, JoinPath(t.path)));
                clash = true;
                break;
            }
            if (kept.plugin == t.plugin && kept.id == t.id) {
                tree.warnings.push_back(TrU8F("同じプラグインに同じ id ({}) のテンプレートが 2 つあるので、後のほうを使いません", t.id));
                clash = true;
                break;
            }
        }
        if (!clash) tree.templates.push_back(std::move(t));
    }

    // パスの先頭部分はどれもノードになる。ノードの order はそのテンプレートのもので、枝は
    // その下で最も小さいもの。
    struct Raw {
        std::vector<std::string> path;
        int tmpl = -1;
        int order = 0;
        bool orderSet = false;
    };
    std::vector<Raw> raw;
    auto rawOf = [&](const std::vector<std::string>& path) -> Raw& {
        for (Raw& r : raw) {
            if (r.path == path) return r;
        }
        raw.push_back({path});
        return raw.back();
    };
    for (size_t i = 0; i < tree.templates.size(); ++i) {
        const ProjectTemplate& t = tree.templates[i];
        for (size_t n = 1; n <= t.path.size(); ++n) {
            Raw& r = rawOf({t.path.begin(), t.path.begin() + static_cast<std::ptrdiff_t>(n)});
            if (n == t.path.size()) {
                r.tmpl = static_cast<int>(i);
                r.order = t.order;
                r.orderSet = true;
            } else if (r.tmpl < 0 && (!r.orderSet || t.order < r.order)) {
                r.order = t.order;
                r.orderSet = true;
            }
        }
    }
    auto label = [&](const Raw& r) { return r.tmpl >= 0 ? tree.templates[static_cast<size_t>(r.tmpl)].name : r.path.back(); };
    // 深さ優先: `parent` の子を順に、それぞれの後にその下を続ける。
    auto add = [&](auto&& self, const std::vector<std::string>& parentPath, size_t parentIndex) -> void {
        std::vector<const Raw*> children;
        for (const Raw& r : raw) {
            if (r.path.size() == parentPath.size() + 1 && std::equal(parentPath.begin(), parentPath.end(), r.path.begin())) {
                children.push_back(&r);
            }
        }
        std::stable_sort(children.begin(), children.end(), [&](const Raw* a, const Raw* b) {
            if (a->order != b->order) return a->order < b->order;
            return label(*a) < label(*b);
        });
        for (const Raw* r : children) {
            tree.nodes.push_back({r->path, r->path.size() - 1, parentIndex, r->tmpl});
            self(self, r->path, tree.nodes.size() - 1);
        }
    };
    add(add, {}, kNpos);
    return tree;
}

std::vector<PluginId> TemplatePlugins(const TemplateTree& tree, size_t node) {
    std::vector<PluginId> out;
    if (node >= tree.nodes.size()) return out;
    const auto& base = tree.nodes[node].path;
    // ノードは木の順: そのノード、続いて、同じ深さかそれより上の次のノードまでの、その下すべて。
    for (size_t i = node; i < tree.nodes.size(); ++i) {
        if (i > node && tree.nodes[i].depth <= tree.nodes[node].depth) break;
        if (!std::equal(base.begin(), base.end(), tree.nodes[i].path.begin())) break;
        if (const ProjectTemplate* t = tree.At(i)) {
            for (const PluginId& id : t->plugins) AppendUnique(out, id);
        }
    }
    return out;
}

std::vector<PluginId> TemplateInstallPlan(const std::vector<PluginId>& needed, const std::vector<PluginManifest>& available,
                                          const std::vector<PluginId>& ready, std::vector<PluginId>* unavailable) {
    std::vector<PluginId> out, missing;
    // 前の計画で写すものは、後の計画では「そこにある」とみなす。コネクターが必要なプラグインは
    // 最後に回す: そのときにはテンプレート自身のコネクター (2D の) が計画済みなので、このエンジンが
    // 持つ最初のコネクター (3D の) を無駄に持ってこない。
    std::vector<PluginId> there = ready;
    auto needsConnector = [&](const PluginId& id) {
        const auto it = std::find_if(available.begin(), available.end(), [&](const PluginManifest& m) { return m.id == id; });
        return it != available.end() && it->needsConnector;
    };
    for (const bool last : {false, true}) {
        for (const PluginId& id : needed) {
            if (needsConnector(id) != last) continue;
            std::vector<PluginId> notHere;
            for (const PluginId& p : InstallPlan(id, available, there, &notHere)) {
                AppendUnique(out, p);
                AppendUnique(there, p);
            }
            for (const PluginId& p : notHere) AppendUnique(missing, p);
        }
    }
    if (unavailable) *unavailable = std::move(missing);
    return out;
}

void ApplyTemplate(json::Value& doc, const ProjectTemplate& t) {
    json::Value record = json::Value::MakeObject();
    if (t.plugin) {
        record.Set("author", t.plugin->author);
        record.Set("plugin", t.plugin->name);
    }
    record.Set("id", t.id);
    doc.Set("template", std::move(record));
    if (t.project.IsObject()) {
        static constexpr const char* kCoreKeys[] = {"kind", "formatVersion", "name", "engineVersion", "template", "plugins"};
        for (const auto& [key, value] : t.project.Members()) {
            if (std::find(std::begin(kCoreKeys), std::end(kCoreKeys), key) != std::end(kCoreKeys)) continue;
            doc.Set(key, value);
        }
    }
    // データがもうプロジェクトにあるプラグインを記録する (そのデータを保存すると記録されるのと同じ)。
    json::Value* used = doc.Find("plugins");
    if (!used || !used->IsArray()) used = &doc.Set("plugins", json::Value::MakeArray());
    for (const auto& entry : doc.Get("softwareData").Items()) {
        const std::string& author = entry.Get("author").AsString();
        const std::string& name = entry.Get("plugin").AsString();
        if (author.empty() || name.empty()) continue;
        bool known = false;
        for (const auto& u : used->Items()) known |= u.Get("author").AsString() == author && u.Get("name").AsString() == name;
        if (known) continue;
        json::Value rec = json::Value::MakeObject();
        rec.Set("author", author);
        rec.Set("name", name);
        used->Push(std::move(rec));
    }
}

json::Value TemplateLayout(const ProjectTemplate& t) {
    const json::Value& entries = t.userSettings.Get("softwares");
    if (t.open.IsNull() && entries.Items().empty()) return {};
    json::Value doc = json::Value::MakeObject();
    doc.Set("kind", kLayoutKind);
    doc.Set("formatVersion", kLayoutFormatVersion);
    if (!t.open.IsNull()) {
        json::Value active = json::Value::MakeObject();
        for (const char* f : {"author", "plugin", "software"}) active.Set(f, t.open.Get(f));
        doc.Set("active", std::move(active));
    }
    json::Value list = json::Value::MakeArray();
    for (const auto& e : entries.Items()) {
        bool twice = false;
        for (const auto& kept : list.Items()) twice |= SameKey(kept, e);
        if (!twice) list.Push(e);
    }
    if (!list.Items().empty()) doc.Set("softwares", std::move(list));
    return doc;
}

std::vector<SoftwareGroup> GroupPluginsBySoftware(const TemplateTree& tree, const std::vector<PluginManifest>& available) {
    std::vector<SoftwareGroup> groups;
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        if (tree.nodes[i].depth != 0) continue;
        SoftwareGroup g{tree.nodes[i].path.front(), tree.Label(i), {}};
        // テンプレートを入れたときに来るもの全部 (何も入っていないものとして数える)。
        g.plugins = TemplateInstallPlan(TemplatePlugins(tree, i), available, {}, nullptr);
        groups.push_back(std::move(g));
    }
    for (const PluginManifest& m : available) {
        for (const std::string& sw : m.softwares) {
            auto it = std::find_if(groups.begin(), groups.end(), [&](const SoftwareGroup& g) { return g.id == sw; });
            if (it == groups.end()) {
                groups.push_back({sw, sw, {}});
                it = groups.end() - 1;
            }
            AppendUnique(it->plugins, m.id);
        }
    }
    // コネクターは、つなぐ物がすべてある所に置く (2026-11-17 画面で: ゲーム制作 × パーティクルが
    // 共通の下に出た)。動くものが無くなるまで繰り返す: コネクターがほかのコネクターを必要とする
    // ことがある ("パーティクル × 物理")。
    for (bool moved = true; moved;) {
        moved = false;
        for (const PluginManifest& m : available) {
            if (!m.connectorFor) continue;
            for (SoftwareGroup& g : groups) {
                auto in = [&](const PluginId& id) { return std::find(g.plugins.begin(), g.plugins.end(), id) != g.plugins.end(); };
                if (in(m.id) || !std::all_of(m.requirements.begin(), m.requirements.end(), in)) continue;
                g.plugins.push_back(m.id);
                moved = true;
            }
        }
    }
    std::erase_if(groups, [](const SoftwareGroup& g) { return g.plugins.empty(); });
    return groups;
}

}  // namespace rm::editor
