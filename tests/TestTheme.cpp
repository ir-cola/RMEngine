// カラーテーマ (editor/widgets/Theme.h, ThemeFile.h) のテスト: 組み込みの 3 つ、テーマパックの
// テーマ (plugins/<author>/<name>/themes/ のファイル)、季節パックの絵の形
// (plugins/rm_engine/theme_seasons/editor/Ornament.h)。

#include "Test.h"

#include "editor/widgets/Theme.h"
#include "editor/widgets/ThemeFile.h"
#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

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
        // ロゴのインク (目立つ方の白か黒) は、その種類のテーマが期待するもの。
        const rm::Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
        CHECK(theme::InkOn(t->info.palette->sidebar) == (t->info.palette->light ? black : white));
        ++count;
    }
    CHECK(count == 7);
    for (const char* id : {"Midnight", "Forest", "Paper", "Spring", "Summer", "Fall", "Winter", "Sakura"}) CHECK(ids.count(id) == 1);
}

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

    // 降る雪は領域の中に収まる。同じシードなら同じ雪片。
    const Bounds flakes = BoundsOf(Snowfall({0, 0, 400, 300}, 50, 6, 3));
    CHECK(flakes.figures == 50 && flakes.x0 > -4 && flakes.y0 > -4 && flakes.x1 < 404 && flakes.y1 < 304);
    // 雪は下の縁の内側に積もる: 縁から `depth` より上にはならず、角にはかからない。
    const rm::ui::Rect panel{100, 200, 300, 150};
    const Bounds drift = BoundsOf(SnowDrift(panel, 9, 8));
    CHECK(drift.y1 == 350 && drift.y0 >= 350 - 9.01f);
    CHECK(drift.x0 >= 100 + 4.f && drift.x1 <= 400 - 4.f);
    CHECK(AllClockwise(SnowDrift(panel, 9, 8)));
    CHECK(SnowDrift({0, 0, 5, 10}, 6, 8).steps.empty());  // 狭すぎて何も入らない
    // パネルがどこにあっても、どんなに広くなっても同じ吹きだまり (右端だけがついていく)。
    const auto heights = [](const rm::ui::Rect& r) {
        std::vector<float> out;
        for (const auto& st : SnowDrift(r, 9, 8).steps) out.push_back(st.a.y);
        return out;
    };
    const std::vector<float> narrow = heights({100, 200, 300, 150}), wide = heights({100, 200, 600, 150});
    const std::vector<float> moved = heights({250, 200, 300, 150});
    CHECK(narrow.size() > 40 && wide.size() > narrow.size());
    for (size_t i = 0; i + 10 < narrow.size(); ++i) CHECK(narrow[i] == wide[i]);  // 右端の傾きまでは
    CHECK(moved == narrow);

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
