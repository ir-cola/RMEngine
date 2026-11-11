// Tests for the integrity check (editor/app/Integrity): the file list and the comparison.

#include "Test.h"

#include "editor/app/Integrity.h"

#include <map>

using namespace rm;
using namespace rm::editor::integrity;

namespace {

const std::string kA(64, 'a'), kB(64, 'b'), kC(64, 'c');

std::vector<FileEntry> Sample() {
    return {{"RM_Engine.exe", 10, kA}, {"Plugins/rm_engine/space3d/plugin.json", 2, kB}, {"Lang/en.json", 3, kC}};
}

}  // namespace

RM_TEST(Integrity_ParseFileList) {
    std::string err;
    auto ok = ParseFileList(*json::Parse(R"({"kind":"rm_engine.files","format":1,"files":[{"path":"RM_Engine.exe","size":5,"sha256":")" +
                                           std::string(64, 'A') + R"("}]})"),
                            &err);
    CHECK(ok && ok->size() == 1 && (*ok)[0].sha256 == std::string(64, 'a') && (*ok)[0].size == 5);
    CHECK(!ParseFileList(*json::Parse(R"({"kind":"other","files":[]})"), &err));
    // Never a path out of the engine's folder, never a short hash.
    CHECK(!ParseFileList(*json::Parse(R"({"kind":"rm_engine.files","files":[{"path":"../x","sha256":")" + kA + R"("}]})"), &err));
    CHECK(!ParseFileList(*json::Parse(R"({"kind":"rm_engine.files","files":[{"path":"C:/x","sha256":")" + kA + R"("}]})"), &err));
    CHECK(!ParseFileList(*json::Parse(R"({"kind":"rm_engine.files","files":[{"path":"x","sha256":"abc"}]})"), &err));
}

RM_TEST(Integrity_CheckFiles_MissingAndChanged) {
    std::map<std::string, std::string> disk = {{"RM_Engine.exe", kA}, {"Plugins/rm_engine/space3d/plugin.json", kC}};
    auto problems = CheckFiles(Sample(), [&](const std::string& p) -> std::optional<std::string> {
        const auto it = disk.find(p);
        return it == disk.end() ? std::nullopt : std::optional<std::string>(it->second);
    });
    CHECK(problems.size() == 2);
    CHECK(problems[0].path == "Plugins/rm_engine/space3d/plugin.json" && problems[0].problem == Problem::Changed);
    CHECK(problems[1].path == "Lang/en.json" && problems[1].problem == Problem::Missing);
    disk["Plugins/rm_engine/space3d/plugin.json"] = kB;
    disk["Lang/en.json"] = kC;
    CHECK(CheckFiles(Sample(), [&](const std::string& p) { return std::optional<std::string>(disk[p]); }).empty());
}

RM_TEST(Integrity_StalePlugins) {
    const PluginId space{"rm_engine", "space3d"}, game{"rm_engine", "game_production"};
    // space3d's installed plugin.json differs (an old copy); game_production has no files in the list.
    auto stale = StalePlugins(Sample(), {space, game}, [&](const PluginId&, const std::string& p) -> std::optional<std::string> {
        return p == "plugin.json" ? std::optional<std::string>(kA) : std::nullopt;
    });
    CHECK(stale.size() == 1 && stale[0] == space);
    stale = StalePlugins(Sample(), {space}, [&](const PluginId&, const std::string&) { return std::optional<std::string>(kB); });
    CHECK(stale.empty());
    stale = StalePlugins(Sample(), {space}, [&](const PluginId&, const std::string&) { return std::optional<std::string>(); });
    CHECK(stale.size() == 1);  // missing in the installed copy
}

RM_TEST(Integrity_EngineChanged) {
    CHECK(!EngineChanged(kA, kA));                // the same engine as at the last start: nothing to look at
    CHECK(EngineChanged(kB, kA));                 // a new file list
    CHECK(EngineChanged(kA, ""));                 // never looked at yet
    CHECK(EngineChanged(std::nullopt, kA));       // no list: always (only plugin.json, cheap)
}

RM_TEST(Integrity_PluginsToRefresh) {
    const PluginId space{"rm_engine", "space3d"}, game{"rm_engine", "game_production"};
    int manifestLooks = 0;
    auto differs = [&](const PluginId& id) {
        ++manifestLooks;
        return id == game;
    };
    // With the list: the files in it decide (an old plugin.json of space3d), plugin.json alone is not read.
    auto old = [&](const PluginId&, const std::string&) { return std::optional<std::string>(kA); };
    auto got = PluginsToRefresh(Sample(), {space, game}, old, differs);
    CHECK(got.size() == 1 && got[0] == space);
    CHECK(manifestLooks == 0);
    auto same = [&](const PluginId&, const std::string&) { return std::optional<std::string>(kB); };
    CHECK(PluginsToRefresh(Sample(), {space, game}, same, differs).empty());
    // Without the list: plugin.json of each.
    got = PluginsToRefresh(std::nullopt, {space, game}, same, differs);
    CHECK(got.size() == 1 && got[0] == game);
    CHECK(manifestLooks == 2);
    CHECK(PluginsToRefresh(std::nullopt, {}, same, differs).empty());  // removed plugins are not looked at
}
