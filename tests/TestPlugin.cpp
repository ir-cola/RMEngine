// プラグイン ID (core/PluginId.h) と plugin.json / 解決 (editor/plugin/PluginManifest) のテスト。

#include "Test.h"

#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <algorithm>

using namespace rm;
using namespace rm::editor;

namespace {

// 公式の ID はコンパイルできる (間違ったものはできない: PluginIdLiteral は consteval)。
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
    CHECK(PluginIdProblem("ab", "render3d") != nullptr);                     // 作者名が短すぎる
    CHECK(PluginIdProblem("abcdefghijklmnopqrstu", "render3d") != nullptr);  // 作者名が 21 文字
    CHECK(PluginIdProblem("rm_engine", "ui") != nullptr);                     // 名前が短すぎる
    CHECK(PluginIdProblem("rm_engine", std::string(65, 'a')) != nullptr);     // 名前が長すぎる
    CHECK(PluginIdProblem("rm_engine", std::string(64, 'a')) == nullptr);
    CHECK(PluginIdProblem("Rm_engine", "render3d") != nullptr);  // 大文字
    CHECK(PluginIdProblem("7studio", "render3d") != nullptr);    // 数字で始まる
    CHECK(PluginIdProblem("rm_engine", "render-3d") != nullptr);  // '-' は使えない
    CHECK(PluginIdProblem("rm_engine", "render.3d") != nullptr);  // '.' も使えない
    CHECK(PluginIdProblem("rm_engine", "render 3d") != nullptr);
    CHECK(PluginIdProblem("rm__engine", "render3d") != nullptr);  // "__"
    CHECK(PluginIdProblem("rm_engine", "render3d_") != nullptr);  // 末尾の '_'
    CHECK(PluginIdProblem("con", "render3d") != nullptr);         // Windows のデバイス名
    CHECK(PluginIdProblem("lpt3", "render3d") != nullptr);
    CHECK(PluginIdProblem("com0", "render3d") == nullptr);  // 予約されているのは com1-com9 だけ
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

    // ID の片方の値だけでは、エラー (推測はしない)
    auto half = json::Parse(R"({"author":"rm_engine","name":"game_ui","requires":[{"name":"game_production"}]})");
    CHECK(!ParsePluginManifest(*half, &err).has_value());
    CHECK(!err.empty());
    auto badId = json::Parse(R"({"author":"RM","name":"game_ui"})");
    CHECK(!ParsePluginManifest(*badId, &err).has_value());
    // displayName が無い -> 名前。requirements が無い -> 無し
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
    c.push_back(Candidate("rm_engine", "game_ui", {game}));  // 必要とするものより先に並ぶ
    c.push_back(Candidate("rm_engine", "game_production"));
    c.push_back(Candidate("rm_engine", "physics", {game, space}));  // space3d は組み込みではない
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
    // 必要とするものが先: game_production が game_ui より前
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
        Manifest("physics", {Id("game_production"), Id("cloth_sim")}),  // cloth_sim はこのエンジンに無い
    };
    std::vector<PluginId> unavailable;
    // 何も入っていない: 木全体を、必要とするものから先に
    auto plan = InstallPlan(Id("game_ui"), available, {}, &unavailable);
    CHECK(plan == std::vector<PluginId>({Id("space3d"), Id("game_production"), Id("game_ui")}));
    CHECK(unavailable.empty());
    // 入っているものは、もう一度コピーしない
    plan = InstallPlan(Id("game_ui"), available, {Id("space3d")}, &unavailable);
    CHECK(plan == std::vector<PluginId>({Id("game_production"), Id("game_ui")}));
    // プラグインは入っているが、2 段下の必要なものが無い: 準備のできたものをたどって見つける
    plan = InstallPlan(Id("game_ui"), available, {Id("game_ui"), Id("game_production")}, &unavailable);
    CHECK(plan == std::vector<PluginId>({Id("space3d")}));
    // すべて準備できている: やることは無い
    plan = InstallPlan(Id("game_ui"), available, {Id("game_ui"), Id("game_production"), Id("space3d")}, &unavailable);
    CHECK(plan.empty());
    // このエンジンに無い必要なもの
    plan = InstallPlan(Id("physics"), available, {}, &unavailable);
    CHECK(unavailable == std::vector<PluginId>({Id("cloth_sim")}));
    CHECK(plan.back() == Id("physics"));
    // 輪になっていても、無限にはループしない
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

// game_production はコネクターが要る。game_x_3d がそれを space3d とつなぐ。
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
    // 対象も requires に入っていること (先に有効になるように)。自分自身や、コネクターを要するコネクターは不可。
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
    // すべて入っている: すべて有効。コネクターは両側の後。
    std::vector<PluginCandidate> c;
    for (const auto& m : w) c.push_back(FromManifest(m));
    auto r = ResolvePlugins(c);
    for (const auto& s : r.statuses) CHECK(s.state == PluginState::Enabled);
    CHECK(r.order.size() == 4);
    auto pos = [&](size_t idx) { return std::find(r.order.begin(), r.order.end(), idx) - r.order.begin(); };
    CHECK(pos(2) > pos(0) && pos(2) > pos(1));

    // 3D 空間 が入っていない: コネクターが動けないので、ゲーム制作は無効 - game_ui もいっしょに。
    c[1].installed = false;
    r = ResolvePlugins(c);
    CHECK(r.statuses[2].state == PluginState::RequirementDisabled);
    CHECK(r.statuses[0].state == PluginState::NoConnector);
    CHECK(r.statuses[0].reason.find("game_x_3d") != std::string::npos);
    CHECK(LacksRequirement(r.statuses[0].state));
    CHECK(r.statuses[3].state == PluginState::RequirementDisabled);
    CHECK(r.order.empty());

    // エンジンにコネクターがまったく無い。
    std::vector<PluginCandidate> alone = {FromManifest(w[0])};
    r = ResolvePlugins(alone);
    CHECK(r.statuses[0].state == PluginState::NoConnector);
}

RM_TEST(Plugins_InstallPlanBringsConnectors) {
    const auto w = ConnectorWorld();
    // ゲーム制作だけで、空間とコネクターが付いてくる。
    auto plan = InstallPlan(Id("game_production"), w, {}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("game_production"), Id("space3d"), Id("game_x_3d")}));
    // 入っているがコネクターが無い ("前提をインストール"): 足りないものだけ。
    plan = InstallPlan(Id("game_production"), w, {Id("game_production")}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("space3d"), Id("game_x_3d")}));
    // ゲーム制作がある場合の 3D 空間: コネクターはひとりでに付いてくる。
    plan = InstallPlan(Id("space3d"), w, {Id("game_production")}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("space3d"), Id("game_x_3d")}));
    // 3D 空間 だけ: コネクターは無し (ゲーム制作が無い)。
    plan = InstallPlan(Id("space3d"), w, {}, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("space3d")}));
    // すべてある: 何もしない。
    CHECK(InstallPlan(Id("game_ui"), w, {Id("game_production"), Id("space3d"), Id("game_x_3d"), Id("game_ui")}, nullptr).empty());
}

// 新しいエンジンが、以前に入れたプラグインのために足したコネクター: 起動時に持ってくる。
RM_TEST(Plugins_ConnectorsToBring) {
    const auto w = ConnectorWorld();
    // 両側が入っていて、コネクターは無い: 付いてくる。
    const std::vector<PluginManifest> both = {w[0], w[1]};
    CHECK(ConnectorsToBring(both, w) == std::vector<PluginId>({Id("game_x_3d")}));
    // もう入っている、または片側が無い: 何もしない。
    CHECK(ConnectorsToBring(w, w).empty());
    CHECK(ConnectorsToBring({w[0]}, w).empty());
    // コネクターをつなぐコネクター (ゲーム制作 × 3D × 物理): つなぐ相手に付いていく。
    PluginManifest phys = Manifest("physics");
    PluginManifest x3dPhys = Manifest("game_x_3d_x_physics", {Id("game_x_3d"), Id("physics")});
    x3dPhys.connectorFor = Id("game_x_3d");
    std::vector<PluginManifest> all = w;
    all.push_back(phys);
    all.push_back(x3dPhys);
    CHECK(ConnectorsToBring({w[0], w[1], phys}, all) == std::vector<PluginId>({Id("game_x_3d"), Id("game_x_3d_x_physics")}));
}

// 前のエンジンが写したのに、このエンジンにはもう無いプラグイン (やめたつなぎ): 起動時に消す。
RM_TEST(Plugins_RetiredCopies) {
    const std::vector<PluginId> engine = {Id("game_x_3d"), Id("weather_x_physics")};
    const std::vector<InstalledCopy> installed = {
        {Id("game_x_3d"), false},             // エンジンにある: 残す
        {Id("game_x_3d_x_physics"), false},   // やめた: 消す
        {Id("downloaded_pack"), true},        // ダウンロードした物: 残す
        {{"someone", "old_tool"}, false},     // (作者は問わない)
    };
    CHECK(RetiredCopies(installed, engine) == std::vector<PluginId>({Id("game_x_3d_x_physics"), PluginId{"someone", "old_tool"}}));
    CHECK(RetiredCopies({}, engine).empty());
}

RM_TEST(Plugins_DependentsThroughConnector) {
    const auto w = ConnectorWorld();
    // 3D 空間 を外す: コネクターが消え、ゲーム制作 (その最後のコネクター) と game_ui も消える。
    const auto d = Dependents(Id("space3d"), w);
    CHECK(d.size() == 3);
    CHECK(d[0].id == Id("game_x_3d") && !d[0].via);
    CHECK(d[1].id == Id("game_production") && d[1].via == Id("game_x_3d"));
    CHECK(d[2].id == Id("game_ui") && d[2].via == Id("game_production"));
    // 2 つ目のコネクター (2D) がまだあれば、ゲーム制作は動き続ける。
    auto more = w;
    PluginManifest x2d = Manifest("game_x_2d", {Id("game_production"), Id("space2d")});
    x2d.connectorFor = Id("game_production");
    more.push_back(Manifest("space2d"));
    more.push_back(x2d);
    const auto d2 = Dependents(Id("space3d"), more);
    CHECK(d2.size() == 1 && d2[0].id == Id("game_x_3d"));
}

RM_TEST(Plugins_InstallPlanFindsWhatAnInstalledConnectorLacks) {
    // 新しいエンジン: コネクターが physics_lib も要るようになったが、入っていない。
    auto w = ConnectorWorld();
    w[2].requirements.push_back(Id("physics_lib"));
    w.push_back(Manifest("physics_lib", {Id("space3d")}));
    const std::vector<PluginId> ready = {Id("game_production"), Id("space3d"), Id("game_x_3d")};
    auto plan = InstallPlan(Id("game_production"), w, ready, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("physics_lib")}));
    // すべてある: 何もしない。
    auto all = ready;
    all.push_back(Id("physics_lib"));
    CHECK(InstallPlan(Id("game_production"), w, all, nullptr).empty());
}

RM_TEST(Plugins_ReadyPlugins_OlderCopiesAreCopiedAgain) {
    const auto engine = ConnectorWorld();
    // 古いエンジンから入れた: コネクターは、このエンジンが外したプラグインをまだ必要とし、
    // ゲーム制作にはコネクターの規則が無い。
    auto installed = engine;
    installed[2].requirements.push_back(Id("render3d"));
    installed[0].needsConnector = false;
    installed.push_back(Manifest("someone_else"));  // このエンジンに無い: そのまま扱う
    const auto ready = ReadyPlugins(installed, engine);
    CHECK(ready == std::vector<PluginId>({Id("space3d"), Id("game_ui"), Id("someone_else")}));
    // だからゲーム制作を入れると、それとそのコネクターをもう一度コピーする。
    const auto plan = InstallPlan(Id("game_production"), engine, ready, nullptr);
    CHECK(plan == std::vector<PluginId>({Id("game_production"), Id("game_x_3d")}));
}

RM_TEST(Plugin_FirstStartInstalls) {
    const PluginId a{"rm_engine", "game_production"}, b{"rm_engine", "space3d"}, c{"rm_engine", "space2d"};
    // 何も入っていなかった新しいエンジン: すべて。
    CHECK(FirstStartInstalls({a, b, c}, {}) == std::vector<PluginId>({a, b, c}));
    // 共有フォルダーにユーザーが入れていたものを引き継ぐ (space2d はそこで外された)。順番はエンジンのもの。
    // このエンジンに無いプラグインは求めない。
    const PluginId gone{"someone", "old"};
    CHECK(FirstStartInstalls({a, b, c}, {b, gone, a}) == std::vector<PluginId>({a, b}));
}
