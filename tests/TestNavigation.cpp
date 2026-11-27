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
#include "rm_engine/game_x_navigation/runtime/NavWorld.h"
#include "rm_engine/game_x_navigation/runtime/Register.h"
#include "rm_engine/game_x_navigation/runtime/ScriptNav.h"
#include "rm_engine/game_x_navigation/runtime/sdk/RMNavAbi.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "base/Version.h"
#include "platform/Shell.h"
#include "rm_engine/navigation/runtime/Bake.h"
#include "rm_engine/navigation/runtime/Crowd.h"
#include "rm_engine/navigation/runtime/NavFile.h"
#include "rm_engine/navigation/runtime/Query.h"
#include "rm_engine/navigation_x_3d/runtime/Register.h"
#include "rm_engine/navigation_x_physics/runtime/Register.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <cmath>
#include <cstdlib>
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
                         &reflect::TypeOf<physics::Collider>(), &reflect::TypeOf<NavAgent>()});
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
    const int distant = PutShape(s, "遠い", game_x_3d::MeshShape::Box, {500, 0, 0}, {1, 1, 1});
    const int vol = s.Create("1階");
    AddTo<space3d::Transform>(s, vol).position = {0, 1, 0};
    AddTo<NavMeshVolume>(s, vol).size = {22, 6, 22};
    (void)floor, (void)wall, (void)distant;

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

// --- 段階 2: 群れ・Play の間のナビの世界・スクリプト・見本の焼いた物 -----------------------------------------
RM_TEST(NavCrowdWalksAroundTheWallToTheTarget) {
    NavInput in;
    Floor(in, 20, 20);
    AddShape(in, space3d::Shape::Box, {1, 2, 14}, {0, 1, 0});
    NavMesh mesh;
    CHECK(mesh.Load(BakeOk(in, Options({-11, -1, -11}, {11, 4, 11})), nullptr));
    NavCrowd crowd;
    CHECK(crowd.Init(mesh, 8, 0.5f));
    AgentParams p;
    p.maxSpeed = 4;
    p.maxAcceleration = 20;
    const int a = crowd.Add({-5, 0, 0}, p);
    CHECK(a >= 0);
    CHECK(crowd.Add({-50, 0, 0}, p) == -1);  // ナビメッシュの外
    CHECK(crowd.SetTarget(a, {5, 0, 0}));
    crowd.Update(1.f / 60);
    CHECK(crowd.View(a).target == AgentView::Target::Moving);
    CHECK(crowd.Remaining(a) > 16.f);  // 壁を回る道
    std::vector<Vec3> path;
    crowd.Path(a, path);
    CHECK(path.size() >= 3);
    float walked = 0;
    Vec3 last = crowd.View(a).position;
    for (int i = 0; i < 60 * 12 && crowd.Remaining(a) > 0.2f; ++i) {
        crowd.Update(1.f / 60);
        const Vec3 now = crowd.View(a).position;
        walked += std::sqrt((now.x - last.x) * (now.x - last.x) + (now.z - last.z) * (now.z - last.z));
        CHECK(!(now.x > -0.5f && now.x < 0.5f && now.z > -7 && now.z < 7));  // 壁を抜けない
        last = now;
    }
    CHECK(crowd.Remaining(a) <= 0.2f);
    CHECK(Near(last.x, 5, 0.4f) && Near(last.z, 0, 0.4f));
    CHECK(walked > 16.f);
    crowd.ClearTarget(a);
    CHECK(crowd.View(a).target == AgentView::Target::None);
}

RM_TEST(NavTurnTowardTurnsTheShortWay) {
    CHECK(Near(game_x_navigation::TurnToward(0, {1, 0, 0}, 1000), 90));  // +X は 90 度 (+Z が 0)
    CHECK(Near(game_x_navigation::TurnToward(0, {1, 0, 0}, 30), 30));    // 多くて 30 度
    CHECK(Near(game_x_navigation::TurnToward(170, {-0.1f, 0, -1}, 1000), 185.7106f, 0.01f));  // 180 をまたいで短い方へ
    CHECK(Near(game_x_navigation::TurnToward(45, {0, 0, 0}, 1000), 45));  // 向きなし
    // (Transform の回転 Y が 90 度のとき、+Z が +X を向く: 3D 空間の決まりと合っている)
    const space3d::Mat4 r = space3d::RotationMatrix({0, 90, 0});
    const Vec3 f = space3d::TransformPoint(r, {0, 0, 1});
    CHECK(Near(f.x, 1) && Near(f.z, 0));
}

namespace {

// 床と壁のシーン + 焼いた物 (バイト列) を持つ AssetAccess。
struct NavPlay {
    Scene scene = NavScene();
    std::string baked;
    game_production::AssetAccess assets;
    RuntimeRegistry reg;
    std::vector<game_production::SystemEvent> events;
    int agent = 0;

    NavPlay() {
        PutShape(scene, "床", game_x_3d::MeshShape::Plane, {0, 0, 0}, {2, 1, 2});
        PutShape(scene, "壁", game_x_3d::MeshShape::Box, {0, 1, 0}, {1, 2, 14});
        const int vol = scene.Create("1階");
        AddTo<space3d::Transform>(scene, vol).position = {0, 1, 0};
        AddTo<NavMeshVolume>(scene, vol).size = {22, 6, 22};
        navigation_x_3d::RegisterRuntime(reg);
        game_x_navigation::RegisterRuntime(reg);
        const Worlds worlds = WorldsOf(scene);
        const auto v = FindVolume(scene, worlds, vol);
        std::vector<const NavSourceInfo*> sources = SourcesOf(reg);
        NavInput in;
        CollectInput(scene, worlds, *v, sources, nullptr, {}, in);
        navigation::BakeOptions o;
        o.bounds = v->bounds;
        o.agent = AgentOf(scene.Setting<NavSettings>()->agentTypes.front());
        NavData d;
        navigation::Bake(in, o, {}, nullptr, d, nullptr);
        NavFileHead h;
        h.agentType = 1;
        baked = WriteNavFile(d, h);
        static_cast<NavMeshVolume*>(scene.Find(vol)->FindComponent(reflect::TypeOf<NavMeshVolume>().name)->value.Get())->baked = AssetRef{"00000000000000000000000000000abc"};
        std::string* b = &baked;
        assets.bytes = [b](const AssetRef& r) -> std::optional<std::string> {
            if (r.guid == "00000000000000000000000000000abc") return *b;
            return std::nullopt;
        };
        agent = scene.Create("敵");
        AddTo<space3d::Transform>(scene, agent).position = {-5, 0.5f, 0};
        AddTo<NavAgent>(scene, agent).speed = 4;
    }
    void Step(int n) {
        for (int i = 0; i < n; ++i) {
            game_production::SystemContext ctx{scene, 1.f / 60};
            ctx.assets = &assets;
            ctx.events = &events;
            ctx.registry = &reg;
            RunNavigation(ctx);
        }
    }
    Vec3 Position() {
        return static_cast<space3d::Transform*>(scene.Find(agent)->FindComponent(reflect::TypeOf<space3d::Transform>().name)->value.Get())->position;
    }
};

}  // namespace

RM_TEST(NavWorldWalksTheAgentAndSendsArrived) {
    NavPlay play;
    NavWorldOf(play.scene).SetDestination(play.agent, {5, 0, 0});  // (読む前に頼んでよい)
    play.Step(1);
    NavWorld* w = FindNavWorld(play.scene);
    CHECK(w && w->Loaded() && w->Meshes().size() == 1);
    const NavWorld::Agent* a = w ? w->FindAgent(play.agent) : nullptr;
    CHECK(a && a->index >= 0 && a->hasTarget);
    CHECK(Near(play.Position().y, 0, 0.2f));  // 足元がナビメッシュの上に
    play.Step(60 * 10);
    CHECK(Near(play.Position().x, 5, 0.3f) && Near(play.Position().z, 0, 0.3f));
    int arrived = 0;
    for (const auto& e : play.events) arrived += e.name == kNavArrivedEvent && e.target == play.agent ? 1 : 0;
    CHECK(arrived == 1);
    a = w->FindAgent(play.agent);
    CHECK(a && a->arrived && !a->hasTarget);
    // 向きは進んだ向き (最後は +X へ回り込んで着く)。
    const auto rot = static_cast<space3d::Transform*>(play.scene.Find(play.agent)->FindComponent(reflect::TypeOf<space3d::Transform>().name)->value.Get())->rotation;
    CHECK(std::isfinite(rot.y));
    // スクリプトが動かしたら (Transform を書いた)、そこから歩き直す。
    static_cast<space3d::Transform*>(play.scene.Find(play.agent)->FindComponent(reflect::TypeOf<space3d::Transform>().name)->value.Get())->position = {5, 0, -8};
    w->SetDestination(play.agent, {5, 0, 8});
    play.Step(60 * 6);
    CHECK(Near(play.Position().x, 5, 0.3f) && Near(play.Position().z, 8, 0.3f));
    // 止まる: 行き先があっても動かない。
    w->SetDestination(play.agent, {-5, 0, 8});
    w->Stop(play.agent);
    play.Step(30);
    CHECK(Near(play.Position().x, 5, 0.3f));
    w->Resume(play.agent);
    play.Step(60);
    CHECK(play.Position().x < 4.f);
    // 部品を外すと群れから外れる。
    play.scene.RemoveComponent(play.agent, reflect::TypeOf<NavAgent>().name);
    play.Step(1);
    CHECK(w->FindAgent(play.agent) == nullptr);
}

RM_TEST(NavScriptPartAnswers) {
    NavPlay play;
    play.Step(1);
    const auto& part = game_x_navigation::ScriptNavPart();
    CHECK(std::string_view(part.name) == RM_PART_NAV && part.version == RM_PART_NAV_VERSION);
    const auto* api = static_cast<const RmNavApi*>(part.table);
    game_production::ScriptWorld sw;
    sw.scene = &play.scene;
    const float a[3] = {-5, 0, 0}, b[3] = {5, 0, 0};
    float pts[3 * 32];
    int n = 0;
    CHECK(api->findPath(&sw, a, b, pts, 32, &n) == 1);
    CHECK(n >= 4);
    CHECK(api->raycast(&sw, a, b) == 0);  // 壁
    float out[3];
    const float away[3] = {50, 0, 50};
    CHECK(api->closestPoint(&sw, a, out) == 1 && Near(out[1], 0, 0.2f));
    CHECK(api->closestPoint(&sw, away, out) == 0);
    CHECK(api->randomPoint(&sw, a, 3, out) == 1);
    CHECK(api->setDestination(&sw, static_cast<uint64_t>(play.agent), b) == 1);
    CHECK(api->setDestination(&sw, 99999, b) == 0);  // 動く者ではない
    play.Step(2);
    RmNavAgentState st{};
    CHECK(api->state(&sw, static_cast<uint64_t>(play.agent), &st) == 1);
    CHECK(st.onMesh == 1 && st.hasPath == 1 && st.remaining > 10.f);
    CHECK(api->setAreaCost(&sw, "沼", 9) == 1 && api->setAreaCost(&sw, "無いエリア", 9) == 0);
    CHECK(FindNavWorld(play.scene)->Filter(*play.scene.Setting<NavSettings>()).costs[1] == 9.f);
    CHECK(api->setAreaOpen(&sw, "立入禁止", 1) == 1);
    CHECK(!FindNavWorld(play.scene)->Blocked(*play.scene.Setting<NavSettings>()).test(2));
}

// 見本「ナビの見本 (3D)」の焼いた物は、そのシーンのものか (シーンを変えたら焼き直す)。
// 焼き直すとき: 環境変数 RM_REBAKE_NAV_TEMPLATE=1 で RMTests.exe を走らせる (ファイルを書き直す)。
RM_TEST(NavTemplateIsBaked) {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path() / L"plugins" / L"rm_engine" / L"game_x_navigation" /
                                       L"templates" / L"nav_3d" / L"Assets";
    std::string why;
    const auto text = platform::ReadFileBytes(root / L"Scenes" / L"Main.rmscene", &why);
    CHECK(text.has_value());
    if (!text) return;
    const auto doc = game_production::ReadSceneFile(*text, &why);
    CHECK(doc.has_value());
    if (!doc) return;
    Scene s = NavScene();
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<game_x_3d::MeshRenderer>(), &reflect::TypeOf<NavMeshVolume>(),
                         &reflect::TypeOf<NavAgent>()});
    s.Load(*doc);
    RuntimeRegistry reg;
    navigation_x_3d::RegisterRuntime(reg);
    navigation_x_physics::RegisterRuntime(reg);
    const auto sources = SourcesOf(reg);
    const Worlds worlds = WorldsOf(s);
    const auto vols = Volumes(s, worlds);
    CHECK(vols.size() == 1);
    if (vols.size() != 1) return;
    const NavAgentType& type = *FindAgentType(*s.Setting<NavSettings>(), vols[0].volume.agentType);
    const std::string print = FingerprintText(Fingerprint(s, worlds, vols[0], type, sources, nullptr));
    const std::filesystem::path file = root / L"Navigation" / PathFromUtf8("1階.rmnav");
    char* rebake = nullptr;
    size_t len = 0;
    if (_dupenv_s(&rebake, &len, "RM_REBAKE_NAV_TEMPLATE") == 0 && rebake) {
        free(rebake);
        NavInput in;
        CHECK(CollectInput(s, worlds, vols[0], sources, nullptr, {}, in));
        navigation::BakeOptions o;
        o.bounds = vols[0].bounds;
        o.agent = AgentOf(type);
        NavData d;
        CHECK(navigation::Bake(in, o, {}, nullptr, d, nullptr));
        NavFileHead h;
        h.engine = kEngineVersion;
        h.agentType = type.id;
        h.agentName = type.name;
        h.fingerprint = print;
        CHECK(platform::WriteFileAtomic(file, WriteNavFile(d, h), &why));
    }
    const auto bytes = platform::ReadFileBytes(file, &why);
    CHECK(bytes.has_value());
    if (!bytes) return;
    NavFileHead head;
    const auto data = ReadNavFile(*bytes, &head, &why);
    CHECK(data.has_value() && !data->tiles.empty());
    CHECK(head.fingerprint == print);  // 違う: 見本のシーンが変わった。RM_REBAKE_NAV_TEMPLATE=1 で焼き直す
    CHECK(vols[0].volume.baked.guid == "c1e84b2f7d9a4c3e9f0b6a5d8e2c7b13");
}
