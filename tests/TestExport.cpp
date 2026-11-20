// ゲームの書き出しの純粋な部分のテスト: .rmpak (core/pak)、どのプラグインが入るか (editor/export
// ExportPlan + 部品の持ち主: game_production Components.h)、exe の名前、生成された main、
// そしてゲーム自身のフォルダのパスだけが残ること。

#include "Test.h"

#include "core/pak/Pak.h"
#include "platform/Crypto.h"
#include "editor/export/ExportPlan.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>
#include <array>
#include <vector>

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
    const std::string bytes = pak::Write(entries, 1234, platform::PakCipher(1234));
    // 書いたままのものは何もない (pak をテキストとして開いてもファイルは読めない)。
    CHECK(bytes.find("My Game") == std::string::npos && bytes.find("rm_engine.scene") == std::string::npos);
    CHECK(bytes.find("Main.rmscene") == std::string::npos);  // 索引も
    pak::Reader r;
    std::string err;
    CHECK(r.Open(FromMemory(bytes), bytes.size(), 1234, platform::PakCipher(1234), &err));
    CHECK(r.Entries().size() == 4 && r.Build() == 1234);
    for (const auto& e : entries) {
        const auto read = r.Read(e.path, &err);
        CHECK(read && *read == e.bytes);
    }
    CHECK(!r.Read("Assets/none.png", &err) && !err.empty());
    // ビルド 0: どのビルドも読める。
    pak::Reader any;
    CHECK(any.Open(FromMemory(bytes), bytes.size(), 0, platform::PakCipher(1234), &err));
}

// Windows の CNG による AES-256-CTR: NIST SP 800-38A のテストベクター (F.5.5)、そして元に戻す。
RM_TEST(Pak_AesCtrVector) {
    auto hex = [](const char* h) {
        std::vector<uint8_t> out;
        for (size_t i = 0; h[i] && h[i + 1]; i += 2) out.push_back(static_cast<uint8_t>(std::stoi(std::string(h + i, 2), nullptr, 16)));
        return out;
    };
    const auto k = hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    const auto v = hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    platform::AesKey key{};
    std::array<uint8_t, 16> iv{};
    std::copy(k.begin(), k.end(), key.begin());
    std::copy(v.begin(), v.end(), iv.begin());
    auto data = hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    CHECK(platform::AesCtr(key, iv, data.data(), data.size()));
    CHECK(data == hex("601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5"));
    CHECK(platform::AesCtr(key, iv, data.data(), data.size()));
    CHECK(data == hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"));
    // pak の暗号: 別のビルドなら別の鍵。
    std::string a(40, 'x'), b(40, 'x');
    platform::PakCipher(1)(a.data(), a.size(), 0);
    platform::PakCipher(2)(b.data(), b.size(), 0);
    CHECK(a != b && a != std::string(40, 'x'));
}

RM_TEST(Pak_Refused) {
    const std::vector<pak::Entry> entries = {{"a.txt", "hello world"}};
    std::string bytes = pak::Write(entries, 7, platform::PakCipher(7));
    std::string err;
    pak::Reader r;
    // 別の書き出しのもの: 理由を付けて断る。
    CHECK(!r.Open(FromMemory(bytes), bytes.size(), 8, platform::PakCipher(8), &err) && !err.empty());
    // pak ではない / 短すぎる。
    const std::string text = "not a pak at all, just some text....................";
    CHECK(!r.Open(FromMemory(text), text.size(), 0, platform::PakCipher(0), &err));
    const std::string tiny = "RMPAK";
    CHECK(!r.Open(FromMemory(tiny), tiny.size(), 0, platform::PakCipher(0), &err));
    // 項目の中で 1 バイト変わった: 読んだときに見つかる。
    CHECK(r.Open(FromMemory(bytes), bytes.size(), 7, platform::PakCipher(7), &err));
    bytes[r.Entries()[0].at + 3] ^= 0x10;
    CHECK(!r.Read("a.txt", &err) && !err.empty());
    // 壊れた索引 (サイズが終わりを越える): Open で断る。
    std::string cut = pak::Write(entries, 7, platform::PakCipher(7));
    cut.resize(cut.size() - 4);
    CHECK(!r.Open(FromMemory(cut), cut.size(), 7, platform::PakCipher(7), &err));
}

RM_TEST(Export_ExeName) {
    CHECK(ExeNameFrom("My Game") == "My Game");
    CHECK(ExeNameFrom("ぼくの:ゲーム?") == "ぼくのゲーム");
    CHECK(ExeNameFrom("  a/b\\c*d.  ") == "abcd");
    CHECK(ExeNameFrom("???") == "Game" && ExeNameFrom("") == "Game" && ExeNameFrom("CON") == "Game");
    CHECK(ExeNameFrom("A=B") == "A=B");  // "=" はファイル名に使える文字 (前のエンジンの #11 は名前の問題ではなかった)
    const std::string longName(100, 'x');
    CHECK(ExeNameFrom(longName).size() == 64);
    CHECK(ExeNameProblem("Game").empty() && !ExeNameProblem("a:b").empty() && !ExeNameProblem("").empty() && !ExeNameProblem("NUL").empty());
    CHECK(ExeNameProblem(ExeNameFrom("名前<>|?.")).empty());
}

RM_TEST(Export_Plugins) {
    const auto world = World();
    std::vector<PluginId> missing;
    // 2D のゲーム: 2D 側とそのコネクターだけで、3D のものはなし。
    const auto two = ExportPlugins({Id("game_production"), Id("game_x_2d")}, world, &missing);
    CHECK(missing.empty() && Has(two, "game_production") && Has(two, "space2d") && Has(two, "game_x_2d") && !Has(two, "space3d"));
    auto at = [&](const std::vector<PluginId>& v, const char* n) { return std::find(v.begin(), v.end(), Id(n)) - v.begin(); };
    CHECK(at(two, "game_production") < at(two, "game_x_2d") && at(two, "space2d") < at(two, "game_x_2d"));
    // ゲーム制作だけが記録された (中身のないシーン): コネクターが要る - 最初のもの。
    const auto bare = ExportPlugins({Id("game_production")}, world, &missing);
    CHECK(Has(bare, "game_x_3d") && Has(bare, "space3d"));
    CHECK(!Has(bare, "theme_colors"));  // 何も使わないものは決して入れない
    // このエンジンにないプラグイン。
    ExportPlugins({Id("physics_jolt")}, world, &missing);
    CHECK(missing.size() == 1 && missing[0] == Id("physics_jolt"));
}

RM_TEST(Export_UsedPluginsFromComponents) {
    // シーン: エンティティの部品の型 (と置いた Prefab で足された型) を、それぞれ 1 回。
    const json::Value scene = Parse(R"({"entities": [
        {"id": 1, "components": [{"type": "rm::space3d::Transform", "data": {}}, {"type": "rm::game_x_3d::Camera", "data": {}}]},
        {"id": 2, "components": [{"type": "rm::space3d::Transform", "data": {}}],
         "prefab": {"source": 1, "overrides": {"addedComponents": [{"source": 1, "component": {"type": "rm::game_x_2d::Sprite"}}]}}}]})");
    std::vector<std::string> types;
    game_production::ComponentTypesIn(scene, types);
    CHECK((types == std::vector<std::string>{"rm::space3d::Transform", "rm::game_x_3d::Camera", "rm::game_x_2d::Sprite"}));
    // それらの持ち主は、プラグインが登録したとおり (ランタイムの部品)。
    RuntimeRegistry r;
    const PluginId ids[] = {game_x_3d::kPluginId.Id(), game_x_2d::kPluginId.Id()};
    RegisterRuntimeParts(r, ids);
    const auto offered = r.Contributions<game_production::ComponentInfo>(game_production::kComponentPoint);
    const auto owners = game_production::PluginsOffering({"rm::space3d::Transform"}, offered);
    CHECK((owners == std::vector<PluginId>{game_x_3d::kPluginId.Id()}));
    const auto none = game_production::PluginsOffering({"rm::someone::Gone"}, offered);
    CHECK(none.empty());
    // 本物のシーンのファイルの形から。
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
    // 登録を持つプラグインの登録を、C の名前で順に。持たないものも並べる。
    CHECK(RuntimeEntryName(Id("game_x_2d")) == "rm_runtime__rm_engine__game_x_2d");
    CHECK(s.find("void rm_runtime__rm_engine__game_x_2d(rm::RuntimeRegistry&);") != std::string::npos);
    CHECK(s.find("&rm_runtime__rm_engine__game_x_2d}") != std::string::npos);
    CHECK(s.find("rm_runtime__rm_engine__game_production") == std::string::npos);
    CHECK(s.find("{\"rm_engine\", \"game_production\", nullptr}") < s.find("{\"rm_engine\", \"game_x_2d\""));
    // すべてのライブラリの型は AllTypes に、列挙型は AllEnums に。
    for (const char* sym : {"&rmgen_generated::T_rm__asset__ModelImport", "&rmgen_generated::T_rm__game_x_2d__Sprite",
                            "&rmgen_generated::E_rm__asset__UpAxis", "extern const ::rm::reflect::TypeDesc T_rm__game_production__Entity;"}) {
        CHECK(s.find(sym) != std::string::npos);
    }
    // タイトルはエスケープした UTF-8 (生の引用符でリテラルが壊れない)、ビルド番号。
    CHECK(s.find("\\\"") != std::string::npos && s.find("\\xe3") != std::string::npos && s.find("42ull") != std::string::npos);
    CHECK(s.find("rm_game_image()") != std::string::npos);
    // 列挙型がまったくない: 空の配列ではなく、空の span。
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

RM_TEST(Export_ResourceIconAndNumber) {
    // リソース: ファイルの詳細に名前 ("" は二重に)、アイコンは、あるときだけ。
    const std::string rc = GameResourceSource("ぼくの \"ゲーム\"", "MyGame", true);
    CHECK(rc.find("1 ICON \"game.ico\"") != std::string::npos);
    CHECK(rc.find("VALUE \"FileDescription\", \"ぼくの \"\"ゲーム\"\"\"") != std::string::npos);
    CHECK(rc.find("\"MyGame.exe\"") != std::string::npos);
    CHECK(GameResourceSource("G", "G", false).find("ICON") == std::string::npos);
    // \ も二重に (rc はエスケープの始まりと読む: "C:\new" だと改行になる)。
    CHECK(GameResourceSource("a\\new", "G", false).find("\"a\\\\new\"") != std::string::npos);
    // PNG をそのまま持つアイコン: ヘッダー、項目 1 つ (256 -> 0)、PNG は 22 バイトのあと。
    std::string png = "\x89PNG\r\n\x1a\n";
    png += std::string("\0\0\0\x0dIHDR", 8);
    png += std::string("\0\0\x01\0\0\0\0\x30", 8);  // 256 x 48
    png += "rest of the picture";
    const auto ico = IcoFromPng(png);
    CHECK(ico.has_value());
    if (ico) {
        CHECK(ico->size() == 22 + png.size() && ico->substr(22) == png);
        CHECK((*ico)[2] == 1 && (*ico)[4] == 1 && (*ico)[6] == 0 && (*ico)[7] == 48);
    }
    CHECK(!IcoFromPng("GIF89a....................."));
    // ビルド番号: 同じファイルなら同じ番号、別のファイルなら別の番号、0 は決してない。
    const std::vector<std::pair<std::string, std::string>> a{{"game.json", "{}"}, {"Assets/a.png", "x"}};
    auto b = a;
    CHECK(BuildNumber(a) == BuildNumber(b) && BuildNumber(a) != 0);
    b[1].second = "y";
    CHECK(BuildNumber(a) != BuildNumber(b));
    const std::vector<std::pair<std::string, std::string>> joined{{"ab", "c"}}, split{{"a", "bc"}};
    CHECK(BuildNumber(joined) != BuildNumber(split));
}

RM_TEST(Export_AssetIdsOfAScene) {
    const json::Value scene = Parse(R"({"entities": [
        {"id": 1, "name": "0123456789abcdef0123456789abcdef", "components": [{"type": "rm::game_x_2d::Sprite", "data": {"image": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}]},
        {"id": 2, "prefab": {"source": 1, "asset": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}, "components": [{"type": "x", "data": {"image": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "n": "ABCDEF0123456789abcdef0123456789"}}]}]})");
    const auto ids = game_production::AssetIdsIn(scene);
    // すべてのアセット ID を、それぞれ 1 回 (大文字のものは ID ではない)。ID に見える名前も
    // 見つかる - 書き出しはそのアセットを見つけられず、外す。
    CHECK(ids.size() == 3);
    CHECK(std::find(ids.begin(), ids.end(), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != ids.end());
}

RM_TEST(Export_IconFile) {
    // .ico はそのまま (中のすべてのサイズ)、PNG はそれに入れる、それ以外はなし。
    auto icoWith = [](uint32_t size, uint32_t offset) {
        std::string ico("\0\0\x01\0\x01\0", 6);
        ico += std::string(8, '\x01');  // 幅、高さ、色数、予約、プレーン数、ビット数
        ico.append(reinterpret_cast<const char*>(&size), 4);
        ico.append(reinterpret_cast<const char*>(&offset), 4);
        return ico + "picture";
    };
    const std::string ico = icoWith(7, 22);
    CHECK(IconFile(ico) == ico);
    CHECK(!IconFile(std::string("\0\0\x01\0\x02\0", 6) + std::string(16, '\x01')));  // 項目 2 つと言うが、あるのは 1 つ
    // 項目の画像がファイルの外、ヘッダーの内側、またはサイズ 0: アイコンではない。
    CHECK(!IconFile(icoWith(8, 22)) && !IconFile(icoWith(7, 23)) && !IconFile(icoWith(7, 0)) && !IconFile(icoWith(0, 22)));
    CHECK(!IconFile(icoWith(0xFFFFFFFFu, 22)) && !IconFile(icoWith(7, 0xFFFFFFF0u)));
    CHECK(!IconFile("BM........................"));
    std::string png = "\x89PNG\r\n\x1a\n";
    png += std::string("\0\0\0\x0dIHDR", 8);
    png += std::string("\0\0\0\x20\0\0\0\x20", 8);
    const auto made = IconFile(png);
    CHECK(made && made->substr(22) == png);
}

// 書き出しに入れるシーン: 一覧のもの、その順 (今開いているものは、そのまま)。そのあと
// 開いているものが一覧にないとき。読めないものは外す。ゲームは一覧の最初から始まる。
RM_TEST(Export_ScenesOfTheGame) {
    using namespace rm::game_production;
    const AssetRef a{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}, b{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}, c{"cccccccccccccccccccccccccccccccc"};
    SceneList list;
    list.scenes = {a, b, c};
    list.first = b;
    auto read = [&](const AssetRef& ref) -> std::optional<std::pair<std::string, std::string>> {
        if (ref == c) return std::nullopt;  // (消えた)
        return std::make_pair("Assets/Scenes/" + ref.guid.substr(0, 1) + ".rmscene", "file " + ref.guid.substr(0, 1));
    };
    ExportScenes s = ScenesForExport(list, a, "Assets/Scenes/a.rmscene", "now a", read);
    CHECK(s.scenes.size() == 2 && s.scenes[0].second == "now a" && s.scenes[1].second == "file b");
    CHECK(s.first == 1 && s.unread == std::vector<std::string>{c.guid});
    // 開いているシーンが一覧にない (新しい): 最後。最初のシーンは一覧のもの。
    list.first = {};
    s = ScenesForExport(list, AssetRef{}, "Assets/Scenes/Scene.rmscene", "new", read);
    CHECK(s.scenes.size() == 3 && s.scenes[2].first == "Assets/Scenes/Scene.rmscene" && s.first == 0);
    // 一覧の最初が読めない: ゲームは開いているものから始まる。
    list.first = c;
    list.scenes = {c, b};
    s = ScenesForExport(list, b, "Assets/Scenes/b.rmscene", "now b", read);
    CHECK(s.scenes.size() == 1 && s.first == 0);
    // 空の一覧 (以前のプロジェクト): 開いているシーンだけ。
    s = ScenesForExport(SceneList{}, a, "Assets/Scenes/Main.rmscene", "main", read);
    CHECK(s.scenes.size() == 1 && s.first == 0 && s.scenes[0].second == "main");
    // ビルドから外した (ゲームの設定 ▸ ビルド): 入らない - ただしゲームが始まるものは必ず入る。
    SceneList some;
    some.scenes = {a, b};
    some.first = b;
    some.notBuilt = {a, b};
    CHECK(!SceneBuilt(some, a) && SceneBuilt(some, b));
    s = ScenesForExport(some, a, "Assets/Scenes/a.rmscene", "now a", read);
    CHECK(s.scenes.size() == 1 && s.scenes[0].second == "file b" && s.first == 0);
}
