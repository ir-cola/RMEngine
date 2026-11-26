// カラーテーマ (editor/widgets/Theme.h, ThemeFile.h) のテスト: 組み込みの 3 つ、テーマパックの
// テーマ (plugins/<author>/<name>/themes/ のファイル)、絵の形の部品 (ui/Shape.h)、季節パックの絵の形
// (plugins/rm_engine/theme_seasons/editor/Ornament.h)、探検パックとゲーミングの絵の形 (各 editor/Shapes.h)。

#include "Test.h"
#include "ui/Grain.h"
#include "ui/Shape.h"

#include "editor/widgets/Theme.h"
#include "editor/widgets/ThemeFile.h"
#include "rm_engine/theme_explore/editor/Shapes.h"
#include "rm_engine/theme_gaming/editor/Shapes.h"
#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace rm::editor;
namespace ornament = rm::theme_seasons::ornament;

RM_TEST(Theme_BuiltInThree) {
    const auto list = theme::Themes();
    CHECK(list.size() >= 3);
    CHECK(list[0].id == "Dark" && list[1].id == "Light" && list[2].id == "System");
    for (size_t i = 0; i < 3; ++i) CHECK(list[i].group == 0 && list[i].pack.empty() && !list[i].ornament && !list[i].name.empty());
    // 前の 3 択はこれらの名前で保存されていた: 前の hub.json でもテーマは保たれる。
    CHECK(theme::FindTheme("Dark").palette && theme::FindTheme("Dark").palette->bg == theme::kDark.bg);
    CHECK(!theme::FindTheme("System").palette);
    // 不明 (入れていないパック、新しいバージョンのテーマ、打ち間違い): 最初のテーマ。
    CHECK(&theme::FindTheme("NoSuchTheme") == &theme::Themes()[0]);
    CHECK(&theme::FindTheme("") == &theme::Themes()[0]);
}

// 選んだ所の色の決まり (組み込みの黒と白も)。背景と同じ色はだめ。
RM_TEST(Theme_SelectionStandsOut) {
    CHECK(theme::SelectionStandsOut(theme::kDark) && theme::SelectionStandsOut(theme::kLight));
    theme::Palette same = theme::kLight;
    same.selected = same.bg;
    CHECK(!theme::SelectionStandsOut(same));
    CHECK(theme::ColorDifference(rm::Color::Hex(0x000000), rm::Color::Hex(0xFFFFFF)) > 99.f);
    CHECK(theme::ContrastRatio(rm::Color::Hex(0x000000), rm::Color::Hex(0xFFFFFF)) > 20.9f);
}

RM_TEST(Theme_SystemFollowsWindows) {
    const theme::ThemeInfo& sys = theme::FindTheme("System");
    CHECK(&theme::PaletteOf(sys, true) == &theme::kLight);
    CHECK(&theme::PaletteOf(sys, false) == &theme::kDark);
    CHECK(theme::PaletteOf(theme::FindTheme("Dark"), true).bg == theme::kDark.bg);
    CHECK(theme::kLight.light && !theme::kDark.light);
}

RM_TEST(Theme_PackThemesComeAndGo) {
    theme::ThemeInfo spring;
    spring.id = "Spring";
    spring.name = L"スプリング";
    spring.palette = theme::kLight;
    spring.group = 1;
    spring.aliases = {"Sakura"};
    theme::SetPackThemes({spring});
    CHECK(theme::Themes().size() == 4);
    CHECK(theme::FindTheme("Spring").id == "Spring" && theme::FindTheme("Spring").group == 1);
    CHECK(theme::FindTheme("Sakura").id == "Spring");  // 前の名前 (桜)
    // パックを外した: 最初のテーマ (hub.json は id を保つので、パックを戻せば戻る)。
    theme::SetPackThemes({});
    CHECK(theme::Themes().size() == 3);
    CHECK(&theme::FindTheme("Spring") == &theme::Themes()[0]);
}

// 季節で切り替える (2026-11-26): 月から季節、テーマファイルの follow、従うテーマの色と名前を写す。
RM_TEST(Theme_FollowsTheSeason) {
    using theme::Season;
    const Season want[12] = {Season::Winter, Season::Winter, Season::Spring, Season::Spring, Season::Spring, Season::Summer,
                             Season::Summer, Season::Summer, Season::Fall,   Season::Fall,   Season::Fall,   Season::Winter};
    for (int m = 1; m <= 12; ++m) CHECK(theme::SeasonOfMonth(m) == want[m - 1]);
    CHECK(theme::SeasonOfMonth(0) == Season::Winter && theme::SeasonOfMonth(13) == Season::Winter && theme::SeasonOfMonth(16) == Season::Spring);
    // ファイル: 色の代わりに follow。夜用は省ける。
    const auto doc = rm::json::Parse(R"({"kind": "rm_engine.theme", "format": 1, "id": "BySeason", "name": "季節で切り替える",
        "follow": {"by": "season", "spring": {"morning": "Spring", "night": "SpringNight"}, "summer": {"morning": "Summer"},
                   "fall": {"morning": "Fall"}, "winter": {"morning": "Winter"}}})");
    CHECK(doc.has_value());
    std::string err;
    const auto t = theme::ParseThemeFile(*doc, &err);
    CHECK(t && t->info.follow && !t->info.palette);
    if (!t || !t->info.follow) return;
    CHECK(theme::FollowedIds(*t->info.follow, Season::Spring, theme::DayPart::Night) == std::vector<std::string>({"SpringNight", "Spring"}));
    CHECK(theme::FollowedIds(*t->info.follow, Season::Summer, theme::DayPart::Night) == std::vector<std::string>{"Summer"});
    CHECK(theme::FollowedIds(*t->info.follow, Season::Winter, theme::DayPart::Morning) == std::vector<std::string>{"Winter"});
    for (const char* bad : {R"({"kind": "rm_engine.theme", "id": "A", "name": "A", "follow": {"by": "weekday"}})",
                            R"({"kind": "rm_engine.theme", "id": "A", "name": "A", "follow": {"by": "season", "spring": {"morning": "S"}}})"}) {
        std::string why;
        CHECK(!theme::ParseThemeFile(*rm::json::Parse(bad), &why) && !why.empty());
    }
    // 従うテーマの色・名前を写す。夜用が無ければ朝用。従う先が無いテーマは外す。
    auto themeOf = [](const char* id, const wchar_t* name, uint32_t bg) {
        theme::ThemeInfo i;
        i.id = id;
        i.name = name;
        i.palette = theme::kLight;
        i.palette->bg = rm::Color::Hex(bg);
        return i;
    };
    std::vector<theme::ThemeInfo> pack = {themeOf("Spring", L"スプリング", 0x110000), themeOf("Summer", L"サマー", 0x220000),
                                          themeOf("Fall", L"フォール", 0x330000), themeOf("Winter", L"ウィンター", 0x440000), t->info};
    theme::ThemeInfo lost = t->info;
    lost.id = "Lost";
    lost.follow->seasons[3].morning = "Nowhere";
    pack.push_back(lost);
    for (const auto& [season, bg, name] : {std::tuple{Season::Spring, 0x110000u, L"スプリング"}, std::tuple{Season::Summer, 0x220000u, L"サマー"},
                                           std::tuple{Season::Fall, 0x330000u, L"フォール"}, std::tuple{Season::Winter, 0x440000u, L"ウィンター"}}) {
        std::vector<theme::ThemeInfo> list = pack;
        std::vector<std::string> missing;
        theme::ResolveFollowing(list, season, theme::DayPart::Night, &missing);
        const auto by = std::find_if(list.begin(), list.end(), [](const theme::ThemeInfo& i) { return i.id == "BySeason"; });
        CHECK(by != list.end() && by->palette && by->palette->bg == rm::Color::Hex(bg) && by->followed == name);
        const bool lostGone = std::none_of(list.begin(), list.end(), [](const theme::ThemeInfo& i) { return i.id == "Lost"; });
        CHECK(lostGone == (season == Season::Winter) && missing.size() == (season == Season::Winter ? 1u : 0u));
        // テーマの画面の見本: 4 つの季節の朝用の色 (春 夏 秋 冬 の順)。どれかの季節が無いテーマは区切らない。
        CHECK(by != list.end() && by->seasons.size() == 4);
        if (by != list.end() && by->seasons.size() == 4) {
            CHECK(by->seasons[0].bg == rm::Color::Hex(0x110000) && by->seasons[1].bg == rm::Color::Hex(0x220000) &&
                  by->seasons[2].bg == rm::Color::Hex(0x330000) && by->seasons[3].bg == rm::Color::Hex(0x440000));
        }
        const auto lostAt = std::find_if(list.begin(), list.end(), [](const theme::ThemeInfo& i) { return i.id == "Lost"; });
        CHECK(lostAt == list.end() || lostAt->seasons.empty());
    }
    // 組み込みのテーマにも従える (PC の設定に合わせる: 色の組は無いまま)。
    std::vector<theme::ThemeInfo> builtin = {t->info};
    for (theme::SeasonThemes& s : builtin[0].follow->seasons) s = {"System", ""};
    theme::ResolveFollowing(builtin, Season::Fall, theme::DayPart::Morning);
    CHECK(builtin.size() == 1 && !builtin[0].palette && builtin[0].followed == L"PC の設定に合わせる");
}

RM_TEST(ThemeFile_ReadsColorsAndFillsTheRest) {
    CHECK(theme::ParseHexColor("#FF8000") == rm::Color::Hex(0xFF8000));
    CHECK(theme::ParseHexColor("00000080") == rm::Color::Hex(0x000000, 128.f / 255.f));
    CHECK(!theme::ParseHexColor("#12345") && !theme::ParseHexColor("#GG0000") && !theme::ParseHexColor(""));
    const auto doc = rm::json::Parse(R"({"kind": "rm_engine.theme", "format": 1, "id": "Sea", "name": "海", "light": true,
        "ornament": "waves", "aliases": ["Ocean"], "order": 2, "caption": "#102030",
        "colors": {"bg": "#AABBCC", "shadow": "#00000040"}, "future": 1})");
    CHECK(doc.has_value());
    std::string err;
    const auto t = theme::ParseThemeFile(*doc, &err);
    CHECK(t.has_value());
    if (!t) return;
    CHECK(t->info.id == "Sea" && t->info.name == L"海" && t->order == 2 && t->ornament == "waves");
    CHECK(t->info.aliases == std::vector<std::string>{"Ocean"});
    CHECK(t->info.palette && t->info.palette->light);
    CHECK(t->info.palette->bg == rm::Color::Hex(0xAABBCC));
    CHECK(t->info.palette->shadow == rm::Color::Hex(0x000000, 64.f / 255.f));
    CHECK(t->info.palette->caption == 0x102030u);
    CHECK(t->info.palette->text == theme::kLight.text);  // 省いた: ライトテーマのもの
    // テーマではない、id がない、読めない色: 理由を添えて断る。
    for (const char* bad : {R"({"kind": "other", "id": "A", "name": "A"})", R"({"kind": "rm_engine.theme", "name": "A"})",
                            R"({"kind": "rm_engine.theme", "id": "A", "name": "A", "colors": {"bg": "red"}})"}) {
        std::string why;
        CHECK(!theme::ParseThemeFile(*rm::json::Parse(bad), &why) && !why.empty());
    }
}

// このリポジトリのパックのテーマファイルすべて: 読める、id は重ならない、ライトなものはライト。
RM_TEST(ThemeFile_ThePacks) {
    const std::filesystem::path plugins = std::filesystem::path(__FILE__).parent_path().parent_path() / "plugins";
    std::set<std::string> ids{"Dark", "Light", "System"};
    std::vector<std::string> follows;
    int count = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(plugins, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".json" || it->path().parent_path().filename() != "themes") continue;
        std::ifstream in(it->path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const auto doc = rm::json::Parse(text);
        CHECK(doc.has_value());
        if (!doc) continue;
        std::string err;
        const auto t = theme::ParseThemeFile(*doc, &err);
        CHECK(t.has_value());
        if (!t) continue;
        CHECK(ids.insert(t->info.id).second);
        for (const std::string& a : t->info.aliases) CHECK(ids.insert(a).second);
        ++count;
        if (t->info.follow) {  // (季節で切り替える: 色は従うテーマのもの)
            for (const theme::SeasonThemes& s : t->info.follow->seasons) {
                follows.push_back(s.morning);
                CHECK(!s.night.empty());  // (夜用もある: 2026-11-26)
                follows.push_back(s.night);
            }
            continue;
        }
        // ロゴのインク (目立つ方の白か黒) は、その種類のテーマが期待するもの。
        const rm::Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
        CHECK(theme::InkOn(t->info.palette->sidebar) == (t->info.palette->light ? black : white));
        // 文字は地 (パネル) の上で読める (コントラスト比 4.5 以上)。選んだ所の色ははっきり見える。
        CHECK(theme::ContrastRatio(t->info.palette->text, t->info.palette->panel) >= 4.5f);
        if (!theme::SelectionStandsOut(*t->info.palette)) {
            rmtest::Fail(__FILE__, __LINE__, ("選んだ所の色が目立たない: " + t->info.id).c_str());
        }
    }
    CHECK(count == 16);
    CHECK(follows.size() == 8);
    for (const std::string& id : follows) CHECK(ids.count(id) == 1);  // 従う先はパックにある
    for (const char* id : {"Midnight", "Forest", "Paper", "Spring", "Summer", "Fall", "Winter", "Sakura", "BySeason", "SpringNight", "SummerNight",
                           "FallNight", "WinterNight", "Space", "DeepSea", "Volcano", "Gaming"}) {
        CHECK(ids.count(id) == 1);
    }
}

// 朝か夜か: 立ち上げた時刻。6 時 .. 17 時台は朝、18 時 .. 翌 5 時台は夜。
RM_TEST(Theme_DayPartOfHour) {
    for (int h = 0; h < 24; ++h) CHECK((theme::DayPartOfHour(h) == theme::DayPart::Morning) == (h >= 6 && h < 18));
    CHECK(theme::DayPartOfHour(24 + 7) == theme::DayPart::Morning && theme::DayPartOfHour(-1) == theme::DayPart::Night);
}

namespace {

// パスが届くすべての点 (曲線の制御点も)。
struct Bounds {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int figures = 0;
};
Bounds BoundsOf(const rm::ui::Path& p) {
    Bounds b;
    auto add = [&](rm::Vec2 v) {
        b.x0 = std::min(b.x0, v.x), b.y0 = std::min(b.y0, v.y), b.x1 = std::max(b.x1, v.x), b.y1 = std::max(b.y1, v.y);
    };
    for (const auto& st : p.steps) {
        if (st.op == rm::ui::Path::Op::Begin) ++b.figures;
        add(st.a);
        if (st.op == rm::ui::Path::Op::Cubic) {
            add(st.b);
            add(st.c);
        }
    }
    return b;
}

// 各図形の直線の角の符号付き面積の 2 倍: > 0 = 画面で時計回り。
bool AllClockwise(const rm::ui::Path& p) {
    std::vector<rm::Vec2> fig;
    bool ok = true;
    auto close = [&] {
        float a = 0;
        for (size_t i = 0; i < fig.size(); ++i) {
            const rm::Vec2 u = fig[i], v = fig[(i + 1) % fig.size()];
            a += u.x * v.y - v.x * u.y;
        }
        if (fig.size() >= 3 && a < 0) ok = false;
        fig.clear();
    };
    for (const auto& st : p.steps) {
        if (st.op == rm::ui::Path::Op::Begin) close();
        fig.push_back(st.op == rm::ui::Path::Op::Cubic ? st.c : st.a);
    }
    close();
    return ok;
}

}  // namespace

RM_TEST(Ornament_SakuraShape) {
    using rm::ui::Path;
    const rm::Vec2 c{200, 300};
    const Path flat = ornament::Sakura(c, 100, 0);
    int figures = 0;
    float farthest = 0;
    auto reach = [&](rm::Vec2 v) { farthest = std::max(farthest, std::hypot(v.x - c.x, v.y - c.y)); };
    for (const Path::Step& s : flat.steps) {
        if (s.op == Path::Op::Begin) ++figures;
        reach(s.a);
        if (s.op == Path::Op::Cubic) {
            reach(s.b);
            reach(s.c);
        }
    }
    CHECK(figures == 5);                       // 花びら 5 枚
    CHECK(AllClockwise(flat) && AllClockwise(ornament::Sakura(c, 100, 33)));  // (ほかの形と重ねても穴があかない)
    CHECK(std::fabs(farthest - 100) < 0.01f);  // 先端は半径に届き、それより先には出ない
    // 回さない: 最初の花びらは真上を向き、切れ込みは中心線の上。
    CHECK(flat.steps[3].op == Path::Op::Line);
    CHECK(std::fabs(flat.steps[3].a.x - 200) < 0.01f && std::fabs(flat.steps[3].a.y - 216) < 0.01f);
    // 反時計回りに 10 度回す: その切れ込みは左へ動く (中心から 84 のまま)。
    const rm::Vec2 n = ornament::Sakura(c, 100, 10).steps[3].a;
    CHECK(n.x < 200 - 14 && n.x > 200 - 15);
    CHECK(std::fabs(std::hypot(n.x - c.x, n.y - c.y) - 84) < 0.01f);
}

RM_TEST(Theme_InkOn_WhiteOnDarkBlackOnLight) {
    const rm::Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
    CHECK(theme::InkOn(theme::kDark.sidebar) == white);
    CHECK(theme::InkOn(theme::kLight.sidebar) == black);
    CHECK(theme::InkOn(black) == white && theme::InkOn(white) == black);
}


RM_TEST(Ornament_ShapesStayInTheirPlace) {
    using namespace ornament;
    // 乱数: 同じシードなら同じ数。すべて 0 .. 1 の中。
    Random a{7}, b{7};
    for (int i = 0; i < 100; ++i) {
        const float x = a.Next();
        CHECK(x == b.Next() && x >= 0 && x < 1);
    }
    // 反時計回りに与えたポリゴンは裏返す (重なる図形がつながり、穴をあけないように)。
    rm::ui::Path poly;
    const rm::Vec2 ccw[4] = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
    AddPolygon(poly, ccw);
    CHECK(AllClockwise(poly));

    // シルエット: 半径の中 (茎 / 柄は少し外へ)。すべて時計回り。
    const rm::Vec2 c{500, 500};
    const Bounds leaf = BoundsOf(MapleLeaf(c, 100, 0));
    CHECK(std::fabs(leaf.y0 - 400) < 1.f);         // 上の丸みの先端は半径のところ
    CHECK(leaf.x0 > 390 && leaf.x1 < 610 && leaf.y1 < 570);
    CHECK(AllClockwise(MapleLeaf(c, 100, 33)));
    const Bounds flake = BoundsOf(Snowflake(c, 100, 0));
    CHECK(flake.x0 > 390 && flake.x1 < 610 && flake.y0 > 390 && flake.y1 < 610);
    CHECK(AllClockwise(Snowflake(c, 100, 10)));
    const Bounds sun = BoundsOf(Sun(c, 100, 0));
    CHECK(std::fabs(sun.y0 - 400) < 0.5f && sun.x0 > 390 && sun.x1 < 610 && sun.y1 < 610);  // 上の光線の先端は半径のところ
    CHECK(AllClockwise(Sun(c, 100, 10)));

    // 桜の木は、右寄りの下の縁から生える。樹冠は上の縁と右の縁の先の上にあり、
    // 大きさの約 1.6 倍まで入ってくる。花びらは左へ吹かれる。
    const rm::ui::Rect screen{0, 0, 1000, 700};
    const rm::ui::Path cherry = CherryTree(screen, 200);
    const Bounds tree = BoundsOf(cherry);
    CHECK(tree.x0 > 1000 - 200 * 1.7f && tree.x1 > 1000 && tree.y0 < 0 && tree.y1 > 700);
    CHECK(AllClockwise(cherry));
    // 下のほうには幹だけ (と花びらが 1、2 枚): 細く、右の縁の近くに立つ。
    float lowLeft = 1e9f, lowRight = -1e9f;
    for (const auto& st : cherry.steps) {
        if (st.a.y < 680) continue;
        lowLeft = std::min(lowLeft, st.a.x);
        lowRight = std::max(lowRight, st.a.x);
    }
    CHECK(lowLeft > 1000 - 200 * 1.0f && lowRight < 1000);
    const rm::ui::Path stream = PetalStream(screen, 820, 80, 9, 4);
    const Bounds sb = BoundsOf(stream);
    CHECK(sb.figures > 60 && sb.x1 < 820 + 30 && sb.x0 > -10 && sb.y1 <= 700 + 10);
    int nearTree = 0, farAway = 0;  // 木の近くは濃く、離れるほど薄く
    for (const auto& st : stream.steps) {
        if (st.op != rm::ui::Path::Op::Begin) continue;
        (st.a.x > 820 - 300 ? nearTree : farAway) += 1;
    }
    CHECK(nearTree > farAway);

    // 角のシルエットはどれも同じ箱を埋める (カエデの葉は桜の花と同じ高さ)。
    const rm::ui::Rect box{10, 400, 200, 200};
    for (const rm::ui::Path& sil : {FitInto(Sakura({0, 0}, 1, 10), box), FitInto(MapleLeaf({0, 0}, 1, 10), box),
                                     FitInto(Snowflake({0, 0}, 1, 10), box), FitInto(Sun({0, 0}, 1, 10), box)}) {
        const Bounds sb2 = BoundsOf(sil);
        CHECK(sb2.x0 >= 10 - 0.01f && sb2.x1 <= 210 + 0.01f && sb2.y0 >= 400 - 0.01f && sb2.y1 <= 600 + 0.01f);
        CHECK(std::fabs((sb2.x1 - sb2.x0) - 200) < 0.01f || std::fabs((sb2.y1 - sb2.y0) - 200) < 0.01f);  // 入る限り大きく
        CHECK(std::fabs((sb2.x0 + sb2.x1) * 0.5f - 110) < 0.01f && std::fabs((sb2.y0 + sb2.y1) * 0.5f - 500) < 0.01f);  // 中央に
    }

    // 花びらの舞いは、その領域の中に収まる (花びら 1 枚ぶんの大きさは前後してよい)。
    const rm::ui::Rect area{0, 0, 300, 200};
    const rm::ui::Path petals = Petals(area, 40, 8, 1234);
    const Bounds pb = BoundsOf(petals);
    CHECK(pb.figures == 40 && pb.x0 > -8 && pb.y0 > -8 && pb.x1 < 308 && pb.y1 < 208);

    const rm::ui::Rect win{0, 0, 800, 600};
    // 浜辺: 右下の角だけ。その岸は (800 - 300, 600) から (800, 600 - 200) まで。
    // 砂の城: 土台の上 (その山は少し下、砂の中)。頼まれた幅と高さで。
    const Bounds castle = BoundsOf(SandCastle({400, 500}, 100));
    CHECK(castle.x0 > 400 - 90 && castle.x1 < 400 + 90 && castle.y0 > 500 - 100 && castle.y1 < 500 + 12);
    CHECK(AllClockwise(SandCastle({400, 500}, 100)));
    // 夏のシルエット: 下の縁の盛り上がり。パラソルがその上で一番高い。
    const Bounds scene = BoundsOf(BeachScene({500, 600}, 300));
    CHECK(scene.y1 > 600 && scene.y0 > 600 - 300 * 1.1f && scene.y0 < 600 - 300 * 0.85f);
    CHECK(scene.x0 > 500 - 300 && scene.x1 < 500 + 300 * 0.9f);
    CHECK(AllClockwise(BeachScene({500, 600}, 300)));
    // 木のおおい: 上は濃く、下へは少し。`depth` より下には出ない (葉 1 枚ぶんは前後してよい)。
    int above = 0, below = 0;
    for (const auto& part : Canopy(win, 200, 0.25f, 9)) {
        if (part.steps.empty()) continue;
        CHECK(BoundsOf(part).y1 < 200 + 30);
        for (const auto& st : part.steps) (st.a.y < 60 ? above : below) += st.op == rm::ui::Path::Op::Begin ? 1 : 0;
    }
    CHECK(above + below >= 200 && above > below);  // 800 x 0.25 枚の葉 (それぞれ茎つき)。大半は上の近く
    // 春の花びらも同じ散らばり: 4 色に分かれ、上の近くが多く、`depth` より下には出ない。
    above = below = 0;
    int colors = 0;
    for (const auto& part : PetalCanopy(win, 200, 0.25f, 9)) {
        if (part.steps.empty()) continue;
        ++colors;
        CHECK(BoundsOf(part).y1 < 200 + 20);
        CHECK(AllClockwise(part));
        for (const auto& st : part.steps) (st.a.y < 60 ? above : below) += st.op == rm::ui::Path::Op::Begin ? 1 : 0;
    }
    CHECK(colors == 4 && above + below >= 200 && above > below);

    // 降る雪は領域の中に収まる。同じシードなら同じ雪片。
    const Bounds flakes = BoundsOf(Snowfall({0, 0, 400, 300}, 50, 6, 3));
    CHECK(flakes.figures == 50 && flakes.x0 > -4 && flakes.y0 > -4 && flakes.x1 < 404 && flakes.y1 < 304);
    // ウィンドウの下に積もる雪: 下の縁から `depth` より上にはならず、左右の端まで (少し越えて) 積もる。
    const rm::ui::Rect window{100, 200, 300, 150};
    const rm::ui::Path driftPath = SnowDrift(window, 24);
    const Bounds drift = BoundsOf(driftPath);
    CHECK(drift.y1 == 350 && drift.y0 >= 350 - 24.01f && drift.y0 < 350 - 12.f);
    CHECK(drift.x0 <= 100 && drift.x1 >= 400 && drift.x0 >= 100 - 4.01f && drift.x1 <= 400 + 4.01f);
    CHECK(AllClockwise(driftPath));
    CHECK(SnowDrift(window, 0).steps.empty() && SnowDrift({0, 0, 0, 10}, 6).steps.empty());
    // 両端で下がらない: 端の上の点も、ちゃんと積もった高さ (depth の 4 割より高い)。
    const auto snowAt = [](const rm::ui::Path& p, float x) {  // x に一番近い上の縁の点の、下の縁からの高さ
        float best = 1e9f, h = 0;
        for (size_t i = 0; i + 2 < p.steps.size(); ++i) {  // 最後の 2 つは下の縁
            if (std::fabs(p.steps[i].a.x - x) < best) best = std::fabs(p.steps[i].a.x - x), h = 350 - p.steps[i].a.y;
        }
        return h;
    };
    CHECK(snowAt(driftPath, 100) > 24 * 0.4f && snowAt(driftPath, 400) > 24 * 0.4f);
    // 形は左端からの位置だけで決まる: 幅を変えても、高さ (h) を変えても、動かしても、同じ点が同じ高さ。
    // 広げると右に点が増えるだけ。
    const auto profile = [](const rm::ui::Rect& r) {
        std::vector<std::pair<float, float>> out;  // (左端からの x, 下の縁からの高さ)。下の縁の 2 点は除く
        const rm::ui::Path p = SnowDrift(r, 24);
        for (size_t i = 0; i + 2 < p.steps.size(); ++i) out.push_back({p.steps[i].a.x - r.x, r.y + r.h - p.steps[i].a.y});
        return out;
    };
    const auto narrow = profile({100, 200, 300, 150}), wide = profile({100, 200, 900, 150});
    const auto tall = profile({100, 200, 300, 600}), moved = profile({250, 40, 300, 150});
    const auto samePrefix = [](const std::vector<std::pair<float, float>>& a, const std::vector<std::pair<float, float>>& b) {
        if (b.size() < a.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::fabs(a[i].first - b[i].first) > 1e-3f || std::fabs(a[i].second - b[i].second) > 1e-3f) return false;
        }
        return true;
    };
    CHECK(narrow.size() > 70 && wide.size() > narrow.size());
    CHECK(samePrefix(narrow, wide));
    CHECK(tall.size() == narrow.size() && samePrefix(narrow, tall) && moved.size() == narrow.size() && samePrefix(narrow, moved));
    for (const auto& pt : wide) CHECK(pt.second > 0 && pt.second <= 24.01f);

    // 夏の雲: 平らな底 (中心 + 0.16 * 幅) から上へふくらむ。明るい帯と影の帯は雲の内側。同じシードなら同じ雲。
    const rm::Vec2 cc{600, 300};
    const CloudShapes cloud = Cumulus(cc, 200, 11);
    const Bounds body = BoundsOf(cloud.body);
    CHECK(body.figures == 1 && AllClockwise(cloud.body));
    CHECK(std::fabs(body.y1 - (300 + 0.16f * 200)) < 0.01f);  // 底
    CHECK(body.y0 > 300 - 0.4f * 200 && body.y0 < 300 - 0.2f * 200);  // 上へふくらむ (底からの高さは幅の半分ほど)
    CHECK(body.x0 > 600 - 0.6f * 200 && body.x1 < 600 + 0.6f * 200 && body.x1 - body.x0 > 0.9f * 200);
    int onBase = 0;  // 底は平ら: 底の線の上に点がたくさん並ぶ
    for (const auto& st : cloud.body.steps) onBase += std::fabs(st.a.y - body.y1) < 0.01f ? 1 : 0;
    CHECK(onBase > 20);
    for (const rm::ui::Path* band : {&cloud.light[0], &cloud.light[1], &cloud.light[2], &cloud.shade[0], &cloud.shade[1],
                                     &cloud.shade[2], &cloud.shade[3]}) {
        const Bounds bb = BoundsOf(*band);
        CHECK(bb.figures == 1 && AllClockwise(*band));
        CHECK(bb.x0 >= body.x0 - 0.01f && bb.x1 <= body.x1 + 0.01f && bb.y0 >= body.y0 - 0.01f && bb.y1 <= body.y1 + 0.01f);
    }
    CHECK(BoundsOf(cloud.light[0]).y1 < BoundsOf(cloud.shade[0]).y1);  // 明るい帯は上、影は下
    CHECK(std::fabs(BoundsOf(cloud.shade[3]).y1 - body.y1) < 0.01f && BoundsOf(cloud.shade[3]).y0 > BoundsOf(cloud.shade[0]).y0);
    const CloudShapes same = Cumulus(cc, 200, 11), other = Cumulus(cc, 200, 12);
    CHECK(BoundsOf(same.body).y0 == body.y0 && same.body.steps.size() == cloud.body.steps.size());
    CHECK(BoundsOf(other.body).y0 != body.y0 || BoundsOf(other.body).x0 != body.x0);  // シードで少し違う形
    CHECK(Cumulus(cc, 0, 1).body.steps.empty());

    // つらら (氷柱) は上の縁から下がる。頼まれた長さより長くならず、ところどころに少しだけ。
    const IcicleShapes ice = Icicles(win, 40, 5);
    const Bounds ib = BoundsOf(ice.ice);
    CHECK(ib.y0 >= 2 - 0.01f && ib.y1 <= 2 + 40 + 0.01f && ib.x0 >= 0);
    CHECK(ib.figures >= 1 && ib.figures <= 3 * (800 / 260 + 1));
    CHECK(BoundsOf(ice.snow).y0 >= -5.01f && BoundsOf(ice.shine).y1 <= ib.y1);
    CHECK(AllClockwise(ice.ice) && AllClockwise(ice.shine) && AllClockwise(ice.snow));
    // 各つららの先端 (一番低い点): ウィンドウの幅が何でも、同じ場所に同じつらら。
    // 右の縁をはみ出すものは残す (縁で切れる)。落とさない。
    const auto tips = [](const rm::ui::Path& path, float before) {
        std::vector<rm::Vec2> out;
        rm::Vec2 low{0, -1e9f};
        auto flush = [&] {
            if (low.y > -1e9f && low.x < before) out.push_back(low);
            low = {0, -1e9f};
        };
        for (const auto& st : path.steps) {
            if (st.op == rm::ui::Path::Op::Begin) flush();
            if (st.a.y > low.y) low = st.a;
        }
        flush();
        return out;
    };
    const auto tipsNarrow = tips(Icicles({0, 0, 800, 600}, 40, 5).ice, 800), tipsWide = tips(Icicles({0, 0, 2000, 600}, 40, 5).ice, 800);
    CHECK(!tipsNarrow.empty() && tipsNarrow == tipsWide);
}

RM_TEST(Theme_Grain) {
    // 砂のざらざらの絵: 決めた割合ほどの粒、同じ見た目なら同じ絵、アルファを掛けた色。
    rm::ui::GrainLook g;
    g.light = {1, 1, 1, 0.5f};
    g.dark = {0, 0, 0, 0.25f};
    g.lightShare = 0.1f;
    g.darkShare = 0.1f;
    g.clumps = 0;
    const std::vector<uint32_t> px = rm::ui::GrainPixels(128, g);
    CHECK(px.size() == 128u * 128u);
    size_t light = 0, dark = 0, other = 0;
    for (const uint32_t p : px) {
        if (p == 0) continue;
        const uint32_t a = p >> 24, b = p & 0xFF;
        if (a == 128 && b == 128) ++light;       // 白 x 0.5 (アルファを掛けた)
        else if (a == 64 && b == 0) ++dark;      // 黒 x 0.25
        else ++other;
    }
    CHECK(other == 0);
    CHECK(light > 1300 && light < 2000 && dark > 1300 && dark < 2000);  // 16384 の 1 割ほどずつ
    CHECK(rm::ui::GrainPixels(128, g) == px);
    g.seed = 2;
    CHECK(rm::ui::GrainPixels(128, g) != px);
    CHECK(rm::ui::GrainPixels(0, g).empty());
}

RM_TEST(Theme_Susuki) {
    // ススキ: 根元の上に立ち (下の辺より下へは出ない)、高さまで。穂は根元より左へ垂れる。時計回り。
    const rm::Vec2 base{900, 600};
    using namespace rm::theme_seasons::ornament;
    const rm::ui::Path s = Susuki(base, 300, 7);
    const Bounds b = BoundsOf(s);
    CHECK(b.figures > 100);
    CHECK(b.y1 <= 600 + 3 && b.y0 >= 600 - 300 - 20);
    CHECK(b.x0 < 900 - 300 * 0.35f);  // 穂は左へ
    CHECK(b.x1 < 900 + 300 * 0.2f);  // 右へはあまり出ない
    CHECK(AllClockwise(s));
    CHECK(BoundsOf(Susuki(base, 300, 7)).x0 == b.x0);  // 同じ種なら同じ形
}

RM_TEST(Theme_Snowman) {
    using namespace rm::theme_seasons::ornament;
    // 雪だるま: 底が base、高さの中に収まる。丸 2 つ・バケツ・縁。どれも時計回り。
    const SnowmanShapes s = Snowman({500, 400}, 60);
    const Bounds body = BoundsOf(s.body), bucket = BoundsOf(s.bucket);
    CHECK(body.figures == 2 && bucket.figures == 1 && BoundsOf(s.rim).figures == 1);
    // 輪郭: 太らせた丸は同じ所で少し大きい。
    const Bounds grown = BoundsOf(Snowman({500, 400}, 60, 1.5f).body);
    CHECK(std::abs(grown.y1 - 401.5f) < 0.5f && grown.x0 < body.x0 - 1.f);
    CHECK(std::abs(body.y1 - 400) < 0.5f && body.y0 > 400 - 60);
    CHECK(bucket.y0 >= 400 - 60 - 2 && bucket.y1 < body.y0 + 30);  // バケツは頭の上
    CHECK(AllClockwise(s.body) && AllClockwise(s.bucket) && AllClockwise(s.rim));
    // 積もった雪の高さは SnowDrift の上の線と同じ。
    CHECK(SnowDriftHeight(100, 24) > 24 * 0.45f && SnowDriftHeight(100, 24) < 24 * 0.99f);
}

namespace {
// 図形が `box` の中 (少しの余り `slack` まで) にあり、図形が 1 つ以上で、すべて時計回り。
bool InsideAndClockwise(const rm::ui::Path& p, float x0, float y0, float x1, float y1, float slack = 0.5f) {
    const Bounds b = BoundsOf(p);
    return b.figures > 0 && b.x0 >= x0 - slack && b.y0 >= y0 - slack && b.x1 <= x1 + slack && b.y1 <= y1 + slack && AllClockwise(p);
}
}  // namespace

// 核の部品 (ui/Shape.h): 星・散らした粒・下の波。
RM_TEST(Shape_StarDotsWaves) {
    using namespace rm::ui::shape;
    rm::ui::Path star;
    AddStar(star, {100, 100}, 5, 50, 20);
    const Bounds sb = BoundsOf(star);
    CHECK(std::fabs(sb.y0 - 50) < 0.01f && sb.x0 > 50 && sb.x1 < 150 && sb.y1 < 150 && AllClockwise(star));
    CHECK(star.steps.size() == 10);  // 先 5 つとへこみ 5 つ
    const rm::ui::Rect area{10, 20, 300, 200};
    const rm::ui::Path dots = Dots(area, 40, 6, 3);
    CHECK(BoundsOf(dots).figures == 40 && InsideAndClockwise(dots, 10 - 3, 20 - 3, 310 + 3, 220 + 3));
    CHECK(BoundsOf(Dots(area, 40, 6, 3)).x0 == BoundsOf(dots).x0);  // 同じ種なら同じ
    // 波: 下の端から depth まで。左右と下は 2 ピクセル外まで。左端からの形は幅を変えても同じ。
    const rm::ui::Rect win{0, 0, 800, 600};
    const rm::ui::Path wave = WavyBottom(win, 40, 0);
    const Bounds wb = BoundsOf(wave);
    CHECK(wb.figures == 1 && wb.x0 >= -2.01f && wb.x1 <= 802.01f && wb.y1 <= 602.01f && wb.y0 >= 600 - 40 - 0.01f && wb.y0 < 600 - 30);
    CHECK(AllClockwise(wave));
    const rm::ui::Path narrow = WavyBottom({0, 0, 400, 600}, 40, 0);
    CHECK(narrow.steps[5].a.y == wave.steps[5].a.y);
}

// 季節の夜用の形: 団子・うちわ・月見団子・ツリーは半径の中、時計回り。紐はたるみごとに下げる所があり、上の端の近く。
RM_TEST(Ornament_NightShapes) {
    using namespace ornament;
    const rm::Vec2 c{500, 500};
    CHECK(InsideAndClockwise(HanamiDango(c, 100, 0), 400, 400, 600, 600, 2.f));
    CHECK(InsideAndClockwise(HanamiDango(c, 100, -30), 400, 400, 600, 600, 2.f));
    CHECK(InsideAndClockwise(TsukimiDango(c, 100), 400, 400, 600, 600, 2.f));
    CHECK(std::fabs(BoundsOf(TsukimiDango(c, 100)).y0 - 400) < 1.f);  // てっぺんは半径の所
    CHECK(InsideAndClockwise(ChristmasTree(c, 100, 0), 400, 400, 600, 600, 2.f));
    CHECK(std::fabs(BoundsOf(ChristmasTree(c, 100, 0)).y0 - 400) < 1.f);  // 星の先は半径の所
    // 団子は串の上の端に寄る: 一番上の団子のてっぺんは半径の近く、下の 3 割は串だけ。
    const rm::ui::Path dango = HanamiDango(c, 100, 0);
    CHECK(BoundsOf(dango).y0 < 400 + 8);
    int low = 0;
    for (const auto& st : dango.steps) low += st.a.y > 500 + 40 && std::fabs(st.a.x - 500) > 6 ? 1 : 0;
    CHECK(low == 0);
    // 左下の影の案: どれも半径の中 (少しの余り)、時計回り。
    int which = 0;
    for (const rm::ui::Path& sil : {Bonbori(c, 100, 0), SakuraBranch(c, 100, 0), TokkuriSakazuki(c, 100), Goldfish(c, 100, 0), Furin(c, 100),
                                     Kakigori(c, 100), MosquitoCoil(c, 100), ChristmasTree(c, 100, 0)}) {
        const Bounds b = BoundsOf(sil);
        if (!InsideAndClockwise(sil, 380, 380, 620, 620, 4.f)) {
            const std::string why = "影の案 " + std::to_string(which) + " " + std::to_string(b.x0) + "," + std::to_string(b.y0) + " .. " +
                                    std::to_string(b.x1) + "," + std::to_string(b.y1) + (AllClockwise(sil) ? "" : " 反時計回り");
            rmtest::Fail(__FILE__, __LINE__, why.c_str());
        }
        ++which;
    }
    // 夏の夜の別案: 半径の中。穴 (種・ビー玉・模様) があるので、時計回りでない図形もある。
    for (const rm::ui::Path& sil : {SenkoHanabi(c, 100), Ramune(c, 100), Watermelon(c, 100), WaterYoyo(c, 100)}) {
        const Bounds b = BoundsOf(sil);
        CHECK(b.figures >= 2 && b.x0 > 380 && b.x1 < 620 && b.y0 > 380 && b.y1 < 620);
    }
    CHECK(!AllClockwise(Watermelon(c, 100)) && BoundsOf(Watermelon(c, 100)).figures == 1 + 1 + 7);  // 半月・境目の線・種 7 つ
    // 花火 (リアル調): 菊・柳・牡丹。光の筋と先の星は半径の近くまで (柳は大きく垂れる)。
    for (const FireworkKind k : {FireworkKind::Chrysanthemum, FireworkKind::Willow, FireworkKind::Peony}) {
        const FireworkShapes f = FireworkShell(c, 100, k, 3);
        const Bounds all = BoundsOf(f.tips);
        CHECK(all.figures > 30 && AllClockwise(f.trails) && AllClockwise(f.tips) && AllClockwise(f.glow[0]));
        CHECK(all.x0 > 380 && all.x1 < 620 && all.y0 > 380 && all.y1 < 690);
    }
    CHECK(BoundsOf(FireworkShell(c, 100, FireworkKind::Chrysanthemum, 3).trails).figures == 72);
    // かまくら: 地面の上の、幅 1.6 h・高さ h のドーム。入り口と灯りはドームの中。
    const KamakuraShapes hut = Kamakura({500, 600}, 50);
    const Bounds dome = BoundsOf(hut.dome), door = BoundsOf(hut.door), light = BoundsOf(hut.light);
    CHECK(std::fabs(dome.y0 - 550) < 0.5f && dome.y1 <= 600.5f && std::fabs(dome.x1 - dome.x0 - 80) < 1);
    CHECK(door.x0 > dome.x0 && door.x1 < dome.x1 && door.y0 > dome.y0 && light.x0 > door.x0 && light.y0 > door.y0);
    CHECK(AllClockwise(hut.dome) && AllClockwise(hut.door) && AllClockwise(hut.light));
    // サンタとトナカイ: 左上へ飛ぶ (左の端ほど上)。全長は size の 2 倍くらいまで。
    const rm::ui::Path santa = SantaSleigh(c, 100, -14);
    const Bounds sb = BoundsOf(santa);
    CHECK(sb.x1 - sb.x0 < 220 && sb.x1 - sb.x0 > 150 && AllClockwise(santa));
    float leftY = 0, rightY = 0;
    int nl = 0, nr = 0;
    for (const auto& st : santa.steps) {
        if (st.a.x < sb.x0 + 30) leftY += st.a.y, ++nl;
        if (st.a.x > sb.x1 - 30) rightY += st.a.y, ++nr;
    }
    CHECK(nl > 0 && nr > 0 && leftY / nl < rightY / nr);
    const rm::ui::Rect win{0, 0, 1000, 600};
    const GarlandShapes g = Garland(win, 250, 30, 3, 1.5f);
    CHECK(g.points.size() == 4 * 3);  // 0, 250, 500, 750 から 4 つのたるみ
    for (const rm::Vec2 v : g.points) CHECK(v.y > 0 && v.y <= 1 + 30 + 0.01f && v.x > 0 && v.x < 1000);
    CHECK(InsideAndClockwise(g.cord, -2, -2, 1002, 33));
    // 風に吹かれる花びら: 右から左へ (wind < 0) なら、右の半分に多い (風上ほど多い)。左から右なら左に多い。
    for (const float wind : {-1.f, 1.f}) {
        const PetalWindShapes pw = PetalWind(win, 500, 0.3f, wind, 7);
        int right = 0, left = 0;
        for (const rm::ui::Path& p : pw.petals) {
            for (const auto& st : p.steps) {
                if (st.op == rm::ui::Path::Op::Begin) (st.a.x > 500 ? right : left) += 1;
            }
            CHECK(AllClockwise(p));
        }
        CHECK(wind < 0 ? right > left : left > right);
        CHECK(BoundsOf(pw.streaks).figures == 14 && AllClockwise(pw.streaks));
    }
}

// 探検パックの形。
RM_TEST(Explore_Shapes) {
    using namespace rm::theme_explore::shapes;
    const rm::Vec2 c{500, 500};
    CHECK(InsideAndClockwise(Rocket(c, 100, 0), 440, 400, 560, 603, 2.f));
    CHECK(std::fabs(BoundsOf(Rocket(c, 100, 0)).y0 - 400) < 0.5f);  // 先は半径の所
    CHECK(InsideAndClockwise(Jellyfish(c, 100, 0), 400, 400, 600, 600, 8.f));
    CHECK(InsideAndClockwise(Flame(c, 100, 0), 400, 400, 600, 600, 2.f));
    CHECK(std::fabs(BoundsOf(Flame(c, 100, 0)).y0 - 400) < 1.f);
    // 惑星: 輪は球の 2.2 倍まで。奥の半分は上、手前の半分は下 (傾けなければ)。
    const PlanetShapes planet = RingedPlanet(c, 50, 0);
    const Bounds back = BoundsOf(planet.ringBack), front = BoundsOf(planet.ringFront);
    CHECK(std::fabs(back.x1 - 610) < 0.5f && std::fabs(back.x0 - 390) < 0.5f && back.y1 <= 500.5f && front.y0 >= 499.5f);
    CHECK(AllClockwise(planet.ringBack) && AllClockwise(planet.ringFront) && AllClockwise(planet.body));
    CHECK(InsideAndClockwise(ShootingStar(c, 100, 0), 490, 400, 510, 510));  // 尾は上へ
    const rm::ui::Rect win{0, 0, 1200, 800};
    for (const rm::ui::Path& n : Nebula(c, 200, 3)) CHECK(InsideAndClockwise(n, 200, 300, 800, 700));
    CHECK(BoundsOf(Sparkles(win, 12, 16, 2)).figures == 12);
    const std::vector<rm::ui::Path> rays = LightRays(win, 4, 1);
    CHECK(rays.size() == 4);
    for (const rm::ui::Path& r : rays) CHECK(InsideAndClockwise(r, 0, -3, 1200 + 800 * 0.3f + 200, 800));
    const BubbleShapes bubbles = Bubbles(win, 20, 18, 4);
    CHECK(BoundsOf(bubbles.body).figures == 20 && BoundsOf(bubbles.shine).figures == 20 && AllClockwise(bubbles.body));
    const rm::ui::Path kelp = Kelp({1000, 800}, 300, 6);
    const Bounds kb = BoundsOf(kelp);
    CHECK(kb.figures > 0 && AllClockwise(kelp));
    CHECK(kb.x0 > 850 && kb.x1 < 1150);
    CHECK(kb.y0 >= 800 - 300 - 10 && kb.y1 <= 806);
    CHECK(BoundsOf(FishSchool(c, 200, 10, 20, 9)).figures == 20 && AllClockwise(FishSchool(c, 200, 10, 20, 9)));
    // 火山: 下の端の上に立つ山。火口は山のてっぺんの近く。照りと煙は火口の上。
    const VolcanoShapes v = Volcano({900, 800}, 300, 2);
    const Bounds m = BoundsOf(v.mountain);
    CHECK(m.y1 <= 806.01f && std::fabs(m.y0 - 500) < 0.5f && AllClockwise(v.mountain) && AllClockwise(v.lava));
    CHECK(std::fabs(v.crater.x - 900) < 0.01f && v.crater.y > 500 && v.crater.y < 530);
    const Bounds smoke = BoundsOf(Smoke(v.crater, 300, 1));
    CHECK(smoke.y1 < v.crater.y + 30 && smoke.y0 < v.crater.y - 250 && smoke.x0 < v.crater.x - 100);  // 上って左へ
    const rm::ui::Path embers = Embers(win, 50, 8, 3);
    CHECK(BoundsOf(embers).figures == 50 && AllClockwise(embers));
}

// ゲーミングの形と虹の色。
RM_TEST(Gaming_Shapes) {
    using namespace rm::theme_gaming::shapes;
    auto near = [](rm::Color a, rm::Color b) { return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f; };
    CHECK(near(Spectrum(0), {1, 0, 0, 1}) && near(Spectrum(0.2f), {1, 1, 0, 1}) && near(Spectrum(0.4f), {0, 1, 0, 1}));
    CHECK(near(Spectrum(0.8f), {0, 0, 1, 1}) && near(Spectrum(1.f), {1, 0, 0, 1}) && near(Spectrum(-0.6f), {0, 1, 0, 1}));
    CHECK(Spectrum(0.5f, 0.3f).a == 0.3f);
    const rm::ui::Rect win{0, 0, 1000, 600};
    const GridShapes g = NeonGrid(win, 400, 25);
    CHECK(InsideAndClockwise(g.lines, -1500, 399, 2500, 603));  // (縦の線はウィンドウの左右の外まで広がる: 描く側が切る)
    const Bounds hb = BoundsOf(g.horizonLine);
    CHECK(hb.figures == 1 && std::fabs(hb.y0 - 399) < 0.01f);
    CHECK(BoundsOf(NeonGrid(win, 700, 25).lines).figures == 0);  // 地平線が下の端より下: 何も無い
    CHECK(BoundsOf(Pixels(win, 30, 4, 1)).figures == 30 && AllClockwise(Pixels(win, 30, 4, 1)));
    CHECK(InsideAndClockwise(Controller({500, 500}, 100, 0), 400, 400, 600, 600, 2.f));
}



