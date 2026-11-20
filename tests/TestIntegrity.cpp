// 整合性チェック (editor/app/Integrity) のテスト: ファイル一覧と、その比較。

#include "Test.h"

#include "editor/app/EngineKey.h"
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
    // エンジンのフォルダーの外へのパスや、短いハッシュは出さない。
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
    // space3d の入っている plugin.json が違う (古いコピー)。game_production は一覧にファイルが無い。
    auto stale = StalePlugins(Sample(), {space, game}, [&](const PluginId&, const std::string& p) -> std::optional<std::string> {
        return p == "plugin.json" ? std::optional<std::string>(kA) : std::nullopt;
    });
    CHECK(stale.size() == 1 && stale[0] == space);
    stale = StalePlugins(Sample(), {space}, [&](const PluginId&, const std::string&) { return std::optional<std::string>(kB); });
    CHECK(stale.empty());
    stale = StalePlugins(Sample(), {space}, [&](const PluginId&, const std::string&) { return std::optional<std::string>(); });
    CHECK(stale.size() == 1);  // 入っているコピーに無い
}

RM_TEST(Integrity_EngineChanged) {
    CHECK(!EngineChanged(kA, kA));                // 前回の起動と同じエンジン: 見るものは無い
    CHECK(EngineChanged(kB, kA));                 // 新しいファイル一覧
    CHECK(EngineChanged(kA, ""));                 // まだ見ていない
    CHECK(EngineChanged(std::nullopt, kA));       // 一覧無し: いつも (plugin.json だけ、軽い)
}

RM_TEST(Integrity_PluginsToRefresh) {
    const PluginId space{"rm_engine", "space3d"}, game{"rm_engine", "game_production"};
    int manifestLooks = 0;
    auto differs = [&](const PluginId& id) {
        ++manifestLooks;
        return id == game;
    };
    // 一覧がある場合: 中のファイルで決める (space3d の古い plugin.json)。plugin.json だけは読まない。
    auto old = [&](const PluginId&, const std::string&) { return std::optional<std::string>(kA); };
    auto got = PluginsToRefresh(Sample(), {space, game}, old, differs);
    CHECK(got.size() == 1 && got[0] == space);
    CHECK(manifestLooks == 0);
    auto same = [&](const PluginId&, const std::string&) { return std::optional<std::string>(kB); };
    CHECK(PluginsToRefresh(Sample(), {space, game}, same, differs).empty());
    // 一覧が無い場合: それぞれの plugin.json。
    got = PluginsToRefresh(std::nullopt, {space, game}, same, differs);
    CHECK(got.size() == 1 && got[0] == game);
    CHECK(manifestLooks == 2);
    CHECK(PluginsToRefresh(std::nullopt, {}, same, differs).empty());  // 削除されたプラグインは見ない
}

RM_TEST(EngineKey_OneFolderPerEngine) {
    const std::string k = rm::editor::EngineKey(LR"(C:\RM\build\bin\x64\Release)");
    CHECK(k.starts_with("release-") && k.size() == std::string("release-").size() + 16);
    // 書き方が違っても同じフォルダーは同じエンジン
    CHECK(rm::editor::EngineKey(LR"(c:\rm\BUILD\bin\x64\Release\\)") == k);
    CHECK(rm::editor::EngineKey(LR"(C:/RM/build/bin/x64/Release)") == k);
    CHECK(rm::editor::EngineKey(LR"(C:\RM\build\bin\x64\.\Release)") == k);
    // 別のビルド (Debug や worktree) は別のエンジン
    CHECK(rm::editor::EngineKey(LR"(C:\RM\build\bin\x64\Debug)") != k);
    CHECK(rm::editor::EngineKey(LR"(C:\RM\.claude\worktrees\a\build\bin\x64\Release)") != k);
    // ASCII の文字が無い名前でも、フォルダー名は作れる
    CHECK(rm::editor::EngineKey(LR"(D:\エンジン)").starts_with("engine-"));
}
