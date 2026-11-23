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
