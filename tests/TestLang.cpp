// Tests for the editor's language switch (editor/lang): the catalog, Tr / TrF and their fallbacks.

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
    CHECK(Tr(L"保存") == L"保存");  // Japanese is the default
    {
        EnglishFor en(R"({"保存":"Save","「{}」を削除":"Delete \"{}\"","{} 個":"{} items","壊れた {}":"Broken {} {}"})");
        CHECK(Tr(L"保存") == L"Save");
        CHECK(Tr(L"知らない文") == L"知らない文");  // no English: stays Japanese
        CHECK(TrU8("保存") == "Save");
        CHECK(TrF(L"「{}」を削除", std::wstring(L"Cube")) == L"Delete \"Cube\"");
        CHECK(TrF(L"{} 個", 3) == L"3 items");
        CHECK(TrU8F("「{}」を削除", std::string("Cube")) == "Delete \"Cube\"");
        // A translation that wants other arguments falls back to the Japanese format.
        CHECK(TrF(L"壊れた {}", 1) == L"壊れた 1");
    }
    CHECK(Tr(L"保存") == L"保存");
}

RM_TEST(Updates_Parse_InTheChosenLanguage) {
    const auto v = *json::Parse(R"({"kind":"rm_engine.updates","updates":[
        {"version":"0.0.2","date":"2026-12-01","items":[{"ja":"新しい","en":"New"},{"ja":"英語なし"}]},
        {"version":"0.0.1","date":"2026-11-01","items":[]}]})");
    auto ja = rm::editor::ParseUpdates(v, false);
    CHECK(ja.size() == 1);  // an entry without items is left out
    CHECK(ja[0].version == "0.0.2" && ja[0].items.size() == 2 && ja[0].items[0] == "新しい");
    auto en = rm::editor::ParseUpdates(v, true);
    CHECK(en[0].items[0] == "New" && en[0].items[1] == "英語なし");  // no English: the Japanese
    CHECK(rm::editor::ParseUpdates(*json::Parse(R"({"kind":"x"})"), false).empty());
}
