// Tests for exporting a game's pure parts: the .rmpak (core/pak), which plugins go in (editor/export
// ExportPlan + the components' owners: game_production Components.h), the exe's name, the generated
// main and that only paths of the game's own folder are left.

#include "Test.h"

#include "core/pak/Pak.h"
#include "editor/export/ExportPlan.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>

using namespace rm;
using namespace rm::editor;
using namespace rm::editor::exporter;

namespace {

json::Value Parse(const char* text) {
    auto v = json::Parse(text);
    CHECK(v.has_value());
    return v ? std::move(*v) : json::Value();
}

PluginId Id(const char* name) { return {"rm_engine", name}; }

PluginManifest Manifest(const char* name, std::vector<PluginId> requirements = {}) {
    PluginManifest m;
    m.id = Id(name);
    m.displayName = name;
    m.requirements = std::move(requirements);
    return m;
}

std::vector<PluginManifest> World() {
    PluginManifest game = Manifest("game_production");
    game.needsConnector = true;
    PluginManifest x3d = Manifest("game_x_3d", {Id("game_production"), Id("space3d")});
    x3d.connectorFor = Id("game_production");
    PluginManifest x2d = Manifest("game_x_2d", {Id("game_production"), Id("space2d")});
    x2d.connectorFor = Id("game_production");
    return {game, Manifest("space3d"), Manifest("space2d"), x3d, x2d, Manifest("theme_colors")};
}

pak::Reader::ReadFn FromMemory(const std::string& bytes) {
    return [&bytes](uint64_t at, size_t size, char* out) {
        if (at > bytes.size() || size > bytes.size() - at) return false;
        std::copy_n(bytes.data() + at, size, out);
        return true;
    };
}

bool Has(const std::vector<PluginId>& v, const char* name) { return std::find(v.begin(), v.end(), Id(name)) != v.end(); }

}  // namespace

RM_TEST(Pak_RoundTrip) {
    const std::vector<pak::Entry> entries = {{"game.json", R"({"title": "My Game"})"},
                                             {"Assets/Scenes/Main.rmscene", std::string("{\"kind\": \"rm_engine.scene\"}\n")},
                                             {"Assets/empty.txt", ""},
                                             {"Assets/bin.dat", std::string("\0\x01\xff\x80", 4)}};
    const std::string bytes = pak::Write(entries, 1234);
    // Nothing is there as it was written (the files cannot be read by opening the pak as text).
    CHECK(bytes.find("My Game") == std::string::npos && bytes.find("rm_engine.scene") == std::string::npos);
    CHECK(bytes.find("Main.rmscene") == std::string::npos);  // the index too
    pak::Reader r;
    std::string err;
    CHECK(r.Open(FromMemory(bytes), bytes.size(), 1234, &err));
    CHECK(r.Entries().size() == 4 && r.Build() == 1234);
    for (const auto& e : entries) {
        const auto read = r.Read(e.path, &err);
        CHECK(read && *read == e.bytes);
    }
    CHECK(!r.Read("Assets/none.png", &err) && !err.empty());
    // Build 0: any build is read.
    pak::Reader any;
    CHECK(any.Open(FromMemory(bytes), bytes.size(), 0, &err));
}

RM_TEST(Pak_Refused) {
    const std::vector<pak::Entry> entries = {{"a.txt", "hello world"}};
    std::string bytes = pak::Write(entries, 7);
    std::string err;
    pak::Reader r;
    // Of another export: refused with the reason.
    CHECK(!r.Open(FromMemory(bytes), bytes.size(), 8, &err) && !err.empty());
    // Not a pak / too short.
    const std::string text = "not a pak at all, just some text....................";
    CHECK(!r.Open(FromMemory(text), text.size(), 0, &err));
    const std::string tiny = "RMPAK";
    CHECK(!r.Open(FromMemory(tiny), tiny.size(), 0, &err));
    // A changed byte in an entry: found when it is read.
    CHECK(r.Open(FromMemory(bytes), bytes.size(), 7, &err));
    bytes[r.Entries()[0].at + 3] ^= 0x10;
    CHECK(!r.Read("a.txt", &err) && !err.empty());
    // A broken index (a size past the end): refused at Open.
    std::string cut = pak::Write(entries, 7);
    cut.resize(cut.size() - 4);
    CHECK(!r.Open(FromMemory(cut), cut.size(), 7, &err));
}

RM_TEST(Export_ExeName) {
    CHECK(ExeNameFrom("My Game") == "My Game");
    CHECK(ExeNameFrom("ぼくの:ゲーム?") == "ぼくのゲーム");
    CHECK(ExeNameFrom("  a/b\\c*d.  ") == "abcd");
    CHECK(ExeNameFrom("???") == "Game" && ExeNameFrom("") == "Game" && ExeNameFrom("CON") == "Game");
    CHECK(ExeNameFrom("A=B") == "A=B");  // "=" is a file name's character (the previous engine's #11 was not the name)
    const std::string longName(100, 'x');
    CHECK(ExeNameFrom(longName).size() == 64);
    CHECK(ExeNameProblem("Game").empty() && !ExeNameProblem("a:b").empty() && !ExeNameProblem("").empty() && !ExeNameProblem("NUL").empty());
    CHECK(ExeNameProblem(ExeNameFrom("名前<>|?.")).empty());
}

RM_TEST(Export_Plugins) {
    const auto world = World();
    std::vector<PluginId> missing;
    // A 2D game: the 2D side and its connector, nothing of 3D.
    const auto two = ExportPlugins({Id("game_production"), Id("game_x_2d")}, world, &missing);
    CHECK(missing.empty() && Has(two, "game_production") && Has(two, "space2d") && Has(two, "game_x_2d") && !Has(two, "space3d"));
    auto at = [&](const std::vector<PluginId>& v, const char* n) { return std::find(v.begin(), v.end(), Id(n)) - v.begin(); };
    CHECK(at(two, "game_production") < at(two, "game_x_2d") && at(two, "space2d") < at(two, "game_x_2d"));
    // Only game production recorded (a scene with nothing in it): it needs a connector - the first one.
    const auto bare = ExportPlugins({Id("game_production")}, world, &missing);
    CHECK(Has(bare, "game_x_3d") && Has(bare, "space3d"));
    CHECK(!Has(bare, "theme_colors"));  // never what nothing uses
    // A plugin this engine does not have.
    ExportPlugins({Id("physics_jolt")}, world, &missing);
    CHECK(missing.size() == 1 && missing[0] == Id("physics_jolt"));
}

RM_TEST(Export_UsedPluginsFromComponents) {
    // A scene: its entities' component types (and a placed prefab's added one), each once.
    const json::Value scene = Parse(R"({"entities": [
        {"id": 1, "components": [{"type": "rm::space3d::Transform", "data": {}}, {"type": "rm::game_x_3d::Camera", "data": {}}]},
        {"id": 2, "components": [{"type": "rm::space3d::Transform", "data": {}}],
         "prefab": {"source": 1, "overrides": {"addedComponents": [{"source": 1, "component": {"type": "rm::game_x_2d::Sprite"}}]}}}]})");
    std::vector<std::string> types;
    game_production::ComponentTypesIn(scene, types);
    CHECK((types == std::vector<std::string>{"rm::space3d::Transform", "rm::game_x_3d::Camera", "rm::game_x_2d::Sprite"}));
    // Their owners, as the plugins registered them (runtime parts).
    RuntimeRegistry r;
    const PluginId ids[] = {game_x_3d::kPluginId.Id(), game_x_2d::kPluginId.Id()};
    RegisterRuntimeParts(r, ids);
    const auto offered = r.Contributions<game_production::ComponentInfo>(game_production::kComponentPoint);
    const auto owners = game_production::PluginsOffering({"rm::space3d::Transform"}, offered);
    CHECK((owners == std::vector<PluginId>{game_x_3d::kPluginId.Id()}));
    const auto none = game_production::PluginsOffering({"rm::someone::Gone"}, offered);
    CHECK(none.empty());
    // From a real scene's file form.
    game_production::Scene s;
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    s.AddComponent(s.Create("A"), reflect::TypeOf<space3d::Transform>());
    types.clear();
    game_production::ComponentTypesIn(s.Save(), types);
    CHECK(game_production::PluginsOffering(types, offered).size() == 1);
}

RM_TEST(Export_GameMain) {
    std::vector<GameLib> libs;
    libs.push_back({std::nullopt, false, {"T_rm__asset__ModelImport"}, {"E_rm__asset__UpAxis"}});
    libs.push_back({Id("game_production"), false, {"T_rm__game_production__Entity"}, {}});
    libs.push_back({Id("game_x_2d"), true, {"T_rm__game_x_2d__Sprite"}, {}});
    const std::string s = GameMainSource(libs, "ぼくの \"ゲーム\"", 42);
    // The registration of the plugins that have one, by its C name, in order; the others listed too.
    CHECK(RuntimeEntryName(Id("game_x_2d")) == "rm_runtime__rm_engine__game_x_2d");
    CHECK(s.find("void rm_runtime__rm_engine__game_x_2d(rm::RuntimeRegistry&);") != std::string::npos);
    CHECK(s.find("&rm_runtime__rm_engine__game_x_2d}") != std::string::npos);
    CHECK(s.find("rm_runtime__rm_engine__game_production") == std::string::npos);
    CHECK(s.find("{\"rm_engine\", \"game_production\", nullptr}") < s.find("{\"rm_engine\", \"game_x_2d\""));
    // Every library's types in AllTypes, the enums in AllEnums.
    for (const char* sym : {"&rmgen_generated::T_rm__asset__ModelImport", "&rmgen_generated::T_rm__game_x_2d__Sprite",
                            "&rmgen_generated::E_rm__asset__UpAxis", "extern const ::rm::reflect::TypeDesc T_rm__game_production__Entity;"}) {
        CHECK(s.find(sym) != std::string::npos);
    }
    // The title as escaped UTF-8 (no raw quote breaks the literal), the build number.
    CHECK(s.find("\\\"") != std::string::npos && s.find("\\xe3") != std::string::npos && s.find("42ull") != std::string::npos);
    CHECK(s.find("rm_game_image()") != std::string::npos);
    // No enums at all: an empty span, not an empty array.
    const std::string bare = GameMainSource({{std::nullopt, false, {}, {}}}, "G", 1);
    CHECK(bare.find("return {};") != std::string::npos && bare.find("kItems[] = {\n    };") == std::string::npos);
}

RM_TEST(Export_ReadSymbols) {
    const char* text = R"(namespace rmgen_generated {
extern const ::rm::reflect::TypeDesc T_rm__x__A;
}
namespace rmgen_generated {
extern const ::rm::reflect::TypeDesc T_rm__x__B = {
extern const ::rm::reflect::EnumDesc E_rm__x__Kind = {
extern const ::rm::reflect::TypeDesc T_rm__x__B = {
})";
    std::vector<std::string> types, enums;
    ReadSymbols(text, types, enums);
    CHECK((types == std::vector<std::string>{"T_rm__x__B"}) && (enums == std::vector<std::string>{"E_rm__x__Kind"}));
}

RM_TEST(Export_PortablePaths) {
    CHECK(PortablePath("Assets/Scenes/Main.rmscene") && PortablePath("Scripts.dll") && PortablePath("a\\b.png"));
    for (const char* bad : {"", "C:\\Users\\me\\a.png", "/etc/x", "\\\\server\\share\\a", "../a.png", "a/../../b", "a//b", "D:a"}) CHECK(!PortablePath(bad));
    const json::Value data = Parse(R"({"title": "G", "scene": {"entities": [{"name": "C:\\Users\\me\\x.png"}]},
        "list": ["Assets/a.png", "\\\\pc\\share\\b.png", "d:/x"], "n": 1})");
    const auto abs = AbsolutePathsIn(data);
    CHECK(abs.size() == 3);
    CHECK(AbsolutePathsIn(Parse(R"({"a": "Assets/x.png", "b": "1:2"})")).empty());
}
