// Tests for plugin IDs (core/PluginId.h) and plugin.json / resolution (editor/plugin/PluginManifest).

#include "Test.h"

#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

using namespace rm;
using namespace rm::editor;

namespace {

// The official ID compiles (a wrong one would not: PluginIdLiteral is consteval).
constexpr PluginIdLiteral kGame{"rm_engine", "game_production"};
static_assert(PluginIdProblem("rm_engine", "game_production") == nullptr);
static_assert(PluginIdProblem("rm", "render3d") != nullptr);

PluginCandidate Candidate(const char* author, const char* name, std::vector<PluginId> requirements = {},
                          const char* manifestAuthor = nullptr) {
    PluginCandidate c;
    c.id = {author, name};
    c.manifestFound = true;
    PluginManifest m;
    m.id = {manifestAuthor ? manifestAuthor : author, name};
    m.displayName = name;
    m.requirements = std::move(requirements);
    c.manifest = std::move(m);
    return c;
}

}  // namespace

RM_TEST(PluginId_Rules) {
    CHECK(PluginIdProblem("rm_engine", "render3d") == nullptr);
    CHECK(PluginIdProblem("akatuki", "toon_shader2") == nullptr);
    CHECK(PluginIdProblem("ab", "render3d") != nullptr);                     // author too short
    CHECK(PluginIdProblem("abcdefghijklmnopqrstu", "render3d") != nullptr);  // author 21 chars
    CHECK(PluginIdProblem("rm_engine", "ui") != nullptr);                     // name too short
    CHECK(PluginIdProblem("rm_engine", std::string(65, 'a')) != nullptr);     // name too long
    CHECK(PluginIdProblem("rm_engine", std::string(64, 'a')) == nullptr);
    CHECK(PluginIdProblem("Rm_engine", "render3d") != nullptr);  // upper case
    CHECK(PluginIdProblem("7studio", "render3d") != nullptr);    // starts with a digit
    CHECK(PluginIdProblem("rm_engine", "render-3d") != nullptr);  // '-' is not allowed
    CHECK(PluginIdProblem("rm_engine", "render.3d") != nullptr);  // '.' neither
    CHECK(PluginIdProblem("rm_engine", "render 3d") != nullptr);
    CHECK(PluginIdProblem("rm__engine", "render3d") != nullptr);  // "__"
    CHECK(PluginIdProblem("rm_engine", "render3d_") != nullptr);  // trailing '_'
    CHECK(PluginIdProblem("con", "render3d") != nullptr);         // Windows device names
    CHECK(PluginIdProblem("lpt3", "render3d") != nullptr);
    CHECK(PluginIdProblem("com0", "render3d") == nullptr);  // only com1-com9 are reserved
    CHECK(PluginIdProblem("console", "render3d") == nullptr);
    CHECK(kGame.Id() == (PluginId{"rm_engine", "game_production"}));
    CHECK(kGame.Id().Text() == "rm_engine / game_production");
}

RM_TEST(PluginManifest_Parse) {
    auto v = json::Parse(R"({"author":"rm_engine","name":"game_ui","displayName":"ゲーム UI",
                             "requires":[{"author":"rm_engine","name":"game_production"}],"unknownKey":1})");
    CHECK(v.has_value());
    std::string err;
    auto m = ParsePluginManifest(*v, &err);
    CHECK(m.has_value());
    CHECK(m->id == (PluginId{"rm_engine", "game_ui"}));
    CHECK(m->displayName == "ゲーム UI");
    CHECK(m->requirements.size() == 1 && m->requirements[0].name == "game_production");

    // one value of an ID alone is an error (never guessed)
    auto half = json::Parse(R"({"author":"rm_engine","name":"game_ui","requires":[{"name":"game_production"}]})");
    CHECK(!ParsePluginManifest(*half, &err).has_value());
    CHECK(!err.empty());
    auto badId = json::Parse(R"({"author":"RM","name":"game_ui"})");
    CHECK(!ParsePluginManifest(*badId, &err).has_value());
    // no displayName -> the name; no requirements -> none
    auto plain = json::Parse(R"({"author":"rm_engine","name":"game_ui"})");
    auto pm = ParsePluginManifest(*plain, &err);
    CHECK(pm && pm->displayName == "game_ui" && pm->requirements.empty());
}

RM_TEST(PluginIdList_Parse) {
    std::string err;
    auto empty = ParsePluginIdList(json::Value(), &err);
    CHECK(empty && empty->empty());
    auto notArray = ParsePluginIdList(json::Value("rm_engine"), &err);
    CHECK(!notArray.has_value());
    auto strings = json::Parse(R"(["rm_engine.game_production"])");
    CHECK(!ParsePluginIdList(*strings, &err).has_value());
}

RM_TEST(Plugins_ResolveOrderAndReasons) {
    const PluginId game{"rm_engine", "game_production"};
    const PluginId space{"rm_engine", "space3d"};
    std::vector<PluginCandidate> c;
    c.push_back(Candidate("rm_engine", "game_ui", {game}));  // listed before what it needs
    c.push_back(Candidate("rm_engine", "game_production"));
    c.push_back(Candidate("rm_engine", "physics", {game, space}));  // space3d is not built in
    c.push_back(Candidate("rm_engine", "cloth", {{"rm_engine", "physics"}}));
    c.push_back(Candidate("rm_engine", "mismatch", {}, "someone"));
    PluginCandidate noFile;
    noFile.id = {"rm_engine", "no_manifest"};
    c.push_back(noFile);

    const PluginResolution r = ResolvePlugins(c);
    CHECK(r.statuses[0].state == PluginState::Enabled);
    CHECK(r.statuses[1].state == PluginState::Enabled);
    CHECK(r.statuses[2].state == PluginState::MissingRequirement);
    CHECK(r.statuses[2].reason.find("space3d") != std::string::npos);
    CHECK(r.statuses[3].state == PluginState::RequirementDisabled);
    CHECK(r.statuses[4].state == PluginState::IdMismatch);
    CHECK(r.statuses[5].state == PluginState::NoManifest);
    // requirements first: game_production before game_ui
    CHECK(r.order.size() == 2);
    CHECK(r.order[0] == 1 && r.order[1] == 0);
}

RM_TEST(Plugins_CycleAndDuplicate) {
    std::vector<PluginCandidate> c;
    c.push_back(Candidate("rm_engine", "alpha", {{"rm_engine", "bravo"}}));
    c.push_back(Candidate("rm_engine", "bravo", {{"rm_engine", "alpha"}}));
    c.push_back(Candidate("rm_engine", "charlie"));
    c.push_back(Candidate("rm_engine", "charlie"));
    const PluginResolution r = ResolvePlugins(c);
    CHECK(r.statuses[0].state != PluginState::Enabled);
    CHECK(r.statuses[1].state != PluginState::Enabled);
    CHECK(r.statuses[0].state == PluginState::Cycle || r.statuses[1].state == PluginState::Cycle);
    CHECK(r.statuses[2].state == PluginState::BadManifest);
    CHECK(r.statuses[3].state == PluginState::BadManifest);
    CHECK(r.order.empty());
}
