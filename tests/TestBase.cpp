// L0 base のテスト: UTF 変換、JSON、ファイル名。

#include "Test.h"

#include "base/FileName.h"
#include "base/Json.h"
#include "base/Utf.h"

#include <chrono>
#include <format>
#include <string>

using namespace rm;

// --- UTF ---------------------------------------------------------------------

RM_TEST(Utf_AsciiAndJapaneseRoundTrip) {
    CHECK(Utf8ToWide("abc") == L"abc");
    CHECK(Utf8ToWide("\xE6\x97\xA5\xE6\x9C\xAC") == L"\u65E5\u672C");  // 日本
    CHECK(WideToUtf8(L"\u65E5\u672C") == "\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK(Utf8ToWide("") == L"");
    CHECK(WideToUtf8(L"") == "");
}

RM_TEST(Utf_SurrogatePair) {
    const std::string grin = "\xF0\x9F\x98\x80";  // U+1F600
    const std::wstring w = Utf8ToWide(grin);
    CHECK(w.size() == 2);
    CHECK(w[0] == 0xD83D && w[1] == 0xDE00);
    CHECK(WideToUtf8(w) == grin);
}

RM_TEST(Utf_InvalidBecomesReplacement) {
    CHECK(Utf8ToWide("\xFF") == L"\xFFFD");
    CHECK(Utf8ToWide("\xC0\xAF") == L"\xFFFD\xFFFD");          // 冗長な '/'
    CHECK(Utf8ToWide("\xE3\x81") == L"\xFFFD\xFFFD");          // 途中で切れている
    CHECK(Utf8ToWide("\xED\xA0\x80") == L"\xFFFD\xFFFD\xFFFD");  // エンコードされたサロゲート
    CHECK(Utf8ToWide("a\xFFz") == L"a\xFFFDz");
    const wchar_t lone[] = {0xD83D, L'x', 0};
    CHECK(WideToUtf8(lone) == "\xEF\xBF\xBDx");
}

RM_TEST(Utf_Path) {
    const std::string s = "C:\\\xE4\xBD\x9C\xE5\x93\x81\\a.rmpf";  // C:\作品\a.rmpf
    CHECK(PathToUtf8(PathFromUtf8(s)) == s);
    CHECK(PathFromUtf8(s).filename().native() == L"a.rmpf");
}

// --- JSON --------------------------------------------------------------------

RM_TEST(Json_WriteIsStableAndOrdered) {
    const std::string text =
        "{\n"
        "  \"kind\": \"rm_engine.project\",\n"
        "  \"formatVersion\": 1,\n"
        "  \"name\": \"\xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0\",\n"
        "  \"ratio\": 0.5,\n"
        "  \"whole\": 2.0,\n"
        "  \"on\": true,\n"
        "  \"none\": null,\n"
        "  \"list\": [\n"
        "    1,\n"
        "    \"two\"\n"
        "  ],\n"
        "  \"empty\": {},\n"
        "  \"emptyList\": []\n"
        "}\n";
    auto v = json::Parse(text);
    CHECK(v.has_value());
    CHECK(json::Write(*v) == text);  // バイト単位で同じになる往復
    CHECK(v->Members()[0].first == "kind");
    CHECK(v->Get("formatVersion").IsInt());
    CHECK(v->Get("whole").GetType() == json::Type::Double);
    CHECK(v->Get("list").Size() == 2);
}

RM_TEST(Json_UnknownKeysArePreserved) {
    auto v = json::Parse(R"({"a":1,"future":{"x":[1,2]},"b":"keep"})");
    CHECK(v.has_value());
    v->Set("a", 42);         // 知っているキーはその場で変える
    v->Set("added", "new");  // 新しいキーは末尾に足す
    CHECK(json::Write(*v, false) == R"({"a":42,"future":{"x":[1,2]},"b":"keep","added":"new"})");
}

RM_TEST(Json_Numbers) {
    auto v = json::Parse(R"([0,-7,9007199254740993,1.5,-0.25,1e3,2E-2])");
    CHECK(v.has_value());
    CHECK(v->At(0).AsInt() == 0);
    CHECK(v->At(1).AsInt() == -7);
    CHECK(v->At(2).AsInt() == 9007199254740993ll);  // double の精度を超える値も、正確に保つ
    CHECK(v->At(3).AsDouble() == 1.5);
    CHECK(v->At(4).AsDouble() == -0.25);
    CHECK(v->At(5).AsDouble() == 1000.0);
    CHECK(v->At(6).AsDouble() == 0.02);
    const std::string once = json::Write(*v, false);
    CHECK(json::Write(*json::Parse(once), false) == once);  // 2 回目の書き込みからは変わらない
}

RM_TEST(Json_Strings) {
    auto v = json::Parse(R"(["a\"b\\c\n\t", "\u3042", "\ud83d\ude00", "\ud83d", "/\/"])");
    CHECK(v.has_value());
    CHECK(v->At(0).AsString() == "a\"b\\c\n\t");
    CHECK(v->At(1).AsString() == "\xE3\x81\x82");      // あ
    CHECK(v->At(2).AsString() == "\xF0\x9F\x98\x80");  // U+1F600
    CHECK(v->At(3).AsString() == "\xEF\xBF\xBD");      // 単独のサロゲート -> U+FFFD
    CHECK(v->At(4).AsString() == "//");
    json::Value s("ctl\x01");
    CHECK(json::Write(s, false) == "\"ctl\\u0001\"");
}

RM_TEST(Json_Errors) {
    json::ParseError e;
    CHECK(!json::Parse("{", &e));
    CHECK(!json::Parse("[1,]", &e));
    CHECK(!json::Parse("{\"a\" 1}", &e));
    CHECK(!json::Parse("tru", &e));
    CHECK(!json::Parse("\"abc", &e));
    CHECK(!json::Parse("1 2", &e));
    CHECK(!json::Parse("01", &e));
    CHECK(!json::Parse("", &e));
    CHECK(!json::Parse(std::string(1000, '['), &e));  // 深さの上限があり、スタックオーバーフローしない
    CHECK(!json::Parse("{\n  \"a\": 1,\n  x\n}", &e));
    CHECK(e.line == 3 && e.column == 3);
    CHECK(!e.message.empty());
}

RM_TEST(Json_BomAndDuplicates) {
    auto v = json::Parse("\xEF\xBB\xBF{\"a\":1,\"b\":2,\"a\":3}");
    CHECK(v.has_value());
    CHECK(v->Size() == 2);
    CHECK(v->Members()[0].first == "a");
    CHECK(v->Get("a").AsInt() == 3);
}

RM_TEST(Json_AccessorsOnWrongType) {
    json::Value n;
    CHECK(n.AsString().empty());
    CHECK(n.Get("x").IsNull());
    CHECK(n.Size() == 0);
    CHECK(n.AsInt(5) == 5);
    json::Value arr = json::Value::MakeArray();
    arr.Push(1);
    CHECK(arr.Find("x") == nullptr);
    CHECK(!arr.Erase("x"));
}

// --- ファイル名 --------------------------------------------------------------

RM_TEST(FileName_Validation) {
    CHECK(ValidateFileName("MyGame") == FileNameError::None);
    CHECK(ValidateFileName("\xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0 2") == FileNameError::None);  // "ゲーム 2"
    CHECK(ValidateFileName("Level=1") == FileNameError::None);  // '=' は使える (前のエンジンのバグ #11)
    CHECK(ValidateFileName("CONSOLE") == FileNameError::None);
    CHECK(ValidateFileName("") == FileNameError::Empty);
    CHECK(ValidateFileName("a<b") == FileNameError::InvalidChar);
    CHECK(ValidateFileName("a/b") == FileNameError::InvalidChar);
    CHECK(ValidateFileName("tab\there") == FileNameError::InvalidChar);
    CHECK(ValidateFileName("CON") == FileNameError::ReservedName);
    CHECK(ValidateFileName("con.txt") == FileNameError::ReservedName);
    CHECK(ValidateFileName("Com1") == FileNameError::ReservedName);
    CHECK(ValidateFileName("LPT9.x") == FileNameError::ReservedName);
    CHECK(ValidateFileName("abc.") == FileNameError::TrailingDotOrSpace);
    CHECK(ValidateFileName("abc ") == FileNameError::TrailingDotOrSpace);
    CHECK(ValidateFileName(" abc") == FileNameError::LeadingSpace);
    CHECK(ValidateFileName(std::string(64, 'a')) == FileNameError::None);
    CHECK(ValidateFileName(std::string(65, 'a')) == FileNameError::TooLong);
}

RM_TEST(Json_BigObjectsStayFast) {
    // 200,000 個のキー: 読み込み、詰め、探すのを直線的に行う (キーごとにリストを探すと 2 乗に
    // なる: 4e10 回の比較、何分もかかる)。
    std::string text = "{";
    for (int i = 0; i < 200000; ++i) text += std::format("{}\"k{}\": {}", i ? "," : "", i, i);
    text += "}";
    const auto start = std::chrono::steady_clock::now();
    const auto v = json::Parse(text);
    CHECK(v && v->Size() == 200000);
    json::Value filled = json::Value::MakeObject();
    for (int i = 0; i < 200000; ++i) filled.Set(std::format("k{}", i), i);
    int64_t sum = 0;
    for (int i = 0; i < 200000; i += 7) sum += v->Get(std::format("k{}", i)).AsInt();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 10.0);
    CHECK(sum > 0 && filled.Get("k199999").AsInt() == 199999 && !filled.Find("k200000"));
    CHECK(json::Write(*v, false) == json::Write(*json::Parse(json::Write(*v, false)), false));
}

RM_TEST(Json_BigObjectIndexFollowsChanges) {
    json::Value o = json::Value::MakeObject();
    for (int i = 0; i < 40; ++i) o.Set(std::format("k{}", i), i);
    CHECK(o.Get("k30").AsInt() == 30);
    // その場で置き換え (順序は保つ)、消し、また設定する (末尾に付く)。
    o.Set("k30", 300);
    CHECK(o.Get("k30").AsInt() == 300 && o.Members()[30].first == "k30");
    CHECK(o.Erase("k5") && !o.Find("k5") && o.Get("k6").AsInt() == 6 && o.Size() == 39);
    o.Set("k5", 5);
    CHECK(o.Members().back().first == "k5" && o.Get("k5").AsInt() == 5);
    // Members() を通して外から変えても、ちゃんと見つかる。
    o.Members().erase(o.Members().begin());  // k0 が消えた
    CHECK(!o.Find("k0") && o.Get("k1").AsInt() == 1);
    o.Members()[0].first = "renamed";
    CHECK(o.Get("renamed").AsInt() == 1 && !o.Find("k1"));
    // コピーは自分のものを持つ (キャッシュは共有しない)。
    json::Value c = o;
    c.Set("only_in_copy", 1);
    CHECK(c.Find("only_in_copy") && !o.Find("only_in_copy"));
    // 大きなオブジェクトで繰り返されたキー: 最後の値が、最初の場所に入る (小さなものと同じ)。
    std::string text = "{";
    for (int i = 0; i < 30; ++i) text += std::format("\"k{}\": {},", i, i);
    text += "\"k3\": 33}";
    const auto p = json::Parse(text);
    CHECK(p && p->Size() == 30 && p->Get("k3").AsInt() == 33 && p->Members()[3].first == "k3");
}
