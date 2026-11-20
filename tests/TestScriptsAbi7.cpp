// ABI 7 のスクリプト API (RmAbi.h の新しい部分) のテスト。ここで手作りした
// モジュールで行う: コルーチン (待ち - フレーム、ゲーム内の秒、実時間の秒、
// 別のコルーチン -、持ち主の EndPlay による停止、クラッシュ)、
// イベント (全員へ / 1 つへ / 部分木へ、Tick のあと、OnEvent で送ったものは
// 同じフレーム)、時間の倍率、タグ / レイヤー / 子 / 親 (ループは断る)、
// 親を変えても保たれるワールド (3D と 2D)、LookAt、マウス、
// プレイヤーごとの入力とプレイヤーのキー割り当て (保存して読み戻す)、
// スプライトのアニメーション、スクリプトからのパーティクル。

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"
#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"
#include "rm_engine/game_x_2d/runtime/sdk/RM2DAbi.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
#include "rm_engine/game_x_particles/runtime/sdk/RMParticlesAbi.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

#include "platform/Win.h"

using namespace rm;
using namespace rm::game_production;

namespace {

const RmApi* g_api = nullptr;
std::string g_log;
int g_destroyed = 0;

template <class T>
void Construct(void* p) { new (p) T(); }
template <class T>
void Destruct(void* p) { static_cast<T*>(p)->~T(); }
template <class T>
void Copy(void* d, const void* s) { *static_cast<T*>(d) = *static_cast<const T*>(s); }
uint64_t StringGet(const void*, char* buf, uint64_t cap) {
    if (buf && cap) buf[0] = 0;
    return 0;
}
void StringSet(void*, const char*, uint64_t) {}

// 手作りしたコルーチン: 再開のたびに手順を記録し、次の待ちを頼む (もう無ければ終わる)。
struct FakeCo {
    std::vector<RmWait> waits;
    size_t step = 0;
    char mark = 'a';
    bool crash = false;
};
int FakeResume(void* f, RmWait* next) {
    auto& c = *static_cast<FakeCo*>(f);
    if (c.crash) {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    }
    g_log += static_cast<char>(c.mark + static_cast<char>(c.step));
    if (c.step >= c.waits.size()) return 0;
    *next = c.waits[c.step++];
    return 1;
}
void FakeDestroy(void* f) {
    ++g_destroyed;
    delete static_cast<FakeCo*>(f);
}
const RmCoroutinesApi& Co() { return *static_cast<const RmCoroutinesApi*>(g_api->part(g_api->ctx, RM_PART_COROUTINES, RM_PART_COROUTINES_VERSION)); }

// --- スクリプト ---------------------------------------------------------------------
struct Waits {
    uint64_t id = 0;
};
FakeCo* g_next = nullptr;  // 次の BeginPlay が始めるもの
void WaitsBegin(void* s, RmEntityId) {
    FakeCo* co = std::exchange(g_next, nullptr);
    if (co) static_cast<Waits*>(s)->id = Co().start(g_api->ctx, co, &FakeResume, &FakeDestroy);
}

struct Listener {
    int got = 0;
};
void ListenerEvent(void* s, RmEntityId e, const RmEvent* ev) {
    ++static_cast<Listener*>(s)->got;
    g_log += std::to_string(e) + ev->name + std::to_string(static_cast<int>(ev->number)) + ev->text + "/" + std::to_string(ev->sender) + " ";
    // OnEvent から送った答えは同じフレームに届く。
    if (std::string(ev->name) == "Ping") {
        const auto* t = static_cast<const RmEventsApi*>(g_api->part(g_api->ctx, RM_PART_EVENTS, RM_PART_EVENTS_VERSION));
        t->send(g_api->ctx, 1, ev->sender, "Pong", 0, "", 0, 0);
    }
}
void ListenerTick(void*, RmEntityId e, float) { g_log += "T" + std::to_string(e) + " "; }

struct Sender {
    int frame = 0;
};
RmEntityId g_one = 0, g_down = 0;
void SenderTick(void* s, RmEntityId e, float) {
    g_log += "T" + std::to_string(e) + " ";
    if (++static_cast<Sender*>(s)->frame != 1) return;
    const auto* t = static_cast<const RmEventsApi*>(g_api->part(g_api->ctx, RM_PART_EVENTS, RM_PART_EVENTS_VERSION));
    t->send(g_api->ctx, 0, 0, "All", 1, "x", 1, 0);
    t->send(g_api->ctx, 1, g_one, "One", 2, "yy", 1, 0);  // (文字は 1 バイトだけ)
    t->send(g_api->ctx, 2, g_down, "Down", 3, "", 0, 0);
}

const RmScriptDesc kScripts[] = {
    {"co::Waits", "待つ", "Waits.h", sizeof(Waits), alignof(Waits), nullptr, 0, &Construct<Waits>, &Destruct<Waits>, &Copy<Waits>, &WaitsBegin},
    {"ev::Listener", "聞く", "Listener.h", sizeof(Listener), alignof(Listener), nullptr, 0, &Construct<Listener>, &Destruct<Listener>,
     &Copy<Listener>, nullptr, &ListenerTick, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &ListenerEvent},
    {"ev::Sender", "送る", "Sender.h", sizeof(Sender), alignof(Sender), nullptr, 0, &Construct<Sender>, &Destruct<Sender>, &Copy<Sender>, nullptr,
     &SenderTick},
};
constexpr uint32_t kFlags = kEngineDebug ? RmModuleFlag_Debug : 0;
const RmModule kModule{RM_ABI_VERSION, kFlags, 3, kScripts, &StringGet, &StringSet};

Scene SceneWith(const ScriptModule& m, std::vector<const reflect::TypeDesc*> more = {}) {
    Scene s;
    std::vector<const reflect::TypeDesc*> types = m.Types();
    for (const reflect::TypeDesc* t : more) types.push_back(t);
    s.SetComponentTypes(types, m.Types());
    return s;
}

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool Near(Vec3 a, Vec3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

// ホストが動かす 1 フレーム: 実時間の秒と、倍率をかけた秒。
void Step(ScriptRunner& r, Scene& s, ScriptWorld& w, const ScriptModule& m, float realDt) {
    w.unscaledDt = realDt;
    r.Frame(s, w, m, {}, realDt * w.timeScale);
}

}  // namespace

RM_TEST(Abi7_Coroutine_GameTimeAndRealTime) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int e = s.Create("E");
    s.AddComponent(e, m.Find("co::Waits")->desc);
    ScriptWorld w;
    const RmApi api = MakeScriptApi(w);
    g_api = &api;
    g_log.clear();
    g_destroyed = 0;
    g_next = new FakeCo{{RmWait{RmWait_Frames, 1, 0, 0}, RmWait{RmWait_Seconds, 0, 0.25, 0}, RmWait{RmWait_RealSeconds, 0, 1.0, 0}}};
    ScriptRunner r;
    std::vector<int> frameOf;
    for (int f = 0; f < 30; ++f) {
        const size_t before = g_log.size();
        Step(r, s, w, m, 0.1f);
        for (size_t i = before; i < g_log.size(); ++i) frameOf.push_back(f);
    }
    CHECK(g_log == "abcd");
    // b: 次のフレーム。c: 待ちはフレーム 1 の終わり (0.2 秒) に始まった ->
    // 0.45 秒: フレーム 4 の終わり。d: その 1 実時間秒後 (1.5 秒): フレーム 14 の終わり。
    // そのあと解放される。
    CHECK((frameOf == std::vector<int>{0, 1, 4, 14}));
    CHECK(g_destroyed == 1 && w.coroutines.empty());
    // 時間の倍率が 0 のとき、ゲーム内の秒は止まるが、実時間の秒は進む。
    g_log.clear();
    g_destroyed = 0;
    ScriptWorld w2;
    const RmApi api2 = MakeScriptApi(w2);
    g_api = &api2;
    Scene s2 = SceneWith(m);
    s2.AddComponent(s2.Create("E"), m.Find("co::Waits")->desc);
    g_next = new FakeCo{{RmWait{RmWait_Seconds, 0, 0.05, 0}}};
    FakeCo* real = new FakeCo{{RmWait{RmWait_RealSeconds, 0, 0.05, 0}}, 0, 'A'};
    w2.timeScale = 0;
    ScriptRunner r2;
    Step(r2, s2, w2, m, 0.1f);  // a (スクリプトで始めた) と、外で始めた (持ち主なし)
    w2.scene = &s2;
    const uint64_t loose = Co().start(api2.ctx, real, &FakeResume, &FakeDestroy);
    CHECK(loose != 0 && Co().running(api2.ctx, loose) == 1);
    for (int f = 0; f < 5; ++f) Step(r2, s2, w2, m, 0.1f);
    CHECK(g_log == "aAB");  // ゲーム内の秒は倍率 0 でずっと待つ。実時間の秒は終わった
    CHECK(Co().running(api2.ctx, loose) == 0 && w2.coroutines.size() == 1);
    r2.Stop(s2, w2, m, {});  // Stop: 全部解放される
    CHECK(w2.coroutines.empty() && g_destroyed == 2);
    g_api = nullptr;
}

RM_TEST(Abi7_Coroutine_StoppedWithItsScript_WaitsForAnother_Crash) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int a = s.Create("A");
    s.AddComponent(a, m.Find("co::Waits")->desc);
    ScriptWorld w;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    const RmApi api = MakeScriptApi(w);
    g_api = &api;
    g_log.clear();
    g_destroyed = 0;
    g_next = new FakeCo{{RmWait{RmWait_Seconds, 0, 100.0, 0}}};
    ScriptRunner r;
    Step(r, s, w, m, 0.1f);
    const uint64_t id = static_cast<const Waits*>(s.Find(a)->FindComponent("co::Waits")->value.Get())->id;
    CHECK(id != 0 && Co().running(api.ctx, id) == 1);
    // 外で始めたものは、そのコルーチン (RmWait_Coroutine) を待つ。
    w.scene = &s;
    const uint64_t waiter = Co().start(api.ctx, new FakeCo{{RmWait{RmWait_Coroutine, 0, 0, id}}, 0, 'W'}, &FakeResume, &FakeDestroy);
    Step(r, s, w, m, 0.1f);
    CHECK(g_log == "aW" && Co().running(api.ctx, waiter) == 1);
    // エンティティを切る: EndPlay - そのコルーチンは消える。待っていた方は、
    // そのフレームで走り続ける (そして終わる)。
    s.Find(a)->data.active = false;
    Step(r, s, w, m, 0.1f);
    CHECK(Co().running(api.ctx, id) == 0 && g_log == "aWX" && Co().running(api.ctx, waiter) == 0 && g_destroyed == 2);
    // id で止める。クラッシュするコルーチン: 止めて知らせ、ゲームは続く。
    const uint64_t later = Co().start(api.ctx, new FakeCo{{RmWait{RmWait_Frames, 5, 0, 0}}, 0, 'L'}, &FakeResume, &FakeDestroy);
    Co().stop(api.ctx, later);
    CHECK(Co().running(api.ctx, later) == 0 && g_destroyed == 3);
    FakeCo* bad = new FakeCo{{}, 0, 'Z', true};
    CHECK(Co().start(api.ctx, bad, &FakeResume, &FakeDestroy) == 0);
    CHECK(told.find("コルーチンが止まりました") != std::string::npos);
    delete bad;  // (エンジンはクラッシュしたもののフレームには触らない)
    r.Stop(s, w, m, {});
    g_api = nullptr;
}

RM_TEST(Abi7_Events_EveryoneOneSubtree_AfterTheTicks) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const reflect::TypeDesc& listener = m.Find("ev::Listener")->desc;
    const int sender = s.Create("S");
    s.AddComponent(sender, m.Find("ev::Sender")->desc);
    const int a = s.Create("A");
    s.AddComponent(a, listener);
    const int p = s.Create("P");
    s.AddComponent(p, listener);
    const int c = s.Create("C", p);
    s.AddComponent(c, listener);
    const int off = s.Create("Off");
    s.AddComponent(off, listener);
    s.Find(off)->data.active = false;
    g_one = static_cast<RmEntityId>(a);
    g_down = static_cast<RmEntityId>(p);
    ScriptWorld w;
    const RmApi api = MakeScriptApi(w);
    g_api = &api;
    g_log.clear();
    ScriptRunner r;
    Step(r, s, w, m, 0.1f);
    const std::string A = std::to_string(a), P = std::to_string(p), C = std::to_string(c), S = std::to_string(sender);
    // まず全部の Tick (名前順で、受け取り側が送り手より先)、そのあと送った順にイベント。切ってある
    // 受け取り側には何も届かない。
    CHECK(g_log == "T" + A + " T" + P + " T" + C + " T" + S + " " + A + "All1x/" + S + " " + P + "All1x/" + S + " " + C + "All1x/" + S + " " + A +
                       "One2y/" + S + " " + P + "Down3/" + S + " " + C + "Down3/" + S + " ");
    // OnEvent で送ったもの (Ping -> 送り手へ Pong) は同じフレームに届く。
    g_log.clear();
    w.scene = &s;
    w.currentEntity = a;  // (A のスクリプトが送ったかのように)
    const auto* t = static_cast<const RmEventsApi*>(api.part(api.ctx, RM_PART_EVENTS, RM_PART_EVENTS_VERSION));
    t->send(api.ctx, 1, static_cast<RmEntityId>(p), "Ping", 0, "", 0, 0);
    w.currentEntity = 0;
    r.DeliverEvents(s, w, m, {});
    CHECK(g_log == P + "Ping0/" + A + " " + A + "Pong0/" + P + " ");
    // 無くなったものへ: 誰にも届かない。
    g_log.clear();
    t->send(api.ctx, 1, 99999, "Lost", 0, "", 0, 0);
    r.DeliverEvents(s, w, m, {});
    CHECK(g_log.empty() && w.events.empty());
    r.Stop(s, w, m, {});
    g_api = nullptr;
}

RM_TEST(Abi7_TimeScale_TagsLayersChildrenParents) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    ScriptWorld w;
    const RmApi api = MakeScriptApi(w);
    const auto& time = *static_cast<const RmTimeApi*>(api.part(api.ctx, RM_PART_TIME, RM_PART_TIME_VERSION));
    const auto& ent = *static_cast<const RmEntityApi*>(api.part(api.ctx, RM_PART_ENTITY, RM_PART_ENTITY_VERSION));
    // 時間の倍率: 0 から 100 に収める (壊れた数は: 1)。
    time.setScale(api.ctx, 0.5f);
    CHECK(time.scale(api.ctx) == 0.5f);
    time.setScale(api.ctx, -3);
    CHECK(time.scale(api.ctx) == 0);
    time.setScale(api.ctx, std::nanf(""));
    CHECK(time.scale(api.ctx) == 1);
    time.setScale(api.ctx, 0.5f);
    ScriptRunner r;
    Step(r, s, w, m, 0.2f);
    Step(r, s, w, m, 0.2f);
    CHECK(Near(static_cast<float>(w.time), 0.2f) && Near(static_cast<float>(time.unscaledTime(api.ctx)), 0.4f) && time.frameCount(api.ctx) == 2);
    CHECK(Near(time.unscaledDeltaTime(api.ctx), 0.2f));
    // タグとレイヤー。
    w.scene = &s;
    const int a = s.Create("A"), b = s.Create("B"), c = s.Create("C", a);
    ent.setTag(api.ctx, static_cast<RmEntityId>(c), "Enemy");
    ent.setTag(api.ctx, static_cast<RmEntityId>(b), "Enemy");
    RmEntityId found[4] = {};
    CHECK(ent.findWithTag(api.ctx, "Enemy", found, 1) == 2 && found[0] == static_cast<RmEntityId>(c));  // 階層の順: C (A の下) が先
    char buf[16];
    CHECK(ent.getTag(api.ctx, static_cast<RmEntityId>(b), buf, sizeof(buf)) == 5 && std::string(buf) == "Enemy");
    const std::vector<std::string> layers{"Water", "UI"};
    w.layers = &layers;
    ent.setLayer(api.ctx, static_cast<RmEntityId>(a), "UI");
    CHECK(s.Find(a)->data.layer == "UI");
    CHECK(ent.layerIndex(api.ctx, "") == 0 && ent.layerIndex(api.ctx, "Water") == 1 && ent.layerIndex(api.ctx, "UI") == 2 && ent.layerIndex(api.ctx, "X") == -1);
    // 子 (0: 最上位) と親: 自分の下へは断る。その場で動かす。
    CHECK(ent.children(api.ctx, 0, found, 4) == 2 && found[0] == static_cast<RmEntityId>(a) && found[1] == static_cast<RmEntityId>(b));
    CHECK(ent.setParent(api.ctx, static_cast<RmEntityId>(a), static_cast<RmEntityId>(c), 1, -1) == 0);
    CHECK(ent.setParent(api.ctx, static_cast<RmEntityId>(b), static_cast<RmEntityId>(a), 1, 0) == 1);
    CHECK(s.Find(b)->data.parent == a && ent.children(api.ctx, static_cast<RmEntityId>(a), found, 4) == 2 && found[0] == static_cast<RmEntityId>(b));
    CHECK(ent.setParent(api.ctx, static_cast<RmEntityId>(c), 0, 0, -1) == 1 && s.Find(c)->data.parent == 0);
    ent.setName(api.ctx, static_cast<RmEntityId>(c), "Renamed");
    CHECK(s.Find(c)->data.name == "Renamed");
    // シーンと一緒に保存する (そして読み戻す)。
    Scene back;
    back.Load(s.Save());
    CHECK(back.Find(b)->data.tag == "Enemy" && back.Find(a)->data.layer == "UI");
}

RM_TEST(Abi7_KeepWorld_3DAnd2D_LookAt) {
    // 3D: 親を動かし、回し、拡大する。子は親の下に入れても、ワールドでの位置を保つ。
    const reflect::TypeDesc& tf = reflect::TypeOf<space3d::Transform>();
    Scene s;
    s.SetComponentTypes({&tf}, {});
    const int parent = s.Create("Parent"), child = s.Create("Child");
    s.AddComponent(parent, tf);
    s.AddComponent(child, tf);
    auto* pt = static_cast<space3d::Transform*>(s.Find(parent)->FindComponent(tf.name)->value.Get());
    auto* ct = static_cast<space3d::Transform*>(s.Find(child)->FindComponent(tf.name)->value.Get());
    *pt = {{10, 0, 0}, {0, 90, 0}, {2, 2, 2}};
    *ct = {{1, 2, 3}, {0, 0, 30}, {1, 1, 1}};
    ScriptWorld w;
    w.scene = &s;
    const auto& t3 = *static_cast<const RmTransform3DApi2*>(game_x_3d::ScriptTransform3DPart2().table);
    float before[16], now[16], after[3];
    t3.worldMatrix(&w, static_cast<uint64_t>(child), before);
    game_x_3d::KeepWorld3D(s, child, parent);
    const int ids[] = {child};
    s.Move(ids, parent, Scene::kEnd);
    t3.worldMatrix(&w, static_cast<uint64_t>(child), now);
    bool same = true;
    for (int i = 0; i < 16; ++i) same = same && Near(before[i], now[i]);
    CHECK(same && Near(ct->scale.x, 0.5f) && s.Find(child)->data.parent == parent);
    after[0] = now[12];
    after[1] = now[13];
    after[2] = now[14];
    // LookAt: その +Z (左手系の前) を目標へ、ワールドで (回した親の下でも)。
    const float target[3] = {after[0] + 5, after[1], after[2]}, up[3] = {0, 1, 0};
    CHECK(t3.lookAt(&w, static_cast<uint64_t>(child), target, up) == 1);
    float m[16];
    t3.worldMatrix(&w, static_cast<uint64_t>(child), m);
    const Vec3 forward = space3d::Normalize(Vec3{m[8], m[9], m[10]});
    CHECK(Near(forward, Vec3{1, 0, 0}));
    // ワールド位置を設定: そこへ行く。
    const float there[3] = {-4, 5, 6};
    t3.setWorldPosition(&w, static_cast<uint64_t>(child), there);
    t3.worldPosition(&w, static_cast<uint64_t>(child), after);
    CHECK(Near(Vec3{after[0], after[1], after[2]}, Vec3{-4, 5, 6}));

    // 2D: 回して拡大した親と、描く順で同じこと。
    const reflect::TypeDesc& t2d = reflect::TypeOf<space2d::Transform2D>();
    Scene s2;
    s2.SetComponentTypes({&t2d}, {});
    const int p2 = s2.Create("P"), c2 = s2.Create("C");
    s2.AddComponent(p2, t2d);
    s2.AddComponent(c2, t2d);
    auto* q = static_cast<space2d::Transform2D*>(s2.Find(p2)->FindComponent(t2d.name)->value.Get());
    auto* k = static_cast<space2d::Transform2D*>(s2.Find(c2)->FindComponent(t2d.name)->value.Get());
    *q = {{5, 5}, 90, {2, 2}, 3};
    *k = {{1, 0}, 10, {1, 1}, 7};
    w.scene = &s2;
    const auto& t2 = *static_cast<const RmTransform2DApi2*>(game_x_2d::ScriptTransform2DPart2().table);
    float p0[2], p1[2], r1[2];
    t2.worldGet(&w, static_cast<uint64_t>(c2), 0, p0);
    game_x_2d::KeepWorld2D(s2, c2, p2);
    const int ids2[] = {c2};
    s2.Move(ids2, p2, Scene::kEnd);
    t2.worldGet(&w, static_cast<uint64_t>(c2), 0, p1);
    t2.worldGet(&w, static_cast<uint64_t>(c2), 1, r1);
    CHECK(Near(p0[0], p1[0]) && Near(p0[1], p1[1]) && Near(r1[0], 10) && k->order == 4);  // ワールドで 3 + 4 = 7
    const float right[2] = {p1[0], p1[1] + 3};  // そこから真上: +X を 90 に回した
    t2.lookAt(&w, static_cast<uint64_t>(c2), right);
    t2.worldGet(&w, static_cast<uint64_t>(c2), 1, r1);
    CHECK(Near(r1[0], 90));
}

RM_TEST(Abi7_Mouse_InputByPlayer_Bindings) {
    ScriptWorld w;
    const RmApi api = MakeScriptApi(w);
    const auto& mouse = *static_cast<const RmMouseApi*>(api.part(api.ctx, RM_PART_MOUSE, RM_PART_MOUSE_VERSION));
    float at[2];
    w.SetPointer(Vec2{100, 50}, {1280, 720});
    CHECK(mouse.position(api.ctx, at) == 1 && at[0] == 100 && at[1] == 50);
    w.SetPointer(Vec2{110, 40}, {1280, 720});
    mouse.delta(api.ctx, at);
    CHECK(at[0] == 10 && at[1] == -10);
    w.SetPointer(std::nullopt, {1280, 720});  // ゲームの外へ出た: 最後にいた所のまま、動かない
    CHECK(mouse.position(api.ctx, at) == 0 && at[0] == 110);
    mouse.delta(api.ctx, at);
    CHECK(at[0] == 0 && at[1] == 0);

    // プレイヤー別: 2P のパッドは MoveX を押し、キーボードは 1P のもの。
    const std::vector<InputAction> list = DefaultInputActions();
    w.actions = &list;
    InputFrame f;
    f.keys[VK_RIGHT] = 1;
    f.pads[1][static_cast<size_t>(static_cast<int>(InputControl::PadLeftStickX) - kPadControlBase)] = -0.8f;
    f.padConnected[1] = 1;
    w.readInput = [&] { return f; };
    w.ReadInput();
    const auto& in = *static_cast<const RmInputApi2*>(api.part(api.ctx, RM_PART_INPUT, RM_PART_INPUT_VERSION2));
    CHECK(Near(in.axis(api.ctx, "MoveX", 0), 1) && Near(in.axis(api.ctx, "MoveX", 1), -0.8f) && Near(in.axis(api.ctx, "MoveX", 2), 0));
    CHECK(in.padConnected(api.ctx, 1) == 1 && in.padConnected(api.ctx, 0) == 0);
    w.keyboardPlayer = 1;  // キーボードは今は 2P のもの: 1P は何もない
    CHECK(Near(in.axis(api.ctx, "MoveX", 0), 0) && Near(in.axis(api.ctx, "MoveX", 1), 0.2f));
    // 押された順: 先にキー、スティックを左へ: -1。
    float dir = 0;
    CHECK(in.anyPressed(api.ctx, -1, &dir) == VK_RIGHT && dir == 1);
    // キーは押したまま: 2P のスティックを左へ倒したものが押された扱いになる。
    w.inputBefore = InputFrame{};
    w.inputBefore.keys[VK_RIGHT] = 1;
    w.inputNow = f;
    CHECK(in.anyPressed(api.ctx, 1, &dir) == static_cast<int32_t>(InputControl::PadLeftStickX) && dir == -1);
    char name[32];
    in.controlName(api.ctx, static_cast<int32_t>(InputControl::PadA), name, sizeof(name));
    CHECK(std::string(name) == "PadA" && ControlName(0x5B) == "Key0x5B" && ControlName(0x20) == "Space");

    // プレイヤーのキー割り当て: Jump を J に、保存、リセット、読み戻す。
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / (L"rm_bindings_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    w.saveFolder = folder;
    CHECK(in.bindingCount(api.ctx, "Jump") == 2);
    CHECK(in.setBinding(api.ctx, "Jump", 0, 'J', 1) == 1 && in.setBinding(api.ctx, "Jump", 2, 'K', 1) == 1 && in.setBinding(api.ctx, "Jump", 4, 'L', 1) == 0);
    CHECK(in.removeBinding(api.ctx, "Jump", 1) == 1 && in.bindingCount(api.ctx, "Jump") == 2);
    int32_t control = 0;
    float scale = 0;
    CHECK(in.binding(api.ctx, "Jump", 1, &control, &scale) == 1 && control == 'K' && scale == 1);
    CHECK(list[2].bindings.size() == 2 && list[2].bindings[0].control == InputControl::Space);  // 設定のものは変わらない
    CHECK(in.saveBindings(api.ctx) == 1);
    in.resetBindings(api.ctx);
    CHECK(in.bindingCount(api.ctx, "Jump") == 2 && w.Actions()[2].bindings[0].control == InputControl::Space);
    CHECK(in.loadBindings(api.ctx) == 1 && w.Actions()[2].bindings[0].control == static_cast<InputControl>('J'));
    // ファイルに無い操作はゲームのものを残し、ゲームにもう無い操作は取り込まない。
    std::vector<InputAction> game = list;
    game.push_back({"Dash", {{InputControl::Shift, 1}}});
    json::Value saved = BindingsToJson(w.Actions());
    json::Value gone = json::Value::MakeObject();
    gone.Set("name", json::Value("Gone"));
    saved.Find("actions")->Push(std::move(gone));
    const std::vector<InputAction> merged = BindingsFromJson(game, saved);
    CHECK(merged.size() == game.size() && merged.back().name == "Dash" && merged[2].bindings[0].control == static_cast<InputControl>('J'));
    std::filesystem::remove_all(folder, ec);
}

RM_TEST(Abi7_SpriteAnimation_AndParticles_FromScripts) {
    const reflect::TypeDesc& sprite = reflect::TypeOf<game_x_2d::Sprite>();
    const reflect::TypeDesc& anim = reflect::TypeOf<game_x_2d::SpriteAnimation>();
    const reflect::TypeDesc& fx = reflect::TypeOf<game_x_particles::ParticleSystem>();
    Scene s;
    s.SetComponentTypes({&sprite, &anim, &fx}, {&fx});
    const int e = s.Create("E");
    s.AddComponent(e, sprite);
    s.AddComponent(e, anim);
    s.AddComponent(e, fx);
    s.AddComponent(e, fx);
    auto* sp = static_cast<game_x_2d::Sprite*>(s.Find(e)->FindComponent(sprite.name)->value.Get());
    auto* an = static_cast<game_x_2d::SpriteAnimation*>(s.Find(e)->FindComponent(anim.name)->value.Get());
    sp->columns = 4;
    sp->rows = 2;
    an->framesPerSecond = 10;
    ScriptWorld w;
    w.scene = &s;
    const uint64_t id = static_cast<uint64_t>(e);
    const auto& a = *static_cast<const RmSpriteAnimationApi*>(game_x_2d::ScriptSpriteAnimationPart().table);
    SystemContext ctx{s, 0.25f};
    auto run = [&](float dt) {
        ctx.dt = dt;
        game_x_2d::RunSpriteAnimation(ctx);
    };
    // スクリプトから、そのスプライトの コマの並び を再生する。速さを変えても今のコマは保たれる。
    const int32_t run4[] = {4, 5, 6, 7};
    a.setFrames(&w, id, run4, 4);
    run(0.25f);
    CHECK(a.step(&w, id) == 2 && sp->cell == 6 && a.playing(&w, id) == 1);
    a.setSpeed(&w, id, 1);
    CHECK(a.step(&w, id) == 2);
    run(1.0f);
    CHECK(a.step(&w, id) == 3 && sp->cell == 7);
    // 一時停止: そのまま。コマへ飛ぶ。停止: 最初のコマ、そのまま。また再生。
    a.pause(&w, id, 1);
    run(5.0f);
    CHECK(a.step(&w, id) == 3 && a.playing(&w, id) == 0);
    a.setStep(&w, id, 1);
    run(0);
    CHECK(sp->cell == 5);
    a.stop(&w, id);
    run(3.0f);
    CHECK(a.step(&w, id) == 0 && sp->cell == 4);
    a.play(&w, id, 0);
    a.setLoop(&w, id, 0);
    run(10.0f);
    CHECK(a.step(&w, id) == 3 && a.playing(&w, id) == 0 && a.loop(&w, id) == 0);  // ループしない: 最後で止まる

    // パーティクル: スクリプトが頼んだことは、描く側のために部品に残る (全部、または 1 つ)。
    const auto& p = *static_cast<const RmParticlesApi*>(game_x_particles::ScriptParticlesPart().table);
    auto* first = static_cast<game_x_particles::ParticleSystem*>(s.Find(e)->FindComponent(fx.name, 0)->value.Get());
    auto* second = static_cast<game_x_particles::ParticleSystem*>(s.Find(e)->FindComponent(fx.name, 1)->value.Get());
    CHECK(p.count(&w, id) == 2 && p.playing(&w, id, RM_PARTICLES_ALL) == 1);
    p.stop(&w, id, RM_PARTICLES_ALL, 0);
    CHECK(p.playing(&w, id, RM_PARTICLES_ALL) == 0 && first->scriptPlay == 2 && second->scriptPlay == 2 && first->restarts == 0);
    p.play(&w, id, 1, 1);
    CHECK(p.playing(&w, id, 1) == 1 && p.playing(&w, id, 0) == 0 && second->restarts == 1);
    p.emit(&w, id, 0, 30);
    p.pause(&w, id, RM_PARTICLES_ALL, 1);
    p.setSpeed(&w, id, RM_PARTICLES_ALL, 50);
    CHECK(first->emitted == 30 && second->emitted == 0 && first->paused && second->paused && first->speed == 10 && p.speed(&w, id, 1) == 10);

    // エミッターの発生: 止まっていれば頼まれた分だけ。持っている数より多くは出ない。
    particles::ParticleEmitter em;
    em.rate = 100;
    em.burst = 0;
    em.capacity = 50;
    particles::EmitterClock clock;
    CHECK(particles::EmitterBirths(clock, em, 0.1f, true, 0) == 10);
    CHECK(particles::EmitterBirths(clock, em, 0.1f, false, 5) == 5 && clock.age > 0.09f && clock.age < 0.11f);  // (時計は止まったまま)
    CHECK(particles::EmitterBirths(clock, em, 0.1f, true, 1000) == 50);
}

RM_TEST(Abi7_ScriptTimes_OnlyWhenAsked) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    s.AddComponent(s.Create("A"), m.Find("ev::Listener")->desc);
    s.AddComponent(s.Create("B"), m.Find("ev::Listener")->desc);
    ScriptWorld w;
    const RmApi api = MakeScriptApi(w);
    g_api = &api;
    ScriptRunner r;
    Step(r, s, w, m, 0.1f);
    CHECK(r.Times().empty());
    r.SetTiming(true);
    Step(r, s, w, m, 0.1f);
    const auto times = r.Times();
    CHECK(times.size() == 1 && times[0].type == m.Find("ev::Listener") && times[0].calls == 2 && times[0].ms >= 0);
    r.Stop(s, w, m, {});
    g_api = nullptr;
}
