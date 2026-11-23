// 気候 (plugins/rm_engine/climate): エリアの深さと重み・重ね方・高さで冷える・シーンからエリアを集める・
// スクリプトの API。

#include "Test.h"

#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/climate/runtime/ScriptClimate.h"
#include "rm_engine/climate/runtime/sdk/RMClimateAbi.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/weather/runtime/Weather.h"
#include "rm_engine/weather_x_climate/runtime/Register.h"
#include "core/RuntimeRegistry.h"

#include <cmath>

using namespace rm;
using namespace rm::climate;
using game_production::Scene;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

template <class T>
T& Add(Scene& s, int id) {
    s.AddComponent(id, reflect::TypeOf<T>());
    return *static_cast<T*>(s.Find(id)->FindComponent(reflect::TypeOf<T>().name)->value.Get());
}

Scene ClimateScene() {
    Scene s;
    s.SetSettingTypes({&reflect::TypeOf<ClimateSettings>()});
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<space2d::Transform2D>(), &reflect::TypeOf<ClimateZone>(),
                         &reflect::TypeOf<ClimateZone2D>()});
    return s;
}

}  // namespace

RM_TEST(Climate_DepthAndWeight) {
    const ZoneBody box{ZoneBody::Kind::Box, {10, 5, 20}};
    CHECK(Near(ZoneDepth(box, {0, 0, 0}), 5.f));        // (一番近い縁: 上下)
    CHECK(Near(ZoneDepth(box, {8, 0, 0}), 2.f));
    CHECK(ZoneDepth(box, {11, 0, 0}) < 0);               // 外
    const ZoneBody cyl{ZoneBody::Kind::Cylinder, {10, 50, 0}};
    CHECK(Near(ZoneDepth(cyl, {6, 0, 8}), 0.f) && Near(ZoneDepth(cyl, {3, 0, 4}), 5.f));
    const ZoneBody rect{ZoneBody::Kind::Rect, {4, 2, 0}}, circle{ZoneBody::Kind::Circle, {5, 0, 0}};
    CHECK(Near(ZoneDepth(rect, {1, 0, 999}), 2.f) && Near(ZoneDepth(circle, {3, 4, 0}), 0.f));  // (2D は z を見ない)
    // 重み: 縁で 0、境目の幅で 1、間はなめらか。幅 0: 中なら 1。
    CHECK(ZoneWeight(-1, 10) == 0.f && ZoneWeight(0, 10) == 0.f && ZoneWeight(10, 10) == 1.f && ZoneWeight(50, 10) == 1.f);
    CHECK(Near(ZoneWeight(5, 10), 0.5f) && ZoneWeight(2, 10) < 0.2f && ZoneWeight(8, 10) > 0.8f);
    CHECK(ZoneWeight(0.01f, 0) == 1.f && ZoneWeight(-0.01f, 0) == 0.f && ZoneWeight(NAN, 10) == 0.f);
}

RM_TEST(Climate_BlendByPriority) {
    const ClimateHere outside{15, 50};
    CHECK(Near(Blend(outside, {}).temperature, 15.f));
    // 半分の重みで -5 ℃ のエリア: 間の 5 ℃。湿り気は変えない。
    const ZoneEffect cold{0.5f, 0, true, false, -5, 90};
    CHECK(Near(Blend(outside, {&cold, 1}).temperature, 5.f) && Near(Blend(outside, {&cold, 1}).humidity, 50.f));
    // 優先度の大きいほうが上: 並びの順に関係なく、山頂 (-20 ℃, 優先度 1) が地方 (-5 ℃) の上に重なる。
    const ZoneEffect region{1, 0, true, false, -5, 0}, peak{1, 1, true, true, -20, 80};
    const ZoneEffect a[] = {peak, region}, b[] = {region, peak};
    CHECK(Near(Blend(outside, a).temperature, -20.f) && Near(Blend(outside, b).temperature, -20.f) && Near(Blend(outside, a).humidity, 80.f));
    // 同じ優先度は並びの順 (後ろが上)。
    const ZoneEffect warm{1, 0, true, false, 30, 0};
    const ZoneEffect c[] = {region, warm};
    CHECK(Near(Blend(outside, c).temperature, 30.f));
    // 高さで冷える: 100 m で 0.6 ℃。基準の高さから。
    ClimateSettings s;
    s.baseHeight = 50;
    CHECK(Near(CooledByHeight(10, 150, s), 9.4f) && Near(CooledByHeight(10, 50, s), 10.f) && Near(CooledByHeight(10, -50, s), 10.6f));
    s.lapse = 0;
    CHECK(Near(CooledByHeight(10, 1000, s), 10.f));
}

RM_TEST(Climate_FieldFromScene) {
    Scene s = ClimateScene();
    ClimateSettings& set = *s.Setting<ClimateSettings>();
    set.lapse = 0;
    // 3D: (100, 0, 0) にある 40 m の箱。境目 10 m。
    const int zone = s.Create("寒い所");
    Add<space3d::Transform>(s, zone).position = {100, 0, 0};
    ClimateZone& z = Add<ClimateZone>(s, zone);
    z.size = {40, 40, 40};
    z.border = 10;
    z.temperature = -5;
    const ClimateField f(s);
    CHECK(f.Zones3D().size() == 1 && f.Zones2D().empty());
    CHECK(Near(f.At({0, 0, 0}).temperature, 15.f));     // 外
    CHECK(Near(f.At({100, 0, 0}).temperature, -5.f));   // 真ん中 (縁から 20 m)
    CHECK(Near(f.At({115, 0, 0}).temperature, 5.f));    // 縁から 5 m: 半分
    CHECK(Near(f.At2D({100, 0}).temperature, 15.f));    // (3D のエリアは 2D の場所に効かない)
    // 親の Transform と大きさ: 親を 2 倍にすると、エリアも 2 倍 (80 m) で、親の分だけ動く。
    const int parent = s.Create("地方");
    Add<space3d::Transform>(s, parent).scale = {2, 2, 2};
    const int big = s.Create("大きな所", parent);
    Add<space3d::Transform>(s, big).position = {0, 0, -100};
    Add<ClimateZone>(s, big).border = 0;
    const ClimateField g(s);
    CHECK(Near(g.At({0, 0, -200 + 39}).temperature, -5.f) && Near(g.At({0, 0, -200 + 41}).temperature, 15.f));
    // 切ったエリアは効かない。
    s.Find(zone)->data.active = false;
    CHECK(Near(ClimateField(s).At({100, 0, 0}).temperature, 15.f));
    // 3D は高さで冷える。
    set.lapse = 1;
    CHECK(Near(ClimateField(s).At({0, 200, 0}).temperature, 13.f));
    // 2D: 円のエリア。湿り気だけ変える。
    const int pond = s.Create("池");
    Add<space2d::Transform2D>(s, pond).position = {10, 0};
    ClimateZone2D& p = Add<ClimateZone2D>(s, pond);
    p.shape = ZoneShape2D::Circle;
    p.size = {8, 8};
    p.border = 0;
    p.setTemperature = false;
    p.setHumidity = true;
    p.humidity = 95;
    const ClimateField h(s);
    CHECK(Near(h.At2D({12, 0}).humidity, 95.f) && Near(h.At2D({12, 0}).temperature, 15.f) && Near(h.At2D({15, 0}).humidity, 50.f));
    CHECK(Near(h.At2D({12, 0}).temperature, h.At2D({12, 5000}).temperature));  // (2D は高さで冷えない)
}

RM_TEST(Climate_Script) {
    Scene s = ClimateScene();
    const int zone = s.Create("寒い所");
    Add<space3d::Transform>(s, zone);
    Add<ClimateZone>(s, zone).border = 0;
    game_production::ScriptWorld world;
    world.scene = &s;
    const auto* api = static_cast<const RmClimateApi*>(ScriptClimatePart().table);
    float t = 0, h = 0;
    const float inside[3] = {0, 0, 0}, outside[3] = {500, 0, 0};
    CHECK(api->at(&world, inside, 0, &t, &h) == 1 && Near(t, -5.f) && Near(h, 50.f));
    CHECK(api->at(&world, outside, 0, &t, &h) == 1 && Near(t, 15.f));
    CHECK(api->set(&world, 0, -30) == 1 && api->get(&world, 0, &t) == 1 && Near(t, -30.f));
    CHECK(api->set(&world, 1, 400) == 1 && api->get(&world, 1, &h) == 1 && Near(h, 100.f));  // (0 .. 100 に)
    CHECK(api->set(&world, 9, 1) == 0 && api->set(&world, 0, NAN) == 0);
    CHECK(api->at(nullptr, inside, 0, &t, &h) == 0);
}

// 気候の地図: ランレングス・塗る・消す・縮める・マスの大きさを変える・種類を消す。
RM_TEST(Climate_PaintGrid) {
    const std::vector<int> ids{0, 0, 0, 1, 1, 2, 0};
    CHECK(EncodeRuns(ids) == "0*3,1*2,2,0");
    CHECK(DecodeRuns("0*3,1*2,2,0", 7) == ids);
    CHECK(DecodeRuns("1*2", 4) == (std::vector<int>{1, 1, 0, 0}));       // (足りない分は 0)
    CHECK(DecodeRuns("3*9", 2) == (std::vector<int>{3, 3}));             // (多い分は捨てる)
    CHECK(DecodeRuns("x,-1,2*y,4", 4) == (std::vector<int>{0, 0, 4, 0}));  // (読めない: 0。数の読めない続きは 0 個)
    CHECK(EncodeRuns({}).empty() && DecodeRuns("", 2) == (std::vector<int>{0, 0}));
    // 塗る: 4 m のマス。真ん中 (10, 10)、半径 4 m: 真ん中が円に入るマス。
    PaintGrid g;
    CHECK(PaintCircle(g, 4, {10, 10}, 4, 2));
    CHECK(g.At(2, 2) == 2 && g.At(1, 2) == 2 && g.At(3, 2) == 2 && g.At(2, 1) == 2 && g.At(2, 3) == 2);
    CHECK(g.At(1, 1) == 0 && g.At(0, 2) == 0 && g.At(4, 2) == 0 && g.At(99, 99) == 0);  // (マス (1, 1) の真ん中 (6, 6) は 5.7 m 離れている)
    CHECK(!PaintCircle(g, 4, {10, 10}, 4, 2));  // (同じ: 変わらない)
    // 小さい円でも、真ん中のマスは塗る。遠くに塗ると長方形が広がる (前に塗った所はそのまま)。
    CHECK(PaintCircle(g, 4, {-30, 50}, 0.1f, 1) && g.At(-8, 12) == 1 && g.At(2, 2) == 2);
    // 地図に書く: 塗った所だけを囲む長方形。読み直すと同じ。
    ClimateMap map;
    map.cellSize = 4;
    SetPaint(map, g);
    CHECK(map.cellX == -8 && map.cellZ == 1 && map.width == 12 && map.depth == 12);
    const PaintGrid back = PaintOf(map);
    CHECK(back.At(-8, 12) == 1 && back.At(2, 2) == 2 && back.At(0, 5) == 0);
    // 消す: 消した所が端なら、長方形が縮む。全部消すと幅 0。
    PaintGrid e = back;
    CHECK(PaintCircle(e, 4, {-30, 50}, 1, 0));
    SetPaint(map, e);
    CHECK(map.cellX == 1 && map.cellZ == 1 && map.width == 3 && map.depth == 3);
    CHECK(!PaintCircle(e, 4, {500, 500}, 10, 0));  // (塗っていない所を消す: 変わらない)
    PaintGrid none;
    SetPaint(map, none);
    CHECK(map.width == 0 && map.depth == 0 && map.cells.empty() && PaintOf(map).Empty());
    // マスの大きさを変える: 同じ所が塗られたまま (4 m -> 2 m: 1 マスが 4 マス)。
    PaintGrid one;
    PaintCircle(one, 4, {6, 6}, 0, 3);  // マス (1, 1) = 4..8 m
    const PaintGrid fine = Resample(one, 4, 2);
    CHECK(fine.At(2, 2) == 3 && fine.At(3, 3) == 3 && fine.At(1, 2) == 0 && fine.At(4, 3) == 0);
    const PaintGrid coarse = Resample(fine, 2, 8);  // (8 m のマス (0, 0) の真ん中 = (4, 4) は塗ってある)
    CHECK(coarse.At(0, 0) == 3);
    // 種類: 新しい番号・消すとそのマスも消える。
    ClimateMap m2;
    CHECK(m2.kinds.size() == 4 && NewKindId(m2) == 5 && FindKind(m2, 2) && FindKind(m2, 2)->temperature < 0);
    SetPaint(m2, back);
    RemoveKind(m2, 2);
    CHECK(!FindKind(m2, 2) && m2.kinds.size() == 3 && PaintOf(m2).At(2, 2) == 0 && PaintOf(m2).At(-8, 12) == 1);
    // 保存と読み込み: 知らないキーは書き戻す。
    json::Value v = reflect::Save(m2);
    v.Set("future", json::Value(7));
    ClimateMap m3;
    reflect::Unknowns u;
    reflect::Load(m3, v, &u);
    CHECK(m3.cells == m2.cells && m3.kinds.size() == 3 && reflect::Save(m3, &u).Get("future").AsInt() == 7);
}

// 気候の地図の気候: 塗った割合で混ぜる (境目の幅の正方形の中の面積: 積分画像)。エリアはその上。
RM_TEST(Climate_PaintBlend) {
    Scene s = ClimateScene();
    s.SetSettingTypes({&reflect::TypeOf<ClimateSettings>(), &reflect::TypeOf<ClimateMap>()});
    ClimateSettings& set = *s.Setting<ClimateSettings>();
    set.lapse = 0;
    ClimateMap& map = *s.Setting<ClimateMap>();
    map.kinds = {{7, "寒い", {}, -10.f, 80.f}};
    map.cellSize = 2;
    map.border = 8;
    // x = 0 .. 100、z = -50 .. 50 を塗る (左の縁は x = 0)。
    PaintGrid g;
    for (float x = 1; x < 100; x += 2) {
        for (float z = -49; z < 50; z += 2) PaintCircle(g, 2, {x, z}, 0, 7);
    }
    SetPaint(map, g);
    const ClimateField f(s);
    CHECK(Near(f.At({50, 0, 0}).temperature, -10.f) && Near(f.At({50, 0, 0}).humidity, 80.f));  // 真ん中
    CHECK(Near(f.At({-50, 0, 0}).temperature, 15.f) && Near(f.At({-50, 0, 0}).humidity, 50.f));  // 遠く
    CHECK(Near(f.At({0, 0, 0}).temperature, 2.5f));                                              // 縁: 半分
    CHECK(Near(f.At({-4, 0, 0}).temperature, 15.f) && Near(f.At({4, 0, 0}).temperature, -10.f));  // 境目の幅 8 m の外
    CHECK(Near(f.At({2, 0, 0}).temperature, -3.75f));                                             // 縁から 2 m: 3/4 (まっすぐ変わる)
    CHECK(f.At({1, 0, 0}).temperature < f.At({0.5f, 0, 0}).temperature);                           // (マスの中でもなめらか)
    CHECK(Near(f.At({0, 0, 50}).temperature, 15.f + (-10.f - 15.f) * 0.25f));                     // 角: 1/4
    CHECK(Near(f.At2D({50, 0}).temperature, 15.f));                                               // (地図は 3D だけ)
    // 境目 0: マスの縁で切り替わる。
    map.border = 0;
    CHECK(Near(ClimateField(s).At({0.1f, 0, 0}).temperature, -10.f) && Near(ClimateField(s).At({-0.1f, 0, 0}).temperature, 15.f));
    map.border = 8;
    // エリアは地図の上に重なる。湿り気を変えないエリアは、地図の湿り気のまま。
    const int zone = s.Create("暑い所");
    Add<space3d::Transform>(s, zone).position = {50, 0, 0};
    ClimateZone& z = Add<ClimateZone>(s, zone);
    z.size = {10, 10, 10};
    z.border = 0;
    z.temperature = 40;
    const ClimateField h(s);
    CHECK(Near(h.At({50, 0, 0}).temperature, 40.f) && Near(h.At({50, 0, 0}).humidity, 80.f) && Near(h.At({70, 0, 0}).temperature, -10.f));
    // 高さで冷える は地図の気温から。
    set.lapse = 1;
    CHECK(Near(ClimateField(s).At({70, 300, 0}).temperature, -13.f));
    // 表は同じ中身の地図で使い回す。中身が変わると作り直す。
    const auto t1 = PaintTablesOf(map), t2 = PaintTablesOf(map);
    CHECK(t1 && t1 == t2);
    map.kinds[0].temperature = -20;
    CHECK(PaintTablesOf(map) != t1 && Near(ClimateField(s).At({70, 0, 0}).temperature, -20.f));
    // 塗っていない地図 / 地図にない番号: 表なし。
    ClimateMap empty;
    CHECK(!PaintTablesOf(empty));
    map.kinds[0].id = 8;
    CHECK(!PaintTablesOf(map) && Near(ClimateField(s).At({70, 0, 0}).temperature, 15.f));
}

// 天候 × 気候 と気候の地図: 寒い気候を塗った所だけ雪になり、湿り気で降る量が変わる (天候の口はそのまま)。
RM_TEST(Climate_PaintWeather) {
    using namespace rm::weather_x_climate;
    Scene sc = ClimateScene();
    sc.SetSettingTypes({&reflect::TypeOf<ClimateSettings>(), &reflect::TypeOf<ClimateMap>(), &reflect::TypeOf<weather::Weather>()});
    ClimateMap& map = *sc.Setting<ClimateMap>();
    map.cellSize = 2;
    map.border = 0;
    PaintGrid g;
    PaintCircle(g, map.cellSize, {100, 0}, 15, 2);  // 寒冷 (-8 ℃・60 %) を (100, 0) のまわり 15 m に
    SetPaint(map, g);
    RuntimeRegistry reg;
    RegisterRuntime(reg);
    const auto places = reg.Contributions<weather::WeatherPlaceInfo>(weather::kWeatherPlacePoint);
    CHECK(places.size() == 1);
    weather::WeatherPlace grid[9];
    places[0].item->grid(sc, {100, 0, 0}, 30.f, 3, grid);  // (x: 70, 100, 130)
    CHECK(grid[4].snow == 1.f && grid[3].snow == 0.f && grid[5].snow == 0.f);
    CHECK(Near(grid[4].more, MoreOf(60)) && Near(grid[3].more, 1.f));
    // 種類の気温を上げると雨に。
    map.kinds[1].temperature = 10;
    places[0].item->grid(sc, {100, 0, 0}, 30.f, 3, grid);
    CHECK(grid[4].snow == 0.f);
}

// 天候 × 気候: 気温 -> 雪の割合、湿り気 -> 降る量、カメラのいる所へ数秒かけて、天候の今の天候。
RM_TEST(Climate_WeatherPlace) {
    using namespace rm::weather_x_climate;
    CHECK(SnowShareOf(5) == 0.f && SnowShareOf(2) == 0.f && SnowShareOf(-1) == 1.f && SnowShareOf(-20) == 1.f);
    CHECK(Near(SnowShareOf(0.5f), 0.5f) && SnowShareOf(1) < 0.5f && SnowShareOf(0) > 0.5f && SnowShareOf(NAN) == 0.f);  // (みぞれ)
    CHECK(MoreOf(0) == 0.f && Near(MoreOf(25), 0.5f) && Near(MoreOf(50), 1.f) && Near(MoreOf(100), weather::kPlaceMostMore) && Near(MoreOf(75), 1.25f));
    // 近づける: 初めはすぐ。そのあとは 2 秒で 63 %。
    float s = -1, m = 1;
    Approach(s, m, {1.f, 0.5f}, 0.016f);
    CHECK(s == 1.f && m == 0.5f);
    Approach(s, m, {0.f, 1.5f}, 2.f);
    CHECK(Near(s, 0.3679f) && Near(m, 1.5f - 0.3679f));
    // 天候の今の天候: 雪の割合で分ける (雪にする は使わない)。降る量は倍率を掛けて 1 まで。
    weather::Weather w;
    w.precipitation = 60;
    w.snowing = false;
    w.placeSnow = 0.25f;
    w.placeMore = 1.f;
    CHECK(Near(weather::NowOf(w).snow, 0.15f) && Near(weather::NowOf(w).rain, 0.45f));
    w.placeMore = 2.f;
    CHECK(Near(weather::NowOf(w).snow + weather::NowOf(w).rain, 1.f));
    w.placeSnow = -1;  // (答える者がいない: 雪にする で)
    CHECK(weather::NowOf(w).snow == 0.f && Near(weather::NowOf(w).rain, 0.6f));
    // 口とシステム: 寒いエリアの真ん中にカメラ。
    Scene sc = ClimateScene();
    sc.SetSettingTypes({&reflect::TypeOf<ClimateSettings>(), &reflect::TypeOf<weather::Weather>()});
    const int cold = sc.Create("雪の山");
    Add<space3d::Transform>(sc, cold).position = {100, 0, 0};
    ClimateZone& z = Add<ClimateZone>(sc, cold);
    z.border = 0;
    z.temperature = -10;
    RuntimeRegistry reg;
    RegisterRuntime(reg);
    const auto places = reg.Contributions<weather::WeatherPlaceInfo>(weather::kWeatherPlacePoint);
    CHECK(places.size() == 1);
    weather::WeatherPlace grid[9];
    places[0].item->grid(sc, {100, 0, 0}, 30.f, 3, grid);  // (x: 70, 100, 130 - 箱は 80 .. 120)
    CHECK(grid[4].snow == 1.f && grid[3].snow == 0.f && grid[5].snow == 0.f && Near(grid[4].more, 1.f));
    const int cam = sc.Create("カメラ");
    Add<space3d::Transform>(sc, cam).position = {100, 0, 0};
    game_production::SystemContext ctx{sc, 0.016f};
    ctx.camera = static_cast<uint64_t>(cam);
    for (const auto& sys : reg.Contributions<game_production::SystemInfo>(game_production::kSystemPoint)) sys.item->run(ctx);
    CHECK(sc.Setting<weather::Weather>()->placeSnow == 1.f);
    // 場所ごとの濡れと積雪 (段階 2): 同じシステムが、カメラのまわりの地図のマスごとの降り方も書く。
    // 天候のシステムがそれで進めると、雪の所だけ積もり、雨の所だけよく濡れる。
    weather::Weather& ww = *sc.Setting<weather::Weather>();
    CHECK(ww.placeCells.size() == size_t{weather::kWetMapCells} * weather::kWetMapCells);
    ww.precipitation = 60;
    ww.windStrength = 0;
    game_production::SystemContext minute{sc, 60.f};
    minute.camera = static_cast<uint64_t>(cam);
    weather::RunWeather(minute);
    const weather::WetMap* map = weather::WetMapNow(ww);
    CHECK(map != nullptr);
    if (map) {
        CHECK(weather::WetCellAt(*map, 100, 0)->at(0).snow > 0.01f && weather::WetCellAt(*map, 60, 0)->at(0).snow == 0.f);
        CHECK(weather::WetCellAt(*map, 60, 0)->at(0).sun > weather::WetCellAt(*map, 100, 0)->at(0).sun);
        CHECK(weather::WetCellAt(*map, 119, 0)->at(0).snow > 0.01f && weather::WetCellAt(*map, 125, 0)->at(0).snow == 0.f);  // (縁が 120 m: 境目 0)
    }
}
