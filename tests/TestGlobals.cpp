// ゲームのグローバル変数とセーブのテスト (ゲーム制作の runtime/Globals と、スクリプトの部品
// "rm_engine.game_production.globals" / ".save" と、ヘッダーの Global::<name>)。
// 初めの値を型どおりに読む、セーブを書いて読み戻す、このバージョンが知らないキーは残す、
// 名前でないスロットは断る、スクリプトの型と変数の型の間で数を変換する。

#include "Test.h"

#include "base/Json.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "rm_engine/game_production/runtime/Globals.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <windows.h>

#include <filesystem>

using namespace rm;
using namespace rm::game_production;
namespace fs = std::filesystem;

namespace {

fs::path TempFolder(const wchar_t* name) {
    const fs::path p = fs::temp_directory_path() / (std::wstring(name) + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

std::vector<GlobalVariable> SomeGlobals() {
    return {{"score", GlobalType::Int, "10"},     {"speed", GlobalType::Float, "1.5"}, {"cleared", GlobalType::Bool, "true"},
            {"player", GlobalType::String, "A B"}, {"home", GlobalType::Vec3, "1, 2, 3"}, {"aim", GlobalType::Vec2, ""}};
}

}  // namespace

RM_TEST(Globals_InitialValuesByType) {
    auto v = ParseGlobalValue(GlobalType::Int, " 7 ");
    CHECK(v && v->v[0] == 7);
    CHECK(ParseGlobalValue(GlobalType::Int, "2.6")->v[0] == 3);  // int は整数
    CHECK(ParseGlobalValue(GlobalType::Int, "")->v[0] == 0);
    CHECK(!ParseGlobalValue(GlobalType::Int, "abc") && !ParseGlobalValue(GlobalType::Int, "1, 2"));
    CHECK(ParseGlobalValue(GlobalType::Bool, "true")->v[0] == 1 && ParseGlobalValue(GlobalType::Bool, "false")->v[0] == 0);
    CHECK(!ParseGlobalValue(GlobalType::Bool, "yes"));
    const auto vec = ParseGlobalValue(GlobalType::Vec3, "1, -2.5 3");
    CHECK(vec && vec->v[0] == 1 && vec->v[1] == -2.5 && vec->v[2] == 3);
    CHECK(ParseGlobalValue(GlobalType::Vec2, "4")->v[1] == 0);  // 足りない分は 0
    CHECK(!ParseGlobalValue(GlobalType::Vec2, "1, 2, 3"));
    CHECK(ParseGlobalValue(GlobalType::String, " as it is ")->s == " as it is ");
    // 同じ形で文字に書き戻す。
    for (const GlobalVariable& g : SomeGlobals()) {
        const auto parsed = ParseGlobalValue(g.type, g.initial);
        CHECK(parsed && ParseGlobalValue(g.type, FormatGlobalValue(*parsed))->v[0] == parsed->v[0]);
    }
    CHECK(IsGlobalName("score") && IsGlobalName("_x2") && !IsGlobalName("2x") && !IsGlobalName("int") && !IsGlobalName("スコア") &&
          !IsGlobalName("a-b") && !IsGlobalName(""));
}

RM_TEST(Globals_StoreReset_ABadInitialIsZero) {
    std::vector<GlobalVariable> list = SomeGlobals();
    list.push_back({"broken", GlobalType::Float, "fast"});
    list.push_back({"score", GlobalType::Int, "99"});  // 2 回: 最初のもの
    GlobalStore s;
    std::vector<std::string> warnings;
    s.Reset(list, &warnings);
    CHECK(s.Size() == 7 && s.Find("score")->v[0] == 10 && s.Find("broken")->v[0] == 0 && warnings.size() == 1);
    CHECK(s.Find("player")->s == "A B" && s.Find("home")->v[2] == 3 && s.Find("nobody") == nullptr);
}

RM_TEST(Globals_SaveWrittenAndReadBack_UnknownKeysKept) {
    const fs::path folder = TempFolder(L"rm_saves_");
    GlobalStore s;
    s.Reset(SomeGlobals());
    s.Find("score")->v[0] = 42;
    s.Find("player")->s = "Z";
    std::string why;
    CHECK(!SaveExists(folder, "0"));
    CHECK(WriteSave(s, folder, "0", &why) && SaveExists(folder, "0"));
    const auto doc = json::Parse(*platform::ReadFileBytes(SaveFile(folder, "0"), nullptr));
    CHECK(doc && doc->Get("kind").AsString() == kSaveKind && doc->Get("globals").Get("score").AsInt() == 42);
    CHECK(doc->Get("globals").Get("home").Size() == 3 && doc->Get("globals").Get("cleared").AsBool());

    // 新しいストアに読み戻す。
    GlobalStore t;
    t.Reset(SomeGlobals());
    std::vector<std::string> warnings;
    CHECK(ReadSave(t, folder, "0", &warnings, &why) && t.Find("score")->v[0] == 42 && t.Find("player")->s == "Z" && warnings.empty());

    // 新しいバージョンのセーブ: この一覧にない変数と独自のキーは、そのまま書き戻す。
    json::Value newer = *doc;
    json::Value g = doc->Get("globals");
    g.Set("gone", json::Value(5));
    g.Set("score", json::Value("not a number"));  // 別の型の値: 読まず (警告する)、そのまま残す
    newer.Set("globals", g);
    newer.Set("playTime", json::Value(123.5));
    CHECK(platform::WriteFileAtomic(SaveFile(folder, "1"), json::Write(newer), &why));
    GlobalStore u;
    u.Reset(SomeGlobals());
    warnings.clear();
    CHECK(ReadSave(u, folder, "1", &warnings, &why) && u.Find("score")->v[0] == 10 && warnings.size() == 1);
    u.Find("speed")->v[0] = 9;
    CHECK(WriteSave(u, folder, "1", &why));
    const auto after = json::Parse(*platform::ReadFileBytes(SaveFile(folder, "1"), nullptr));
    CHECK(after->Get("playTime").AsDouble() == 123.5 && after->Get("globals").Get("gone").AsInt() == 5);
    CHECK(after->Get("globals").Get("speed").AsDouble() == 9 && after->Get("globals").Get("score").AsInt() == 10);
    // 新しいスロットは、最後に読んだセーブの未知のキーを引き継ぐ。
    CHECK(WriteSave(u, folder, "copy", &why));
    CHECK(json::Parse(*platform::ReadFileBytes(SaveFile(folder, "copy"), nullptr))->Get("playTime").AsDouble() == 123.5);

    // スロットは名前で、パスではない。無いものを読むと失敗する。
    CHECK(!WriteSave(s, folder, "../x", &why) && !WriteSave(s, folder, "", &why) && !WriteSave(s, folder, "a/b", &why));
    CHECK(!ReadSave(t, folder, "9", &warnings, &why));
    CHECK(DeleteSave(folder, "0", &why) && !SaveExists(folder, "0"));
    CHECK(!WriteSave(s, fs::path(), "0", &why));  // フォルダー無し: 何もしない
    std::error_code ec;
    fs::remove_all(folder, ec);
}

RM_TEST(Game_QuitThroughTheScriptsPart) {
    // Game::Quit は終了を頼む: ホストはこのフレームの後でゲームを終える (ScriptWorld::quitAsked)。
    ScriptWorld w;
    const RmApi api = MakeScriptApi(w);
    const auto* game = static_cast<const RmGameApi*>(api.part(api.ctx, RM_PART_GAME, RM_PART_GAME_VERSION));
    CHECK(game && !w.quitAsked);
    if (!game) return;
    game->quit(api.ctx);
    CHECK(w.quitAsked);
}

RM_TEST(Globals_ThroughTheScriptsParts) {
    ScriptWorld w;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    w.globals.Reset(SomeGlobals());
    w.saveFolder = TempFolder(L"rm_saves_api_");
    const RmApi api = MakeScriptApi(w);
    const auto* g = static_cast<const RmGlobalsApi*>(api.part(api.ctx, RM_PART_GLOBALS, RM_PART_GLOBALS_VERSION));
    const auto* sv = static_cast<const RmSaveApi*>(api.part(api.ctx, RM_PART_SAVE, RM_PART_SAVE_VERSION));
    CHECK(g && sv);
    if (!g || !sv) return;
    int32_t i = 0;
    CHECK(g->get(api.ctx, "score", RmField_Int, &i) == 1 && i == 10);
    float f = 0;
    CHECK(g->get(api.ctx, "score", RmField_Float, &f) == 1 && f == 10);  // int を float で読む
    f = 2.6f;
    CHECK(g->set(api.ctx, "score", RmField_Float, &f) == 1 && w.globals.Find("score")->v[0] == 3);  // int へ: 整数にする
    int32_t b = 0;
    CHECK(g->get(api.ctx, "cleared", RmField_Bool, &b) == 1 && b == 1);
    float v3[3] = {};
    CHECK(g->get(api.ctx, "home", RmField_Vec3, v3) == 1 && v3[1] == 2);
    const float v2[2] = {7, 8};
    CHECK(g->set(api.ctx, "home", RmField_Vec2, v2) == 1 && w.globals.Find("home")->v[0] == 7 && w.globals.Find("home")->v[2] == 3);
    // 文字: 文字としてだけ。数の変数は、その文字を返す。
    char buf[8];
    CHECK(g->getString(api.ctx, "player", buf, sizeof(buf)) == 3 && std::string(buf) == "A B");
    CHECK(g->setString(api.ctx, "player", "Hello!", 6) == 1 && w.globals.Find("player")->s == "Hello!");
    CHECK(g->getString(api.ctx, "score", buf, sizeof(buf)) == 1 && std::string(buf) == "3");
    CHECK(g->setString(api.ctx, "score", "12", 2) == 1 && w.globals.Find("score")->v[0] == 12);
    CHECK(g->setString(api.ctx, "score", "x", 1) == 0);
    CHECK(g->get(api.ctx, "player", RmField_Int, &i) == 0);
    // 一覧に無い名前: 1 回だけ知らせる。
    CHECK(g->get(api.ctx, "nobody", RmField_Int, &i) == 0 && g->get(api.ctx, "nobody", RmField_Int, &i) == 0);
    CHECK(told.find("「nobody」") != std::string::npos && told.find("「nobody」") == told.rfind("「nobody」"));
    // 部品を通して保存: 書かれる、値が変わる、読み戻せる。
    CHECK(sv->exists(api.ctx, "1") == 0 && sv->write(api.ctx, "1") == 1 && sv->exists(api.ctx, "1") == 1);
    w.globals.Find("score")->v[0] = 0;
    CHECK(sv->read(api.ctx, "1") == 1 && w.globals.Find("score")->v[0] == 12);
    CHECK(sv->remove(api.ctx, "1") == 1 && sv->exists(api.ctx, "1") == 0);
    CHECK(sv->write(api.ctx, "..\\x") == 0 && told.find("Save::Write") != std::string::npos);
    std::error_code ec;
    fs::remove_all(w.saveFolder, ec);
}

RM_TEST(Globals_HeaderOfNames) {
    std::vector<GlobalVariable> list = SomeGlobals();
    list.push_back({"2bad", GlobalType::Int, ""});
    list.push_back({"score", GlobalType::Float, ""});  // 2 回
    std::vector<std::string> skipped;
    const std::string h = GlobalsHeader(list, &skipped);
    CHECK(h.find("namespace Global {") != std::string::npos);
    CHECK(h.find("inline constexpr ::rm::GlobalVar<int> score{\"score\"};") != std::string::npos);
    CHECK(h.find("::rm::GlobalVar<std::string> player{\"player\"}") != std::string::npos);
    CHECK(h.find("::rm::GlobalVar<::rm::Vec3> home{\"home\"}") != std::string::npos);
    CHECK(h.find("::rm::GlobalVar<bool> cleared") != std::string::npos);
    CHECK(h.find("2bad") == std::string::npos && skipped.size() == 2);
    // 初めの値はこの中に入らない (1 つ変えても、作り直しにはならない)。
    list[0].initial = "500";
    CHECK(GlobalsHeader(list) == h);
}
