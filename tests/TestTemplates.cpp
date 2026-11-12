// Tests for the project templates (editor/project/Templates): reading template.json, the tree, what
// a node needs installed, the new project's .rmpf and Layout.json, the "ソフトで絞る" groups - and the
// templates shipped in plugins/ (readable, their first scenes load without a warning).

#include "Test.h"

#include "editor/project/Project.h"
#include "editor/project/Templates.h"

#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "core/asset/AssetIndex.h"
#include "rm_engine/game_x_2d/runtime/Camera2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

using namespace rm;
using namespace rm::editor;

namespace {

PluginId Id(const char* name) { return {"rm_engine", name}; }

PluginManifest Manifest(const char* name, std::vector<PluginId> requirements = {}) {
    PluginManifest m;
    m.id = Id(name);
    m.displayName = name;
    m.requirements = std::move(requirements);
    return m;
}

// game_production needs a connector; one for 3D and one for 2D; a theme pack nobody needs.
std::vector<PluginManifest> World() {
    PluginManifest game = Manifest("game_production");
    game.needsConnector = true;
    PluginManifest x3d = Manifest("game_x_3d", {Id("game_production"), Id("space3d")});
    x3d.connectorFor = Id("game_production");
    PluginManifest x2d = Manifest("game_x_2d", {Id("game_production"), Id("space2d")});
    x2d.connectorFor = Id("game_production");
    return {game, Manifest("space3d"), Manifest("space2d"), x3d, x2d, Manifest("theme_colors")};
}

json::Value Parse(const char* text) {
    auto v = json::Parse(text);
    CHECK(v.has_value());
    return v ? std::move(*v) : json::Value();
}

ProjectTemplate Make(const char* owner, const char* id, std::vector<std::string> path, std::vector<PluginId> plugins = {}, int order = 0) {
    ProjectTemplate t;
    t.plugin = Id(owner);
    t.id = id;
    t.path = std::move(path);
    t.name = id;
    t.order = order;
    t.plugins.push_back(Id(owner));
    for (auto& p : plugins) t.plugins.push_back(p);
    return t;
}

// 白紙 / ゲーム (game) / 3D (game_3d) / STG (stg, under 3D) / 2D (game_2d)
std::vector<ProjectTemplate> GameTemplates() {
    return {BlankTemplate(),
            Make("game_production", "game", {"game"}),
            Make("game_x_2d", "game_2d", {"game", "2d"}, {Id("game_production"), Id("space2d")}, 1),
            Make("game_x_3d", "game_3d", {"game", "3d"}, {Id("game_production"), Id("space3d")}, 0),
            Make("stg_kit", "stg", {"game", "3d", "stg"}, {Id("game_x_3d")})};
}

bool Has(const std::vector<PluginId>& list, const char* name) { return std::find(list.begin(), list.end(), Id(name)) != list.end(); }

}  // namespace

RM_TEST(Template_Parse) {
    const json::Value ok = Parse(R"({
        "kind": "rm_engine.template", "format": 1, "id": "game_3d", "path": ["game", "3d"],
        "name": "3D のゲーム", "description": "説明", "order": 2,
        "plugins": [ {"author": "rm_engine", "name": "space3d"}, {"author": "rm_engine", "name": "game_x_3d"} ],
        "open": {"author": "rm_engine", "plugin": "game_production", "software": "game"},
        "project": {"softwareData": []}, "userSettings": {"softwares": []},
        "future": {"of": "a newer engine"} })");
    std::string err;
    auto t = ParseTemplate(ok, Id("game_x_3d"), &err);
    CHECK(t.has_value());
    if (t) {
        CHECK(t->id == "game_3d" && t->name == "3D のゲーム" && t->description == "説明" && t->order == 2);
        CHECK((t->path == std::vector<std::string>{"game", "3d"}));
        // Its own plugin first, then the others, each once.
        CHECK((t->plugins == std::vector<PluginId>{Id("game_x_3d"), Id("space3d")}));
        CHECK(t->open.Get("software").AsString() == "game");
        CHECK(t->project.IsObject() && t->userSettings.IsObject());
    }
    // Only kind / format / id / path / name are needed.
    const json::Value small = Parse(R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["a"], "name": "A"})");
    auto s = ParseTemplate(small, Id("game_production"), &err);
    CHECK(s.has_value() && s->open.IsNull() && s->project.IsNull() && s->plugins.size() == 1);

    for (const char* bad : {
             R"({"kind": "other", "format": 1, "id": "a", "path": ["a"], "name": "A"})",
             R"({"kind": "rm_engine.template", "format": 2, "id": "a", "path": ["a"], "name": "A"})",       // newer
             R"({"kind": "rm_engine.template", "format": 1, "id": "A b", "path": ["a"], "name": "A"})",     // id
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": [], "name": "A"})",          // path
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["3D"], "name": "A"})",      // part
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["game", "_x"], "name": "A"})",
             R"({"kind": "rm_engine.template", "format": 1, "id": "a__b", "path": ["a"], "name": "A"})",
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": "a", "name": "A"})",
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["a"]})",                    // name
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["a"], "name": "A", "plugins": [{"author": "rm_engine"}]})",
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["a"], "name": "A", "open": {"plugin": "x"}})",
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["a"], "name": "A", "project": []})",
             R"({"kind": "rm_engine.template", "format": 1, "id": "a", "path": ["a"], "name": "A", "userSettings": {"softwares": [{}]}})",
         }) {
        err.clear();
        CHECK(!ParseTemplate(Parse(bad), Id("game_production"), &err) && !err.empty());
    }
}

RM_TEST(TemplateTree_OrderAndBranches) {
    std::vector<ProjectTemplate> list = GameTemplates();
    list.push_back(Make("music_daw", "daw", {"music", "daw"}, {}, 5));  // "music" has no template of its own
    const TemplateTree tree = BuildTemplateTree(std::move(list));
    CHECK(tree.warnings.empty());
    std::vector<std::string> labels;
    for (size_t i = 0; i < tree.nodes.size(); ++i) labels.push_back(tree.Label(i));
    // Parents before children; siblings by order (白紙 first), the 3D branch with what is below it before 2D.
    CHECK((labels == std::vector<std::string>{"白紙", "game", "game_3d", "stg", "game_2d", "music", "daw"}));
    const size_t game = tree.Find({"game"}), x3d = tree.Find({"game", "3d"}), stg = tree.Find({"game", "3d", "stg"});
    CHECK(tree.nodes[stg].parent == x3d && tree.nodes[x3d].parent == game && tree.nodes[game].parent == static_cast<size_t>(-1));
    CHECK(tree.nodes[stg].depth == 2);
    const size_t music = tree.Find({"music"});
    CHECK(music != static_cast<size_t>(-1) && tree.At(music) == nullptr);  // a branch only: its id is shown
    CHECK(tree.Find({"nothing"}) == static_cast<size_t>(-1));
}

RM_TEST(TemplateTree_TwoAtOnePlace) {
    std::vector<ProjectTemplate> list = GameTemplates();
    list.push_back(Make("other_game", "game_3d_too", {"game", "3d"}));  // same place: the first stays
    list.push_back(Make("game_x_3d", "game_3d", {"game", "3d_again"}));  // same plugin, same id
    const TemplateTree tree = BuildTemplateTree(std::move(list));
    CHECK(tree.warnings.size() == 2);
    CHECK(tree.At(tree.Find({"game", "3d"}))->plugin == Id("game_x_3d"));
    CHECK(tree.Find({"game", "3d_again"}) == static_cast<size_t>(-1));
}

RM_TEST(TemplateTree_PluginsOfANode) {
    const TemplateTree tree = BuildTemplateTree(GameTemplates());
    // Its own template and everything below it (design 09-29: "ゲーム" brings both 3D and 2D).
    const auto game = TemplatePlugins(tree, tree.Find({"game"}));
    for (const char* n : {"game_production", "game_x_3d", "space3d", "stg_kit", "game_x_2d", "space2d"}) CHECK(Has(game, n));
    CHECK(game.size() == 6 && game.front() == Id("game_production"));
    // A middle branch: what is below it only (no 2D).
    const auto x3d = TemplatePlugins(tree, tree.Find({"game", "3d"}));
    CHECK(Has(x3d, "stg_kit") && Has(x3d, "space3d") && !Has(x3d, "space2d") && !Has(x3d, "game_x_2d"));
    const auto x2d = TemplatePlugins(tree, tree.Find({"game", "2d"}));
    CHECK(x2d.size() == 3 && !Has(x2d, "space3d"));
    CHECK(TemplatePlugins(tree, tree.Find({"blank"})).empty());
    CHECK(TemplatePlugins(tree, 999).empty());
}

RM_TEST(TemplateTree_InstallPlan) {
    const auto world = World();
    // Nothing installed: the 2D game needs game production, 2D 空間 and its connector (not 3D).
    std::vector<PluginId> unavailable;
    const auto all2d = TemplateInstallPlan({Id("game_x_2d"), Id("game_production"), Id("space2d")}, world, {}, &unavailable);
    CHECK(unavailable.empty());
    CHECK(Has(all2d, "game_production") && Has(all2d, "space2d") && Has(all2d, "game_x_2d"));
    CHECK(!Has(all2d, "space3d") && !Has(all2d, "game_x_3d"));  // not the first connector this engine has
    // Game production alone: it brings the first connector (and its space), as installing it does.
    const auto alone = TemplateInstallPlan({Id("game_production")}, world, {}, nullptr);
    CHECK(Has(alone, "game_x_3d") && Has(alone, "space3d"));
    // Requirements before the ones needing them, each once.
    auto at = [&](const std::vector<PluginId>& v, const char* n) { return std::find(v.begin(), v.end(), Id(n)) - v.begin(); };
    CHECK(at(all2d, "space2d") < at(all2d, "game_x_2d") && at(all2d, "game_production") < at(all2d, "game_x_2d"));
    for (const PluginId& id : all2d) CHECK(std::count(all2d.begin(), all2d.end(), id) == 1);
    // The 3D side installed: the 2D game brings only the 2D side.
    const std::vector<PluginId> ready{Id("game_production"), Id("space3d"), Id("game_x_3d")};
    const auto more = TemplateInstallPlan({Id("game_x_2d"), Id("game_production"), Id("space2d")}, world, ready, nullptr);
    CHECK((more == std::vector<PluginId>{Id("space2d"), Id("game_x_2d")}));
    // Everything there: nothing to copy.
    std::vector<PluginId> everything;
    for (const auto& m : world) everything.push_back(m.id);
    CHECK(TemplateInstallPlan({Id("game_x_2d"), Id("game_production")}, world, everything, nullptr).empty());
    // A plugin this engine does not have.
    TemplateInstallPlan({Id("stg_kit"), Id("space3d")}, world, {}, &unavailable);
    CHECK((unavailable == std::vector<PluginId>{Id("stg_kit")}));
}

RM_TEST(Template_NewProjectFile) {
    json::Value doc = json::Value::MakeObject();
    doc.Set("kind", kProjectKind);
    doc.Set("name", "MyGame");
    doc.Set("plugins", json::Value::MakeArray());
    ProjectTemplate t = Make("game_x_3d", "game_3d", {"game", "3d"});
    t.project = Parse(R"({
        "name": "not this", "plugins": [{"author": "x", "name": "y"}], "kind": "nope",
        "softwareData": [ {"author": "rm_engine", "plugin": "game_production", "software": "game", "data": {"scene": {}}},
                          {"author": "rm_engine", "plugin": "game_production", "software": "other", "data": {}} ],
        "extra": 5 })");
    ApplyTemplate(doc, t);
    // The core's keys are the core's.
    CHECK(doc.Get("name").AsString() == "MyGame" && doc.Get("kind").AsString() == kProjectKind);
    CHECK(doc.Get("template").Get("author").AsString() == "rm_engine" && doc.Get("template").Get("plugin").AsString() == "game_x_3d" &&
          doc.Get("template").Get("id").AsString() == "game_3d");
    CHECK(doc.Get("extra").AsInt() == 5 && doc.Get("softwareData").Items().size() == 2);
    // The plugin whose data is there is recorded (once), as saving would record it.
    const auto used = ParsePluginIdList(doc.Get("plugins"), nullptr);
    CHECK(used && (*used == std::vector<PluginId>{Id("game_production")}));

    // 白紙: only the record.
    json::Value blank = json::Value::MakeObject();
    blank.Set("plugins", json::Value::MakeArray());
    ApplyTemplate(blank, BlankTemplate());
    CHECK(blank.Get("template").Get("id").AsString() == "blank" && !blank.Get("template").Find("author"));
    CHECK(blank.Get("plugins").Items().empty() && !blank.Find("softwareData"));
}

RM_TEST(Template_FirstLayout) {
    CHECK(TemplateLayout(BlankTemplate()).IsNull());
    ProjectTemplate t = Make("game_x_3d", "game_3d", {"game", "3d"});
    t.open = Parse(R"({"author": "rm_engine", "plugin": "game_production", "software": "game"})");
    t.userSettings = Parse(R"({"softwares": [
        {"author": "rm_engine", "plugin": "game_production", "software": "game", "view": {"a": 1}},
        {"author": "rm_engine", "plugin": "game_production", "software": "game", "view": {"a": 2}} ]})");
    const json::Value l = TemplateLayout(t);
    CHECK(l.Get("kind").AsString() == kLayoutKind && l.Get("formatVersion").AsInt() == kLayoutFormatVersion);
    CHECK(l.Get("active").Get("software").AsString() == "game" && l.Get("active").Get("plugin").AsString() == "game_production");
    // One entry per software (the first).
    CHECK(l.Get("softwares").Items().size() == 1 && l.Get("softwares").Items()[0].Get("view").Get("a").AsInt() == 1);
    // Only the software to open.
    t.userSettings = {};
    const json::Value o = TemplateLayout(t);
    CHECK(o.Get("active").IsObject() && !o.Find("softwares"));
}

RM_TEST(Template_SoftwareGroups) {
    auto world = World();
    PluginManifest paint = Manifest("paint_brushes");
    paint.softwares = {"illust"};  // no template needs it: it says which software it is for
    world.push_back(paint);
    PluginManifest ui = Manifest("game_ui_kit", {Id("game_production")});
    ui.softwares = {"game"};
    world.push_back(ui);
    std::vector<ProjectTemplate> list{BlankTemplate(), Make("game_production", "game", {"game"}),
                                      Make("game_x_3d", "game_3d", {"game", "3d"}, {Id("space3d")}),
                                      Make("game_x_2d", "game_2d", {"game", "2d"}, {Id("space2d")})};
    const auto groups = GroupPluginsBySoftware(BuildTemplateTree(std::move(list)), world);
    CHECK(groups.size() == 2);  // 白紙 needs nothing: not a software
    CHECK(groups[0].id == "game" && groups[0].name == "game");
    for (const char* n : {"game_production", "space3d", "space2d", "game_x_3d", "game_x_2d", "game_ui_kit"}) CHECK(Has(groups[0].plugins, n));
    CHECK(!Has(groups[0].plugins, "theme_colors") && !Has(groups[0].plugins, "paint_brushes"));
    CHECK(groups[1].id == "illust" && groups[1].name == "illust" && (groups[1].plugins == std::vector<PluginId>{Id("paint_brushes")}));

    // plugin.json's "software": an array of ids.
    std::string err;
    auto m = ParsePluginManifest(Parse(R"({"author": "rm_engine", "name": "paint_brushes", "software": ["illust", "game"]})"), &err);
    CHECK(m && (m->softwares == std::vector<std::string>{"illust", "game"}));
    CHECK(!ParsePluginManifest(Parse(R"({"author": "rm_engine", "name": "paint_brushes", "software": "illust"})"), &err));
    CHECK(!ParsePluginManifest(Parse(R"({"author": "rm_engine", "name": "paint_brushes", "software": [1]})"), &err));
}

// The templates in this repository: readable, at their places, and their first data loads as it is.
RM_TEST(Template_ThePlugins) {
    namespace fs = std::filesystem;
    const fs::path plugins = fs::path(__FILE__).parent_path().parent_path() / "plugins";
    std::vector<ProjectTemplate> list{BlankTemplate()};
    std::error_code ec;
    for (fs::recursive_directory_iterator it(plugins, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().filename() != "template.json" || it->path().parent_path().parent_path().filename() != "templates") continue;
        const fs::path pluginDir = it->path().parent_path().parent_path().parent_path();
        const PluginId owner{pluginDir.parent_path().filename().string(), pluginDir.filename().string()};
        std::ifstream in(it->path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const auto doc = json::Parse(text);
        CHECK(doc.has_value());
        if (!doc) continue;
        std::string err;
        auto t = ParseTemplate(*doc, owner, &err);
        CHECK(t.has_value());
        if (!t) continue;
        // The plugins it names are in this repository.
        for (const PluginId& id : t->plugins) CHECK(fs::exists(plugins / id.author / id.name / "plugin.json"));
        t->folder = it->path().parent_path();
        list.push_back(std::move(*t));
    }
    const TemplateTree tree = BuildTemplateTree(std::move(list));
    CHECK(tree.warnings.empty());
    CHECK(tree.templates.size() == 5);
    for (const auto& path : std::vector<std::vector<std::string>>{{"blank"}, {"game"}, {"game", "3d"}, {"game", "2d"}, {"game", "2d", "scenes"}}) {
        CHECK(tree.At(tree.Find(path)) != nullptr);
    }
    // Choosing ゲーム brings both spaces and both connectors.
    const auto game = TemplatePlugins(tree, tree.Find({"game"}));
    for (const char* n : {"game_production", "space3d", "space2d", "game_x_3d", "game_x_2d"}) CHECK(Has(game, n));

    using namespace rm::game_production;
    for (const ProjectTemplate& t : tree.templates) {
        for (const auto& entry : t.project.Get("softwareData").Items()) {
            CHECK(entry.Get("plugin").AsString() == "game_production" && entry.Get("software").AsString() == "game");
            const json::Value& data = entry.Get("data");
            Scene scene;
            scene.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<game_x_3d::Camera>(),
                                     &reflect::TypeOf<game_x_3d::Light>(), &reflect::TypeOf<game_x_3d::MeshRenderer>(),
                                     &reflect::TypeOf<space2d::Transform2D>(), &reflect::TypeOf<game_x_2d::Camera2D>(),
                                     &reflect::TypeOf<game_x_2d::Sprite>()});
            // The new form: the scene in its own file (the template's Assets/, copied into the project),
            // named in the list by the id in its .meta.
            CHECK(!data.Find("scene"));
            SceneList sceneList;
            reflect::Unknowns listUnknowns;
            reflect::Warnings warnings;
            reflect::Load(sceneList, data.Get("sceneList"), &listUnknowns, &warnings);
            CHECK(listUnknowns.Empty() && !sceneList.scenes.empty());
            // Every scene of the list is a file of the template (found by the id in its .meta); the one
            // read below is the one the project opens with.
            std::map<std::string, fs::path> files;  // id -> scene file
            std::error_code dirEc;
            for (fs::directory_iterator f(t.folder / "Assets" / "Scenes", dirEc), end; !dirEc && f != end; f.increment(dirEc)) {
                if (f->path().extension() != L".meta") continue;
                std::ifstream metaIn(f->path(), std::ios::binary);
                const std::string meta{std::istreambuf_iterator<char>(metaIn), std::istreambuf_iterator<char>()};
                if (const auto id = asset::GuidInMeta(meta)) files[*id] = fs::path(f->path()).replace_extension();
            }
            for (const AssetRef& ref : sceneList.scenes) CHECK(files.contains(ref.guid));
            CHECK(sceneList.first.Empty() || files.contains(sceneList.first.guid));
            if (!files.contains(OpenScene(sceneList).guid)) continue;
            const fs::path sceneFile = files[OpenScene(sceneList).guid];
            std::ifstream sceneIn(sceneFile, std::ios::binary);
            const std::string sceneText{std::istreambuf_iterator<char>(sceneIn), std::istreambuf_iterator<char>()};
            std::string sceneErr;
            const auto sceneJson = ReadSceneFile(sceneText, &sceneErr);
            CHECK(sceneJson.has_value());
            if (!sceneJson) continue;
            scene.Load(*sceneJson, &warnings);
            // (only the template's own scripts are types not known here)
            std::erase_if(warnings, [&](const std::string& w) {
                std::error_code se;
                for (fs::directory_iterator f(t.folder / "Assets" / "Scripts", se), end; !se && f != end; f.increment(se)) {
                    if (f->path().extension() == L".h" && w.find(f->path().stem().string()) != std::string::npos) return true;
                }
                return false;
            });
            CHECK(warnings.empty());
            CHECK(!scene.Empty());
            for (const SceneEntity& e : scene.Entities()) {
                CHECK(e.unknowns.Empty());
                // (a script of the template's own Assets/Scripts is a type of its DLL: not known here)
                for (const Component& c : e.components) CHECK(c.Known() || fs::exists(t.folder / "Assets" / "Scripts" / (c.type + ".h")));
            }
            GameSettings settings;
            reflect::Unknowns unknowns;
            reflect::Load(settings, data.Get("settings"), &unknowns, &warnings);
            CHECK(warnings.empty() && unknowns.Empty());
            CHECK(settings.space == "3d" || settings.space == "2d");
            // The game sees through a camera of the scene.
            const SceneEntity* cam = scene.Find(static_cast<int>(settings.startCamera.id));
            CHECK(cam && (cam->FindComponent(reflect::TypeOf<game_x_3d::Camera>().name) || cam->FindComponent(reflect::TypeOf<game_x_2d::Camera2D>().name)));
        }
        if (!t.project.IsNull()) CHECK(t.open.Get("plugin").AsString() == "game_production");
    }
}
