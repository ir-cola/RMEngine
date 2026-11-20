// ゲーム制作プラグインのシステム (runtime/Systems) のテスト: 固定ステップ、動く順、
// どのエンティティが見えるか - と、組み込みの 回り続ける (game_x_3d/runtime/Spin)。

#include "Test.h"

#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/CameraCycle.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_3d/runtime/Register.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace rm;
using namespace rm::game_production;

namespace {

std::string g_log;  // テスト用のシステムがやったことを、順に
int g_fixedRuns = 0;
float g_lastDt = 0;

void LogA(SystemContext&) { g_log += "A"; }
void LogB(SystemContext&) { g_log += "B"; }
void LogF(SystemContext& c) {
    g_log += "F";
    ++g_fixedRuns;
    g_lastDt = c.dt;
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

}  // namespace

RM_TEST(Systems_OrderFixedFirstThenByOrder) {
    static const SystemInfo b{"B", SystemPhase::Frame, 5, &LogB};
    static const SystemInfo a{"A", SystemPhase::Frame, -1, &LogA};
    static const SystemInfo f{"F", SystemPhase::Fixed, 100, &LogF};
    static const SystemInfo none{"none", SystemPhase::Frame, 0, nullptr};  // 動かすものがない: 外す
    const SystemInfo* list[] = {&b, &a, &f, &none};
    SystemRunner r;
    r.SetSystems(list);
    CHECK(r.Systems().size() == 3);
    Scene s;
    g_log.clear();
    CHECK(r.Advance(s, 0.02, 0.02f) == 1);
    CHECK(g_log == "FAB");
    CHECK(r.Frames() == 1);
}

RM_TEST(Systems_FixedStepCatchesUpAndCarries) {
    static const SystemInfo f{"F", SystemPhase::Fixed, 0, &LogF};
    const SystemInfo* list[] = {&f};
    SystemRunner r;
    r.SetSystems(list);
    Scene s;
    g_fixedRuns = 0;
    // 0.1 秒を 0.02 秒のステップで: 5 ステップ (4 と丸め誤差ではない)。
    CHECK(r.Advance(s, 0.1, 0.02f) == 5);
    CHECK(Near(g_lastDt, 0.02f));
    // 短いフレーム: ステップの時間になるまで、時間を繰り越す。
    CHECK(r.Advance(s, 0.012, 0.02f) == 0);
    CHECK(r.Advance(s, 0.012, 0.02f) == 1);
    CHECK(g_fixedRuns == 6);
    // とても長いフレームでも kMaxFixedSteps より多くは動かず、借りも残さない。
    CHECK(r.Advance(s, 5.0, 0.02f) == SystemRunner::kMaxFixedSteps);
    CHECK(r.Advance(s, 0.0, 0.02f) <= 1);
    // ステップなし: Fixed のシステムは動かない。負の時間: 動かない。
    CHECK(r.Advance(s, 1.0, 0.f) == 0);
    CHECK(r.Advance(s, -1.0, 0.02f) == 0);
    r.Reset();
    CHECK(r.Time() == 0 && r.Frames() == 0);
}

RM_TEST(Systems_ForEachComponent_ActiveOnlyAndEveryOne) {
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_3d::Spin>();
    Scene s;
    s.SetComponentTypes({&spin}, {&spin});
    const int a = s.Create("a");
    const int off = s.Create("off");
    const int under = s.Create("under", off);
    CHECK(s.AddComponent(a, spin));
    CHECK(s.AddComponent(a, spin));  // 複数: この型では許す
    CHECK(s.AddComponent(under, spin));
    s.Find(off)->data.active = false;
    int seen = 0;
    ForEachComponent(s, spin.name, [&](SceneEntity& e, Component&) {
        CHECK(e.data.id == a);  // オフにした親の下のものは違う
        ++seen;
    });
    CHECK(seen == 2);
}

RM_TEST(Systems_Spin_TurnsTheTransform) {
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_3d::Spin>();
    const reflect::TypeDesc& transform = reflect::TypeOf<space3d::Transform>();
    Scene s;
    s.SetComponentTypes({&transform, &spin}, {&spin});
    const int e = s.Create("turning");
    s.AddComponent(e, transform);
    s.AddComponent(e, spin);  // Y まわりに 1 秒で 90 度
    s.AddComponent(e, spin);
    static_cast<game_x_3d::Spin*>(s.Find(e)->FindComponent(spin.name, 1)->value.Get())->degreesPerSecond = {30, 0, 0};
    const int lonely = s.Create("no transform");
    s.AddComponent(lonely, spin);  // 回すものがない: 何も起きない

    static const SystemInfo kSpin{"回り続ける", SystemPhase::Frame, 0, &game_x_3d::RunSpin};
    const SystemInfo* list[] = {&kSpin};
    SystemRunner r;
    r.SetSystems(list);
    r.Advance(s, 0.5, 0.02f);
    const auto& rot = static_cast<const space3d::Transform*>(s.Find(e)->FindComponent(transform.name)->value.Get())->rotation;
    CHECK(Near(rot.y, 45) && Near(rot.x, 15) && Near(rot.z, 0));  // 2 つが足し合わさる
    // -360 と 360 の間に保つ。
    for (int i = 0; i < 10; ++i) r.Advance(s, 1.0, 0.02f);
    CHECK(rot.y > -360 && rot.y < 360);
    CHECK(Near(rot.y, std::fmod(45.f + 900.f, 360.f)));
}

namespace {
uint64_t g_wanted = 0;
void SwitchTo(SystemContext& c) { c.camera = g_wanted; }
}  // namespace

RM_TEST(Systems_SwitchTheCamera) {
    // システムが SystemContext::camera を設定する。ランナーはそれを次のシステムとフレームのために保つ。
    static const SystemInfo kSwitch{"switch", SystemPhase::Frame, 0, &SwitchTo};
    const SystemInfo* list[] = {&kSwitch};
    SystemRunner r;
    r.SetSystems(list);
    Scene s;
    r.SetCamera(7);
    g_wanted = 7;
    r.Advance(s, 0.1, 0.02f);
    CHECK(r.Camera() == 7);
    g_wanted = 9;
    r.Advance(s, 0.1, 0.02f);
    CHECK(r.Camera() == 9);
    r.Reset();
    CHECK(r.Camera() == 0);
}

RM_TEST(Systems_CameraCycle) {
    const reflect::TypeDesc& camera = reflect::TypeOf<game_x_3d::Camera>();
    const reflect::TypeDesc& cycle = reflect::TypeOf<game_x_3d::CameraCycle>();
    Scene s;
    s.SetComponentTypes({&camera, &cycle}, {});
    const int a = s.Create("A");
    s.AddComponent(a, camera);
    const int b = s.Create("B");
    s.AddComponent(b, camera);
    const int off = s.Create("off");
    s.AddComponent(off, camera);
    s.Find(off)->data.active = false;  // オフにしたカメラは飛ばす
    const int director = s.Create("director");
    s.AddComponent(director, cycle);
    static_cast<game_x_3d::CameraCycle*>(s.Find(director)->FindComponent(cycle.name)->value.Get())->seconds = 1.5f;

    static const SystemInfo kCycle{"カメラを順に切り替える", SystemPhase::Frame, 0, &game_x_3d::RunCameraCycle};
    const SystemInfo* list[] = {&kCycle};
    SystemRunner r;
    r.SetSystems(list);
    r.SetCamera(static_cast<uint64_t>(a));
    r.Advance(s, 1.0, 0.02f);  // 0 .. 1: 変わらない
    CHECK(r.Camera() == static_cast<uint64_t>(a));
    r.Advance(s, 1.0, 0.02f);  // 1 .. 2: 1.5 を通る -> B
    CHECK(r.Camera() == static_cast<uint64_t>(b));
    r.Advance(s, 1.0, 0.02f);  // 2 .. 3: 3.0 を通る? (3.0 は終わり: floor(3 / 1.5) = 2 > 1) -> A に戻る
    CHECK(r.Camera() == static_cast<uint64_t>(a));
    // 部品がなければ何も切り替わらない。
    s.RemoveComponent(director, cycle.name);
    r.Advance(s, 5.0, 0.02f);
    CHECK(r.Camera() == static_cast<uint64_t>(a));
}

RM_TEST(Systems_RuntimeRegistry_TheGameReadsTheSameList) {
    // 書き出したゲームがやること: 持っているプラグインのランタイム部分が 1 つのリストに登録される。
    RuntimeRegistry r;
    const PluginId x3d = game_x_3d::kPluginId.Id();
    RegisterRuntimeParts(r, std::span<const PluginId>(&x3d, 1));
    const auto systems = RegisteredSystems(r);
    CHECK(systems.size() == 3 && systems[0]->run == &game_x_3d::RunSpin && systems[1]->run == &game_x_3d::RunCameraCycle &&
          systems[2]->run == &game_x_3d::RunSkyClock);
    // ... とシーンの 空 (3D のゲームのシーンの設定)。
    const auto settings = SettingTypes(r);
    CHECK(settings.size() == 1 && settings[0] == &reflect::TypeOf<game_x_3d::Sky>());
    // コネクターの部品の型は全部、ランタイム部分のもの (Transform, メッシュ, カメラ, 光, 環境光,
    // 回り続ける, カメラを順に切り替える): ゲームはエディターなしでそれらを知っている。
    const auto comps = r.Contributions<ComponentInfo>(kComponentPoint);
    CHECK(comps.size() == 7 && comps[0].plugin == x3d);
    CHECK(comps[0].item->type().name == reflect::TypeOf<space3d::Transform>().name && comps[0].item->onNewEntities && comps[0].item->placement);
    CHECK(std::count_if(comps.begin(), comps.end(), [](const auto& c) { return c.item->multiple; }) == 1);
    // 2D のコネクターも同じ。
    RuntimeRegistry r2;
    const PluginId x2d = game_x_2d::kPluginId.Id();
    RegisterRuntimeParts(r2, std::span<const PluginId>(&x2d, 1));
    CHECK(r2.Contributions<ComponentInfo>(kComponentPoint).size() == 5 && RegisteredSystems(r2).size() == 2);
    CHECK(r.Contributions<SystemInfo>(kComponentPoint).empty());  // その位置にある別の型: 読まない
    // 有効でないプラグインは何も登録しない。
    RuntimeRegistry none;
    const PluginId other{"rm_engine", "space3d"};
    RegisterRuntimeParts(none, std::span<const PluginId>(&other, 1));
    CHECK(RegisteredSystems(none).empty());
}
