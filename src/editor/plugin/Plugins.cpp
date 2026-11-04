#include "editor/plugin/Plugins.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "platform/Shell.h"

#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

void PluginRegistry::AddSoftware(SoftwareType type) {
    type.plugin = current_;
    if (const char* p = plugin_id_detail::PartProblem(type.id, 2, 64, "ソフトの ID が短すぎます", "ソフトの ID が長すぎます")) {
        log::Error("プラグイン {} のソフト「{}」を登録できません: {}", current_.Text(), type.id, p);
        return;
    }
    for (const auto& s : softwares_) {
        if (s.plugin == type.plugin && s.id == type.id) {
            log::Error("プラグイン {} がソフト「{}」を 2 回登録しました", current_.Text(), type.id);
            return;
        }
    }
    if (!type.create) {
        log::Error("プラグイン {} のソフト「{}」に作り方 (create) がありません", current_.Text(), type.id);
        return;
    }
    softwares_.push_back(std::move(type));
}

void Plugins::Load(const fs::path& dir) {
    const auto builtin = BuiltinPlugins();
    std::vector<PluginCandidate> candidates;
    for (const BuiltinPlugin& b : builtin) {
        PluginCandidate cand;
        cand.id = b.id.Id();
        const fs::path file = dir / PathFromUtf8(cand.id.author) / PathFromUtf8(cand.id.name) / L"plugin.json";
        std::error_code ec;
        if (fs::exists(file, ec)) {
            cand.manifestFound = true;
            std::string err;
            if (auto bytes = platform::ReadFileBytes(file, &err)) {
                json::ParseError pe;
                if (auto doc = json::Parse(*bytes, &pe)) {
                    cand.manifest = ParsePluginManifest(*doc, &cand.manifestError);
                } else {
                    cand.manifestError = std::format("plugin.json が壊れています ({} 行 {} 文字目: {})", pe.line, pe.column, pe.message);
                }
            } else {
                cand.manifestError = err;
            }
        }
        candidates.push_back(std::move(cand));
    }

    PluginResolution res = ResolvePlugins(candidates);
    statuses_ = std::move(res.statuses);
    for (const size_t i : res.order) {
        registry_.BeginPlugin(statuses_[i].id);
        builtin[i].registerEditor(registry_);
        registry_.EndPlugin();
    }
    for (const auto& s : statuses_) {
        if (s.state == PluginState::Enabled) {
            log::Info("プラグイン: {} ({})", s.displayName, s.id.Text());
        } else {
            log::Warn("プラグイン {} ({}) は使えません: {}", s.displayName, s.id.Text(), s.reason);
        }
    }
}

const PluginStatus* Plugins::Find(const PluginId& id) const {
    for (const auto& s : statuses_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

bool Plugins::IsEnabled(const PluginId& id) const {
    const PluginStatus* s = Find(id);
    return s && s->state == PluginState::Enabled;
}

}  // namespace rm::editor
