// ナビゲーション (plugins/rm_engine/navigation と、そのつなぎ): 焼く (平らな床・急な坂・低い天井・半径・橋の上と下・
// 同じ入力で同じバイト)・道 (壁を回る・エリアの費用で遠回り・通れないエリア・外の点)・.rmnav の往復と知らないチャンク・
// シーンから焼く物を集める (メッシュ・コライダー・ナビの形・動く物)・指紋・作業スレッドの焼く仕事。設計 docs/NAVIGATION.md 10 章。

#include "Test.h"

#include "base/Utf.h"
#include "core/RuntimeRegistry.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_navigation/editor/NavBake.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/navigation/runtime/Bake.h"
#include "rm_engine/navigation/runtime/NavFile.h"
#include "rm_engine/navigation/runtime/Query.h"
#include "rm_engine/navigation_x_3d/runtime/Register.h"
#include "rm_engine/navigation_x_physics/runtime/Register.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <cmath>
#include <filesystem>

using namespace rm;
using namespace rm::navigation;
using game_production::Scene;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// 形 (3D 空間の ShapeMesh: 表は外から見て反時計回り = エンジンの決まり) を大きさ・回転・位置で置いて足す。
void AddShape(NavInput& in, space3d::Shape shape, Vec3 scale, Vec3 pos, Vec3 rotation = {}, uint8_t area = kAreaNormal) {
    const space3d::MeshData m = space3d::ShapeMesh(shape);
    space3d::Transform t;
    t.position = pos, t.rotation = rotation, t.scale = scale;
    const space3d::Mat4 world = space3d::LocalMatrix(t);
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        in.AddTriangle(space3d::TransformPoint(world, m.vertices[m.indices[i]].position), space3d::TransformPoint(world, m.vertices[m.indices[i + 1]].position),
                       space3d::TransformPoint(world, m.vertices[m.indices[i + 2]].position), area);
    }
}

// 平らな床 (幅 `w` x 奥行き `d`、y = 0。平面の形は 10 x 10)。
void Floor(NavInput& in, float w, float d, float y = 0) { AddShape(in, space3d::Shape::Plane, {w / 10, 1, d / 10}, {0, y, 0}); }

BakeOptions Options(Vec3 lo, Vec3 hi, float cell = 0.2f) {
    BakeOptions o;
    o.bounds = {lo, hi};
    o.agent.cellSize = cell;
    o.agent.cellHeight = cell * 0.5f;
    return o;
}

NavData BakeOk(const NavInput& in, const BakeOptions& o) {
    NavData d;
    std::string err;
    CHECK(Bake(in, o, {}, nullptr, d, &err));
    CHECK(err.empty());
    return d;
}

}  // namespace

RM_TEST(NavBakeFlatFloorIsOneWalkableSurface) {
    NavInput in;
    Floor(in, 10, 10);
    const NavData d = BakeOk(in, Options({-6, -1, -6}, {6, 3, 6}));
    CHECK(!d.Empty());
    NavMesh mesh;
    CHECK(mesh.Load(d, nullptr));
    CHECK(!mesh.Empty());
    // 床の真ん中に点がある (y は床の高さ)。表の向き (エンジンの決まり) が Recast と違うと、ここで床にならない。
    const auto p = mesh.ClosestPoint({0, 0.5f, 0});
    CHECK(p.has_value());
    if (p) CHECK(Near(p->y, 0, 0.15f));
    // 角から角へはまっすぐ (障害物が無い)。
    const NavPath path = mesh.FindPath({-4, 0, -4}, {4, 0, 4});
    CHECK(path.status == NavPath::Status::Found);
    CHECK(Near(path.length, std::sqrt(2.f) * 8, 0.3f));
}

RM_TEST(NavBakeSteepSlopeIsNotWalkable) {
    NavInput in;
    AddShape(in, space3d::Shape::Plane, {1, 1, 1}, {0, 0, 0}, {0, 0, 60});  // 60 度 (登れる坂は 45 度)
    const NavData d = BakeOk(in, Options({-6, -6, -6}, {6, 6, 6}));
    CHECK(d.Empty());
    NavInput gentle;
    AddShape(gentle, space3d::Shape::Plane, {1, 1, 1}, {0, 0, 0}, {0, 0, 20});  // 20 度は歩ける
    CHECK(!BakeOk(gentle, Options({-6, -6, -6}, {6, 6, 6})).Empty());
}

RM_TEST(NavBakeLowCeilingIsNotWalkedUnder) {
    NavInput in;
    Floor(in, 10, 10);
    AddShape(in, space3d::Shape::Box, {12, 0.2f, 12}, {0, 1.1f, 0});  // 天井の下は 1 m (体の高さは 1.8 m)
    NavMesh mesh;
    CHECK(mesh.Load(BakeOk(in, Options({-7, -1, -7}, {7, 4, 7})), nullptr));
    CHECK(!mesh.ClosestPoint({0, 0, 0}, {0.5f, 0.3f, 0.5f}).has_value());  // 床の高さには無い
    const auto top = mesh.ClosestPoint({0, 1.3f, 0}, {0.5f, 0.3f, 0.5f});    // 天井の上は歩ける
    CHECK(top.has_value());
    if (top) CHECK(Near(top->y, 1.2f, 0.15f));
}

RM_TEST(NavBakeKeepsTheRadiusFromTheEdge) {
    NavInput in;
    Floor(in, 10, 10);
    NavMesh mesh;
    CHECK(mesh.Load(BakeOk(in, Options({-6, -1, -6}, {6, 3, 6})), nullptr));
    const auto p = mesh.ClosestPoint({4.99f, 0, 0}, {1, 1, 1});
    CHECK(p.has_value());
    if (p) {
        CHECK(p->x < 5 - 0.3f + 0.21f);  // 縁から半径 (0.3 m) だけ離れる (箱 1 つ分の誤差)
        CHECK(p->x > 5 - 0.3f - 0.41f);
    }
}

RM_TEST(NavBakeBridgeAboveAndBelowAreSeparate) {
    NavInput in;
    Floor(in, 10, 10);
    AddShape(in, space3d::Shape::Box, {10, 0.2f, 2}, {0, 3, 0});  // 床の上 3 m の橋
    NavMesh mesh;
    CHECK(mesh.Load(BakeOk(in, Options({-6, -1, -6}, {6, 5, 6})), nullptr));
    const auto low = mesh.ClosestPoint({0, 0, 0}, {0.5f, 0.5f, 0.5f});
    const auto high = mesh.ClosestPoint({0, 3.2f, 0}, {0.5f, 0.5f, 0.5f});
    CHECK(low.has_value() && high.has_value());
    if (low && high) {
        CHECK(Near(low->y, 0, 0.15f));
        CHECK(Near(high->y, 3.1f, 0.15f));
    }
}

RM_TEST(NavBakeSameInputSameBytes) {
    NavInput in;
    Floor(in, 20, 20);
    AddShape(in, space3d::Shape::Box, {1, 2, 8}, {0, 1, 0});
    BakeOptions one = Options({-11, -1, -11}, {11, 4, 11});
    one.threads = 1;
    BakeOptions many = one;
    many.threads = 4;
    const NavData a = BakeOk(in, one), b = BakeOk(in, many);
    CHECK(a.tiles.size() > 1);
    NavFileHead h;
    h.engine = "0.0.0";
    CHECK(WriteNavFile(a, h) == WriteNavFile(b, h));
    CHECK(WriteNavFile(a, h) == WriteNavFile(BakeOk(in, one), h));
}

RM_TEST(NavPathGoesAroundAWall) {
    NavInput in;
    Floor(in, 20, 20);
    AddShape(in, space3d::Shape::Box, {1, 2, 14}, {0, 1, 0});  // 真ん中の壁 (z -7 .. 7)
    NavMesh mesh;
    CHECK(mesh.Load(BakeOk(in, Options({-11, -1, -11}, {11, 4, 11})), nullptr));
    const NavPath path = mesh.FindPath({-5, 0, 0}, {5, 0, 0});
    CHECK(path.status == NavPath::Status::Found);
    CHECK(path.points.size() >= 4);  // 壁の端で曲がる
    CHECK(path.length > 2 * std::sqrt(25.f + 49.f) - 0.5f);
    // まっすぐは壁に当たる。
    CHECK(!mesh.Raycast({-5, 0, 0}, {5, 0, 0}));
    CHECK(mesh.Raycast({-5, 0, 8.5f}, {5, 0, 8.5f}));
    // 外の点。
    CHECK(mesh.FindPath({-50, 0, 0}, {5, 0, 0}).status == NavPath::Status::StartOff);
    CHECK(mesh.FindPath({-5, 0, 0}, {50, 0, 0}).status == NavPath::Status::EndOff);
    NavMesh empty;
    CHECK(empty.FindPath({0, 0, 0}, {1, 0, 0}).status == NavPath::Status::NoMesh);
}

RM_TEST(NavPathAvoidsCostlyAndBlockedAreas) {
    NavInput in;
    Floor(in, 20, 10);
    NavAreaVolume swamp;
    swamp.outline = {{-2, -3}, {2, -3}, {2, 3}, {-2, 3}};  // 真ん中の沼 (床の奥行きは -5 .. 5)
    swamp.minY = -1, swamp.maxY = 1;
    swamp.area = 1;
    in.volumes.push_back(swamp);
    NavMesh mesh;
    CHECK(mesh.Load(BakeOk(in, Options({-11, -1, -6}, {11, 3, 6})), nullptr));
    NavFilter cheap;
    const NavPath straight = mesh.FindPath({-8, 0, 0}, {8, 0, 0}, cheap);
    CHECK(straight.status == NavPath::Status::Found);
    CHECK(straight.length < 16.5f);
    NavFilter costly;
    costly.costs[1] = 20;
    const NavPath around = mesh.FindPath({-8, 0, 0}, {8, 0, 0}, costly);
    CHECK(around.status == NavPath::Status::Found);
    CHECK(around.length > 17.f);  // 沼をよける
    // 沼の多角形のエリアの番号は 1。
    std::vector<NavPolygon> polys;
    mesh.PolygonsOf(polys);
    bool sawSwamp = false;
    for (const NavPolygon& p : polys) sawSwamp = sawSwamp || p.area == 1;
    CHECK(sawSwamp);

    // 通れないエリア: 奥行きいっぱいの沼は渡れない (一番近い所まで)。
    NavInput wide;
    Floor(wide, 20, 10);
    swamp.outline = {{-2, -6}, {2, -6}, {2, 6}, {-2, 6}};
    wide.volumes.push_back(swamp);
    NavMesh m2;
    CHECK(m2.Load(BakeOk(wide, Options({-11, -1, -6}, {11, 3, 6})), nullptr));
    std::bitset<64> blocked;
    blocked.set(1);
    m2.SetBlockedAreas(blocked);
    CHECK(m2.FindPath({-8, 0, 0}, {8, 0, 0}).status != NavPath::Status::Found);
    m2.SetBlockedAreas({});
    CHECK(m2.FindPath({-8, 0, 0}, {8, 0, 0}).status == NavPath::Status::Found);
}

RM_TEST(NavFileRoundTripAndUnknowns) {
    NavInput in;
    Floor(in, 10, 10);
    const NavData d = BakeOk(in, Options({-6, -1, -6}, {6, 3, 6}));
    NavFileHead h;
    h.engine = "0.0.35";
    h.agentType = 3;
    h.agentName = "大きい敵";
    h.fingerprint = FingerprintText(0x1234abcdULL);
    h.extra.Set("future", json::Value("keep me"));
    std::string bytes = WriteNavFile(d, h);
    NavFileHead back;
    std::string err;
    auto r = ReadNavFile(bytes, &back, &err);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->tiles == d.tiles);
    CHECK(r->agent == d.agent);
    CHECK(r->tilesX == d.tilesX && r->tilesZ == d.tilesZ && r->maxPolysPerTile == d.maxPolysPerTile);
    CHECK(back.agentType == 3 && back.agentName == "大きい敵" && back.engine == "0.0.35");
    CHECK(back.fingerprint == "000000001234abcd");
    CHECK(back.extra.Get("future").IsString() && back.extra.Get("future").AsString() == "keep me");
    CHECK(WriteNavFile(*r, back) == bytes);  // 知らないキーを書き戻す
    // 知らないチャンクは飛ばす。
    std::string more = bytes;
    more.append("ZZZZ", 4);
    const uint64_t n = 3;
    more.append(reinterpret_cast<const char*>(&n), 8);
    more.append("abc");
    auto r2 = ReadNavFile(more, nullptr, &err);
    CHECK(r2.has_value() && r2->tiles.size() == d.tiles.size());
    // 新しい形式・壊れた物は読まない。
    std::string newer = bytes;
    newer[8] = 9;
    CHECK(!ReadNavFile(newer, nullptr, &err).has_value());
    CHECK(!ReadNavFile(bytes.substr(0, bytes.size() - 5), nullptr, &err).has_value());
    CHECK(!ReadNavFile("hello", nullptr, &err).has_value());
}

// --- シーンから (ゲーム制作 × ナビゲーションとつなぎ) -------------------------------------------------
namespace {

using namespace rm::game_x_navigation;

template <class T>
T& AddTo(Scene& s, int id) {
    s.AddComponent(id, reflect::TypeOf<T>());
    return *static_cast<T*>(s.Find(id)->FindComponent(reflect::TypeOf<T>().name)->value.Get());
}

Scene NavScene() {
    Scene s;
    s.SetSettingTypes({&reflect::TypeOf<NavSettings>()});
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<game_x_3d::MeshRenderer>(), &reflect::TypeOf<NavMeshVolume>(),
                         &reflect::TypeOf<NavGeometry>(), &reflect::TypeOf<NavArea>(), &reflect::TypeOf<physics::Rigidbody>(),
                         &reflect::TypeOf<physics::Collider>()});
    return s;
}

int PutShape(Scene& s, const char* name, game_x_3d::MeshShape shape, Vec3 pos, Vec3 scale) {
    const int id = s.Create(name);
    space3d::Transform& t = AddTo<space3d::Transform>(s, id);
    t.position = pos, t.scale = scale;
    AddTo<game_x_3d::MeshRenderer>(s, id).shape = shape;
    return id;
}

std::vector<const NavSourceInfo*> SourcesOf(const RuntimeRegistry& reg) {
    std::vector<const NavSourceInfo*> out;
    for (const auto& c : reg.Contributions<NavSourceInfo>(kNavSourcePoint)) out.push_back(c.item);
    return out;
}

}  // namespace

RM_TEST(NavCollectFromSceneFollowsTheRules) {
    RuntimeRegistry reg;
    navigation_x_3d::RegisterRuntime(reg);
    navigation_x_physics::RegisterRuntime(reg);
    const auto sources = SourcesOf(reg);
    CHECK(sources.size() == 2);
    CHECK(reg.Contributions<NavMoverInfo>(kNavMoverPoint).size() == 1);

    Scene s = NavScene();
    const int floor = PutShape(s, "床", game_x_3d::MeshShape::Plane, {0, 0, 0}, {2, 1, 2});  // 20 x 20
    const int wall = PutShape(s, "壁", game_x_3d::MeshShape::Box, {0, 1, 0}, {1, 2, 14});
    const int deco = PutShape(s, "飾り", game_x_3d::MeshShape::Box, {5, 0.5f, 5}, {1, 1, 1});
    AddTo<NavGeometry>(s, deco).mode = NavGeometryMode::Exclude;
    const int crate = PutShape(s, "箱 (動く)", game_x_3d::MeshShape::Box, {-5, 0.5f, 5}, {1, 1, 1});
    AddTo<physics::Rigidbody>(s, crate).motion = physics::Motion::Dynamic;
    const int far = PutShape(s, "遠い", game_x_3d::MeshShape::Box, {500, 0, 0}, {1, 1, 1});
    const int vol = s.Create("1階");
    AddTo<space3d::Transform>(s, vol).position = {0, 1, 0};
    AddTo<NavMeshVolume>(s, vol).size = {22, 6, 22};
    (void)floor, (void)wall, (void)far;

    const Worlds worlds = WorldsOf(s);
    const auto v = FindVolume(s, worlds, vol);
    CHECK(v.has_value());
    if (!v) return;
    CHECK(Near(v->bounds.min.y, -2) && Near(v->bounds.max.x, 11));
    NavInput in;
    CHECK(CollectInput(s, worlds, *v, sources, nullptr, {}, in));
    // 床 (2 三角形) + 壁の箱 (12)。飾り (入れない)・動く箱・遠い箱は入らない。
    CHECK(in.Triangles() == 14);

    // 焼いて道: 壁を回る。
    navigation::BakeOptions o;
    o.bounds = v->bounds;
    o.agent = AgentOf(*FindAgentType(*s.Setting<NavSettings>(), v->volume.agentType));
    NavData d;
    CHECK(navigation::Bake(in, o, {}, nullptr, d, nullptr));
    NavMesh mesh;
    CHECK(mesh.Load(d, nullptr));
    CHECK(mesh.FindPath({-5, 0, 0}, {5, 0, 0}).points.size() >= 4);

    // 「歩けない」の物は壁 (エリア 0)、「エリアを決める」はその番号。
    NavGeometry& g = AddTo<NavGeometry>(s, wall);
    g.mode = NavGeometryMode::Area;
    g.area = "2";
    CHECK(CollectInput(s, WorldsOf(s), *v, sources, nullptr, {}, in));
    int areaTwo = 0;
    for (const uint8_t a : in.areas) areaTwo += a == 2 ? 1 : 0;
    CHECK(areaTwo == 12);
    // ボリュームの欄で「メッシュを焼く」を切ると、何も入らない (コライダーが無いため)。
    NavVolume off = *v;
    off.volume.meshes = false;
    CHECK(CollectInput(s, WorldsOf(s), off, sources, nullptr, {}, in));
    CHECK(in.Triangles() == 0);
}

RM_TEST(NavCollectStaticColliders) {
    RuntimeRegistry reg;
    navigation_x_physics::RegisterRuntime(reg);
    const auto sources = SourcesOf(reg);
    Scene s = NavScene();
    const int box = s.Create("床 (コライダー)");
    AddTo<space3d::Transform>(s, box).position = {0, -0.5f, 0};
    AddTo<physics::Collider>(s, box).size = {20, 1, 20};
    const int ball = s.Create("球 (動く)");
    AddTo<space3d::Transform>(s, ball).position = {0, 2, 0};
    AddTo<physics::Collider>(s, ball).shape = physics::Shape::Sphere;
    AddTo<physics::Rigidbody>(s, ball);
    const int trig = s.Create("トリガー");
    AddTo<physics::Collider>(s, trig).trigger = true;
    const int vol = s.Create("範囲");
    AddTo<space3d::Transform>(s, vol);
    AddTo<NavMeshVolume>(s, vol).size = {20, 6, 20};
    const Worlds worlds = WorldsOf(s);
    NavInput in;
    CHECK(CollectInput(s, worlds, *FindVolume(s, worlds, vol), sources, nullptr, {}, in));
    CHECK(in.Triangles() == 12);  // 箱だけ
    CHECK(navigation_x_physics::Moves(*s.Find(ball)));
    CHECK(!navigation_x_physics::Moves(*s.Find(box)));
}

RM_TEST(NavFingerprintAndIds) {
    RuntimeRegistry reg;
    navigation_x_3d::RegisterRuntime(reg);
    const auto sources = SourcesOf(reg);
    Scene s = NavScene();
    const int floor = PutShape(s, "床", game_x_3d::MeshShape::Plane, {0, 0, 0}, {1, 1, 1});
    const int vol = s.Create("範囲");
    AddTo<space3d::Transform>(s, vol);
    AddTo<NavMeshVolume>(s, vol);
    const NavSettings& settings = *s.Setting<NavSettings>();
    const NavAgentType& type = *FindAgentType(settings, "1");
    auto print = [&] {
        const Worlds w = WorldsOf(s);
        return Fingerprint(s, w, *FindVolume(s, w, vol), type, sources, nullptr);
    };
    const uint64_t a = print();
    CHECK(print() == a);
    static_cast<space3d::Transform*>(s.Find(floor)->FindComponent(reflect::TypeOf<space3d::Transform>().name)->value.Get())->position.x = 1;
    CHECK(print() != a);
    NavAgentType bigger = type;
    bigger.radius = 0.6f;
    const Worlds w = WorldsOf(s);
    CHECK(Fingerprint(s, w, *FindVolume(s, w, vol), bigger, sources, nullptr) != print());

    CHECK(AreaCodeOf("0") == kAreaNormal && AreaCodeOf("5") == 5 && AreaCodeOf("x") == kAreaNormal && AreaCodeOf("99") == kAreaNormal);
    CHECK(AreaIdOfCode(kAreaNormal) == 0 && AreaIdOfCode(5) == 5);
    CHECK(FindAgentType(settings, "42") == &settings.agentTypes.front());  // 無い型: 最初の型
    CHECK(NewAreaKindId(settings) == 3);
    CHECK(FilterOf(settings).costs[1] == 3.f);  // 既定の「沼」
    CHECK(BlockedOf(settings).test(2));          // 既定の「立入禁止」
}

RM_TEST(NavSettingsKeepUnknownKeys) {
    NavSettings s;
    json::Value v = reflect::Save(s);
    v.Set("later", json::Value(7));
    NavSettings back;
    reflect::Unknowns unknowns;
    reflect::Load(back, v, &unknowns);
    CHECK(back.areas.size() == 3 && back.agentTypes.size() == 1);
    CHECK(reflect::Save(back, &unknowns).Get("later").AsInt() == 7);
}

RM_TEST(NavBakeJobWritesTheFile) {
    RuntimeRegistry reg;
    navigation_x_3d::RegisterRuntime(reg);
    Scene s = NavScene();
    PutShape(s, "床", game_x_3d::MeshShape::Plane, {0, 0, 0}, {1, 1, 1});
    const int vol = s.Create("範囲 <1>");
    AddTo<space3d::Transform>(s, vol);
    AddTo<NavMeshVolume>(s, vol).size = {12, 4, 12};
    const Worlds worlds = WorldsOf(s);
    BakeJob job;
    job.scene = s;
    job.volume = *FindVolume(s, worlds, vol);
    job.type = s.Setting<NavSettings>()->agentTypes.front();
    job.sources = SourcesOf(reg);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / L"rm_nav_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    job.out = dir / L"Navigation" / PathFromUtf8(FileNameOf(job.volume.name) + ".rmnav");
    CHECK(FileNameOf(job.volume.name) == "範囲 _1_");
    job.head.engine = "0.0.35";
    job.head.fingerprint = "00";
    int calls = 0;
    const BakeOutcome r = RunBake(job, {}, [&](int, int) { ++calls; });
    CHECK(r.ok);
    CHECK(r.triangles == 2);
    CHECK(calls > 0);
    CHECK(std::filesystem::exists(job.out));
    std::filesystem::remove_all(dir, ec);
    // やめる合図が立っていると、焼かずにやめる。
    std::stop_source stop;
    stop.request_stop();
    const BakeOutcome s2 = RunBake(job, stop.get_token(), nullptr);
    CHECK(!s2.ok && s2.stopped);
}
