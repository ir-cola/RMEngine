// Tests for plugin IDs (core/PluginId.h) and plugin.json / resolution (editor/plugin/PluginManifest).

#include "Test.h"

#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <algorithm>

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

namespace {

PluginManifest Manifest(const char* name, std::vector<PluginId> requirements = {}) {
    PluginManifest m;
    m.id = {"rm_engine", name};
    m.displayName = name;
    m.requirements = std::move(requirements);
    return m;
}

PluginId Id(const char* name) { return {"rm_engine", name}; }

}  // namespace

RM_TEST(Plugins_InstallPlan) {
    // game_ui -> game_production -> space3d ; physics -> game_production
    const std::vector<PluginManifest> available = {
        Manifest("game_ui", {Id("game_production")}),
        Manifest("game_production", {Id("space3d")}),
        Manifest("space3d"),
        Manifest("physics", {Id("game_production"), Id("cloth_sim")}),  // cloth_sim is not in this engine
    };
    std::vector<PluginId> unavailable;
    // nothing installed: the whole tree, requirements first
    auto plan = InstallPlan(Id("game_ui"), available, {}, &unavailable);
    CHECK(plan == std::vector<PluginId>({Id("space3d"), Id("game_production"), Id("game_ui")}));
    CHECK(unavailable.empty());
    // already installed ones are not copied again
    plan = InstallPlan(Id("game_ui"), available, {Id("space3d")}, &unavailable);
    CHECK(plan == std::vector<PluginId>({Id("game_production"), Id("game_ui")}));
    // the plugin is installed, but a requirement two steps down is missing: found through the ready ones
    plan = InstallPlan(Id("game_ui"), available, {Id("game_ui"), Id("game_production")}, &unavailable);
    CHECK(plan == std::vector<PluginId>({Id("space3d")}));
    // everything ready: nothing to do
    plan = InstallPlan(Id("game_ui"), available, {Id("game_ui"), Id("game_production"), Id("space3d")}, &unavailable);
    CHECK(plan.empty());
    // a requirement this engine does not have
    plan = InstallPlan(Id("physics"), available, {}, &unavailable);
    CHECK(unavailable == std::vector<PluginId>({Id("cloth_sim")}));
    CHECK(plan.back() == Id("physics"));
    // a circle does not loop forever
    const std::vector<PluginManifest> loop = {Manifest("alpha", {Id("bravo")}), Manifest("bravo", {Id("alpha")})};
    plan = InstallPlan(Id("alpha"), loop, {}, nullptr);
    CHECK(plan.size() == 2);
}

RM_TEST(Plugins_Dependents) {
    const std::vector<PluginManifest> installed = {
        Manifest("space3d"),
        Manifest("render3d", {Id("space3d")}),
        Manifest("shader_graph", {Id("render3d")}),
        Manifest("particles", {Id("shader_graph"), Id("render3d")}),
        Manifest("music_daw"),
    };
    const auto d = Dependents(Id("space3d"), installed);
    CHECK(d.size() == 3);
    CHECK(d[0].id == Id("render3d") && !d[0].via);
    CHECK(d[1].id == Id("shader_graph") && d[1].via == Id("render3d"));
    CHECK(d[2].id == Id("particles") && d[2].via.has_value());
    CHECK(Dependents(Id("music_daw"), installed).empty());
}

RM_TEST(Plugins_NotInstalled) {
    std::vector<PluginCandidate> c;
    c.push_back(Candidate("rm_engine", "game_ui", {Id("game_production")}));
    PluginCandidate notInstalled = Candidate("rm_engine", "game_production");
    notInstalled.installed = false;
    c.push_back(notInstalled);
    const PluginResolution r = ResolvePlugins(c);
    CHECK(r.statuses[1].state == PluginState::NotInstalled);
    CHECK(r.statuses[0].state == PluginState::RequirementDisabled);
    CHECK(r.statuses[0].reason.find("インストールされていません") != std::string::npos);
    CHECK(LacksRequirement(r.statuses[0].state));
    CHECK(r.order.empty());
}

namespace {

// game_production needs a connector; game_x_3d connects it with space3d.
std::vector<PluginManifest> ConnectorWorld() {
    PluginManifest game = Manifest("game_production");
    game.needsConnector = true;
    PluginManifest x3d = Manifest("game_x_3d", {Id("game_production"), Id("space3d")});
    x3d.connectorFor = Id("game_production");
    return {game, Manifest("space3d"), x3d, Manifest("game_ui", {Id("game_production")})};
}

PluginCandidate FromManifest(const PluginManifest& m, bool installed = true) {
    PluginCandidate c;
    c.id = m.id;
    c.installed = installed;
    c.manifestFound = true;
    c.manifest = m;
    return c;
}

}  // namespace

RM_TEST(PluginManifest_ConnectorKeys) {
    std::string err;
    auto ok = json::Parse(R"({"author":"rm_engine","name":"game_x_3d","connectorFor":{"author":"rm_engine","name":"game_production"},
                             "requires":[{"author":"rm_engine","name":"game_production"},{"author":"rm_engine","name":"space3d"}]})");
    auto m = ParsePluginManifest(*ok, &err);
    CHECK(m && m->connectorFor == Id("game_production") && !m->needsConnector);
    auto needs = json::Parse(R"({"author":"rm_engine","name":"game_production","needsConnector":true})");
    auto g = ParsePluginManifest(*needs, &err);
    CHECK(g && g->needsConnector && !g->connectorFor);
    // The target must also be required (so it is enabled first), never itself, never a connector that needs one.
    auto notRequired = json::Parse(R"({"author":"rm_engine","name":"game_x_3d","connectorFor":{"author":"rm_engine","name":"game_production"}})");
    CHECK(!ParsePluginManifest(*notRequired, &err));
    auto self = json::Parse(R"({"author":"rm_engine","name":"game_x_3d","connectorFor":{"author":"rm_engine","name":"game_x_3d"},
                               "requires":[{"author":"rm_engine","name":"game_x_3d"}]})");
    CHECK(!ParsePluginManifest(*self, &err));
    auto notBool = json::Parse(R"({"author":"rm_engine","name":"game_production","needsConnector":"yes"})");
    CHECK(!ParsePluginManifest(*notBool, &err));
}

RM_TEST(Plugins_ConnectorDecidesEnabled) {
    const auto w = ConnectorWorld();
    // Everything installed: all enabled, the connector after both of its sides.
    std::vector<PluginCandidate> c;
    for (const auto& m : w) c.push_back(FromManifest(m));
    auto r = ResolvePlugins(c);
    for (const auto& s : r.statuses) CHECK(s.state == PluginState::Enabled);
    CHECK(r.order.size() == 4);
    auto pos = [&](size_t idx) { return std::find(r.order.begin(), r.order.end(), idx) - r.order.begin(); };
    CHECK(pos(2) > pos(0) && pos(2) > pos(1));

    // 3D 空間 not installed: the connector cannot work, so game production is off - and game_ui with it.
    c[1].installed = false;
    r = ResolvePlugins(c);
    CHECK(r.statuses[2].state == PluginState::RequirementDisabled);
    CHECK(r.statuses[0].state == PluginState::NoConnector);
    CHECK(r.statuses[0].reason.find("game_x_3d") != std::string::npos);
    CHECK(LacksRequirement(r.statuses[0].state));
    CHECK(r.statuses[3].state == PluginState::RequirementDisabled);
    CHECK(r.order.empty());

    // No connector in the engine at all.
    std::vector<PluginCandidate> alone = {FromManifest(w[0])};
    r = ResolvePlugins(alone);
    CHECK(r.statuses[0].state == PluginState::NoConnector);
}

RM_TEST(Plugins_InstallPlanBringsConnectors) {
    const auto w = ConnectorWorld();
    // Game production alone brings a space and the connector.
    auto plan = InstallPlan(Id("game_production"), w, {}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("game_production"), Id("space3d"), Id("game_x_3d")}));
    // Installed, but without a connector ("前提をインストール"): only what is missing.
    plan = InstallPlan(Id("game_production"), w, {Id("game_production")}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("space3d"), Id("game_x_3d")}));
    // 3D 空間 when game production is there: the connector comes along by itself.
    plan = InstallPlan(Id("space3d"), w, {Id("game_production")}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("space3d"), Id("game_x_3d")}));
    // 3D 空間 alone: no connector (game production is not there).
    plan = InstallPlan(Id("space3d"), w, {}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("space3d")}));
    // Everything there: nothing.
    CHECK(InstallPlan(Id("game_ui"), w, {Id("game_production"), Id("space3d"), Id("game_x_3d"), Id("game_ui")}, nullptr).empty());
}

RM_TEST(Plugins_DependentsThroughConnector) {
    const auto w = ConnectorWorld();
    // Removing 3D 空間: the connector goes, so game production (its last connector) and game_ui go too.
    const auto d = Dependents(Id("space3d"), w);
    CHECK(d.size() == 3);
    CHECK(d[0].id == Id("game_x_3d") && !d[0].via);
    CHECK(d[1].id == Id("game_production") && d[1].via == Id("game_x_3d"));
    CHECK(d[2].id == Id("game_ui") && d[2].via == Id("game_production"));
    // With a second connector (2D) still there, game production keeps working.
    auto more = w;
    PluginManifest x2d = Manifest("game_x_2d", {Id("game_production"), Id("space2d")});
    x2d.connectorFor = Id("game_production");
    more.push_back(Manifest("space2d"));
    more.push_back(x2d);
    const auto d2 = Dependents(Id("space3d"), more);
    CHECK(d2.size() == 1 && d2[0].id == Id("game_x_3d"));
}

RM_TEST(Plugins_InstallPlanFindsWhatAnInstalledConnectorLacks) {
    // A newer engine: the connector now also needs render3d, which is not installed.
    auto w = ConnectorWorld();
    w[2].requirements.push_back(Id("render3d"));
    w.push_back(Manifest("render3d", {Id("space3d")}));
    const std::vector<PluginId> ready = {Id("game_production"), Id("space3d"), Id("game_x_3d")};
    auto plan = InstallPlan(Id("game_production"), w, ready, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("render3d")}));
    // Everything there: nothing.
    auto all = ready;
    all.push_back(Id("render3d"));
    CHECK(InstallPlan(Id("game_production"), w, all, nullptr).empty());
}
