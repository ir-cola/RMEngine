// エディターの言語切り替え (editor/lang) のテスト: カタログ、Tr / TrF と、そのフォールバック。

#include "Test.h"

#include "editor/hub/Updates.h"
#include "editor/lang/Lang.h"

using namespace rm;
using namespace rm::editor::lang;

namespace {

struct EnglishFor {
    explicit EnglishFor(const char* json) {
        EnglishCatalog().Clear();
        EnglishCatalog().Add(*json::Parse(json));
        SetLanguage(Language::English);
    }
    ~EnglishFor() {
        SetLanguage(Language::Japanese);
        EnglishCatalog().Clear();
    }
};

}  // namespace

RM_TEST(Lang_Catalog_SkipsBadEntries) {
    Catalog c;
    CHECK(c.Add(*json::Parse(R"({"保存":"Save","空":"","数":1,"":"x"})")) == 1);
    CHECK(c.Find("保存") && *c.Find("保存") == "Save");
    CHECK(!c.Find("空") && !c.Find("数"));
    CHECK(c.Add(*json::Parse("[1,2]")) == 0);
}

RM_TEST(Lang_Tr_EnglishWhenKnown_JapaneseOtherwise) {
    CHECK(Tr(L"保存") == L"保存");  // 日本語が既定
    {
        EnglishFor en(R"({"保存":"Save","「{}」を削除":"Delete \"{}\"","{} 個":"{} items","壊れた {}":"Broken {} {}"})");
        CHECK(Tr(L"保存") == L"Save");
        CHECK(Tr(L"知らない文") == L"知らない文");  // 英語が無い: 日本語のまま
        CHECK(TrU8("保存") == "Save");
        CHECK(TrF(L"「{}」を削除", std::wstring(L"Cube")) == L"Delete \"Cube\"");
        CHECK(TrF(L"{} 個", 3) == L"3 items");
        CHECK(TrU8F("「{}」を削除", std::string("Cube")) == "Delete \"Cube\"");
        // 別の引数を求める翻訳は、日本語の書式に戻る。
        CHECK(TrF(L"壊れた {}", 1) == L"壊れた 1");
    }
    CHECK(Tr(L"保存") == L"保存");
}

RM_TEST(Updates_Parse_InTheChosenLanguage) {
    const auto v = *json::Parse(R"({"kind":"rm_engine.updates","updates":[
        {"version":"0.0.2","date":"2026-12-01","items":[{"ja":"新しい","en":"New"},{"ja":"英語なし"}]},
        {"version":"0.0.1","date":"2026-11-01","items":[]}]})");
    auto ja = rm::editor::ParseUpdates(v, false);
    CHECK(ja.size() == 1);  // 項目の無い登録は外す
    CHECK(ja[0].version == "0.0.2" && ja[0].items.size() == 2 && ja[0].items[0] == "新しい");
    auto en = rm::editor::ParseUpdates(v, true);
    CHECK(en[0].items[0] == "New" && en[0].items[1] == "英語なし");  // 英語が無い: 日本語
    CHECK(rm::editor::ParseUpdates(*json::Parse(R"({"kind":"x"})"), false).empty());
}
