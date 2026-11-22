#include "editor/help/HelpBooks.h"

#include "base/Utf.h"
#include "editor/app/Paths.h"
#include "editor/lang/Lang.h"

namespace rm::editor {

help::Library LoadHelpLibrary(const Plugins& plugins) {
    help::BookSource core;
    core.id = "core";
    core.title = Tr(L"Hub とエディター本体");
    core.folder = paths::HelpFolder();
    std::vector<help::BookSource> books;
    for (const Plugins::Entry& e : plugins.Entries()) {
        if (e.status.state != PluginState::Enabled) continue;  // (入っていない・使えないプラグインのヘルプは出さない)
        help::BookSource b;
        b.id = e.status.id.author + "/" + e.status.id.name;
        b.title = Utf8ToWide(plugins.DisplayName(e.status.id));
        b.folder = plugins.InstalledFolder(e.status.id) / L"help";
        for (const PluginManifest& m : plugins.Available()) {
            if (m.id == e.status.id && m.connectorFor) b.under = m.connectorFor->author + "/" + m.connectorFor->name;
        }
        books.push_back(std::move(b));
    }
    return help::ReadLibrary(core, books, lang::Current() == lang::Language::English);
}

}  // namespace rm::editor
