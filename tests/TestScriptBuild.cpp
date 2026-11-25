// 本物の Visual Studio で全部の道を通す: テンプレート 「ゲーム」 のサンプルスクリプト (ArrowMover) を
// MSBuild で DLL にビルドし (先に rmgen --scripts)、ランタイムが読み、エンティティに付けて動かす:
// BeginPlay、右矢印キーで動かす Tick、EndPlay。Visual Studio 2022 の無い PC では飛ばす (そう知らせる)。
// 生成した Visual Studio プロジェクトのテキストとエラーの行も調べる。

#include "Test.h"

#include "core/reflect/Data.h"

#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "platform/Process.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <thread>
#include <filesystem>

using namespace rm;
using namespace rm::game_production;
namespace fs = std::filesystem;

namespace {

fs::path Repo() { return fs::path(__FILE__).parent_path().parent_path(); }

fs::path ExeFolder() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

}  // namespace

RM_TEST(ScriptBuild_ProjectText_AndErrorLines) {
    ScriptBuildRequest r;
    r.project = L"C:/Games/My & Game";
    r.engine = L"C:/RM";
    r.sdk = {L"C:/RM/Sdk"};
    const std::string v = ScriptVcxproj(r);
    CHECK(v.find("<ConfigurationType>DynamicLibrary</ConfigurationType>") != std::string::npos);
    CHECK(v.find("<PlatformToolset>v143</PlatformToolset>") != std::string::npos);
    CHECK(v.find("MultiThreadedDLL") != std::string::npos && v.find("stdcpp20") != std::string::npos);
    CHECK(v.find("rmgen.exe&quot; --scripts") != std::string::npos);
    CHECK(v.find("C:\\RM\\Sdk\\;") != std::string::npos);
    CHECK(ScriptSln().find("Library\\Scripts\\Scripts.vcxproj") != std::string::npos);
    const auto lines = ErrorLines(
        "Mover.cpp(3,5): error C2065: 'x': undeclared identifier [C:\\p\\Scripts.vcxproj]\n"
        "Mover.cpp(4,1): warning C4100: unused\n"
        "Mover.cpp(3,5): error C2065: 'x': undeclared identifier [C:\\p\\Scripts.vcxproj]\n");
    CHECK(lines.size() == 1 && lines[0] == "Mover.cpp(3,5): error C2065: 'x': undeclared identifier");
}

RM_TEST(ScriptBuild_RealMSBuild_TheSampleRuns) {
    if (FindMSBuild().empty()) {
        std::printf("    (Visual Studio 2022 が無いので、本物のビルドは飛ばしました)\n");
        return;
    }
    const fs::path project = fs::temp_directory_path() / (L"rm_script_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(ScriptsFolder(project), ec);
    fs::copy(Repo() / L"plugins/rm_engine/game_production/templates/game/Assets/Scripts", ScriptsFolder(project), fs::copy_options::recursive, ec);
    CHECK(!ec);
    // グローバル変数とセーブのスクリプト (ビルドが書くヘッダーの Global::<name>)。
    {
        std::ofstream h(ScriptsFolder(project) / L"Scorer.h", std::ios::binary);
        h << "#pragma once\n#include \"RM.h\"\nRM_SCRIPT()\nstruct Scorer {\n    RM_PROPERTY() int n = 0;\n    void Tick(rm::Entity self, float dt);\n};\n";
        std::ofstream c(ScriptsFolder(project) / L"Scorer.cpp", std::ios::binary);
        c << "#include \"Scorer.h\"\n"
             "void Scorer::Tick(rm::Entity, float) {\n"
             "    Global::score += 5;\n"
             "    ++Global::score;\n"
             "    Global::player = Global::player.Get() + \"!\";\n"
             "    Global::home = rm::Vec3{1, 2, 3};\n"
             "    if (!Global::cleared) Global::cleared = true;\n"
             "    rm::GlobalVar<float>(\"speed\") *= 2.0f;\n"
             "    if (!rm::Save::Exists(1)) rm::Save::Write(1);\n"
             "}\n";
    }
    // ABI 7 のスクリプト: コルーチン (フレーム、秒、条件)、タイマー、イベントと OnEvent、タグ、
    // 親の設定、数学 - と、2D / 3D / パーティクルの部品のコンパイル (ここでは何もしない)。
    {
        std::ofstream h(ScriptsFolder(project) / L"Waiter.h", std::ios::binary);
        h << R"rm(#pragma once
#include "RM.h"
RM_SCRIPT()
struct Waiter {
    RM_PROPERTY() int steps = 0;
    RM_PROPERTY() int timers = 0;
    RM_PROPERTY() int events = 0;
    RM_PROPERTY() float sum = 0;
    RM_PROPERTY() std::string log;
    void BeginPlay(rm::Entity self);
    void OnEvent(rm::Entity self, const rm::Event& e);
};
)rm";
        std::ofstream c(ScriptsFolder(project) / L"Waiter.cpp", std::ios::binary);
        c << R"rm(#include "Waiter.h"
#include "RM2D.h"
#include "RM3D.h"
#include "RMParticles.h"
#include "RMAnim.h"
static rm::Task Count(Waiter* w) {
    w->log += "a";
    co_await rm::NextFrame();
    w->log += "b";
    co_await rm::WaitSeconds(0.25f);
    w->log += "c";
    co_await rm::WaitUntil([w] { return w->events > 0; });
    w->log += "d";
}
void Waiter::BeginPlay(rm::Entity self) {
    rm::StartCoroutine(Count(this));
    rm::After(0.15f, [this] { ++timers; });
    rm::Every(0.1f, [this] { sum += 1; }, 3);
    self.SetTag("Hero");
    rm::Entity child = rm::Spawn("Child");
    child.SetParent(self, true);
    rm::Events::SendTo(self, "Hello", 2.5, "hi");
    const rm::Vec3 v = rm::Normalize(rm::Vec3{3, 0, 4}) * 10.0f;
    steps = static_cast<int>(std::lround(v.x + v.z)) + static_cast<int>(self.Children().size()) * 100 + (rm::FindWithTag("Hero") == self ? 1000 : 0);
    const rm::Quat q = rm::Quat::Euler({0, 90, 0});
    (void)q.ToEuler();
    (void)rm::Mouse::Position();
    (void)rm::Input::Axis("MoveX", 1);
    rm::SetTimeScale(1.0f);
    rm::SpriteAnimation(self).Play();
    rm::Particles(self).Emit(1);
    const rm::Animator anim(self);  // (アニメーターが無い: 何もしない)
    (void)anim.PlayClip("Run", 0.2f);
    (void)anim.BonePosition("Hand");
    (void)anim.Ended();
    rm::Transform3D(self).LookAt(rm::Vec3{0, 0, -1});
    rm::Transform2D(self).LookAt(rm::Vec2{1, 0});
}
void Waiter::OnEvent(rm::Entity, const rm::Event& e) {
    if (!e.Is("Hello")) return;
    ++events;
    sum += static_cast<float>(e.number);
    log += std::string(e.text);
}
)rm";
    }
    // 作って壊す、エンジンの Transform を足す、新しい種類の欄をすべて持つスクリプト。
    {
        std::ofstream h(ScriptsFolder(project) / L"Shooter.h", std::ios::binary);
        h << "#pragma once\n#include \"RM.h\"\n"
             "RM_ENUM() enum class Shot { RM_ITEM(Label=\"一発\") Single, Burst };\n"
             "RM_STRUCT() struct Pattern { RM_PROPERTY(Edit) int count = 2; RM_PROPERTY(Edit) std::vector<std::string> tags; };\n"
             "RM_SCRIPT()\nstruct Shooter {\n"
             "    RM_PROPERTY(Edit) Shot shot = Shot::Burst;\n"
             "    RM_PROPERTY(Edit) Pattern pattern;\n"
             "    RM_PROPERTY(Edit) std::vector<Pattern> extra;\n"
             "    RM_PROPERTY(Edit) std::vector<int> numbers;\n"
             "    RM_PROPERTY(Edit) rm::PrefabRef enemy;\n"
             "    RM_PROPERTY() int fired = 0;\n"
             "    void Tick(rm::Entity self, float dt);\n};\n";
        std::ofstream c(ScriptsFolder(project) / L"Shooter.cpp", std::ios::binary);
        c << "#include \"Shooter.h\"\n#include \"RM3D.h\"\n"
             "void Shooter::Tick(rm::Entity, float) {\n"
             "    if (fired > 0) return;\n"
             "    const int n = pattern.count + (shot == Shot::Burst ? 1 : 0);\n"
             "    for (int i = 0; i < n; ++i) {\n"
             "        rm::Entity b = rm::Spawn(\"Bullet\");\n"
             "        b.Add<rm::Transform3D>().SetPosition({static_cast<float>(i), 0, 0});\n"
             "        numbers.push_back(i);\n"
             "        ++fired;\n"
             "    }\n"
             "    pattern.tags.push_back(std::string(30, 's'));\n"
             "    extra.push_back(Pattern{5, {\"x\"}});\n"
             "    if (rm::Entity old = rm::Find(\"Old\"); old.Valid()) old.Destroy();\n"
             "    if (!enemy.Empty()) rm::Spawn(enemy);\n"
             "}\n";
    }

    ScriptBuildRequest req;
    req.globals = {{"score", GlobalType::Int, "10"},
                   {"speed", GlobalType::Float, "1.5"},
                   {"cleared", GlobalType::Bool, "false"},
                   {"player", GlobalType::String, "P"},
                   {"home", GlobalType::Vec3, ""}};
    req.project = project;
    req.engine = ExeFolder();  // rmgen.exe はテストの隣にビルドされる
    req.sdk = {Repo() / L"plugins/rm_engine/game_production/runtime/sdk", Repo() / L"plugins/rm_engine/game_x_3d/runtime/sdk",
               Repo() / L"plugins/rm_engine/game_x_2d/runtime/sdk", Repo() / L"plugins/rm_engine/game_x_audio/runtime/sdk",
               Repo() / L"plugins/rm_engine/game_x_particles/runtime/sdk", Repo() / L"plugins/rm_engine/game_x_animation/runtime/sdk"};
#ifdef _DEBUG
    req.debug = true;
#endif
    const ScriptBuildResult built = BuildScripts(req);
    for (const std::string& e : built.errors) std::printf("    %s\n", e.c_str());
    CHECK(built.ok && built.dll.filename() == L"Scripts_0001.dll");
    CHECK(fs::exists(ScriptSolution(project)) && fs::exists(ScriptBuildFolder(project) / L"Scripts.vcxproj"));
    if (!built.ok) return;
    {
        ScriptWorld world;
        world.parts = {game_x_3d::ScriptTransform3DPart(), game_x_3d::ScriptTransform3DPart2(), game_x_2d::ScriptTransform2DPart(),
                       game_x_2d::ScriptTransform2DPart2()};
        // → 押している (サンプルはキーではなく、既定の 入力 のアクション MoveX を読む)。
        world.readInput = [] {
            InputFrame f;
            f.keys[VK_RIGHT] = 1;
            return f;
        };
        const std::vector<InputAction> actions = DefaultInputActions();
        world.actions = &actions;
        world.globals.Reset(req.globals);
        world.saveFolder = ProjectSaveFolder(project);
        std::string told;
        world.log = [&](int, std::string_view text) { told += std::string(text) + "\n"; };
        const RmApi api = MakeScriptApi(world);
        ScriptModule module;
        std::string why;
        CHECK(module.Load(built.dll, api, &why));
        const ScriptType* mover = module.Find("ArrowMover");
        CHECK(mover && std::string(mover->desc.label) == "矢印キーで動く");
        if (!mover) return;
        {
            Scene scene;  // (モジュールより先に消える: スクリプトのコードは DLL の中にある)
            const reflect::TypeDesc& tf = reflect::TypeOf<space3d::Transform>();
            scene.SetComponentTypes({&tf, &mover->desc}, {&mover->desc});
            const int e = scene.Create("Player");
            scene.AddComponent(e, tf);
            scene.AddComponent(e, mover->desc);
            // その ジャンプの音 (rm::SoundRef) は、テンプレートの jump.wav で始まり、アセットの ID で保存される。
            CHECK(json::Write(scene.Save(), false).find("a7d0c3f1e2b54c6d8e9f0a1b2c3d4e51") != std::string::npos);
            ScriptRunner runner;
            runner.Frame(scene, world, module, {}, 0.5f);
            runner.Frame(scene, world, module, {}, 0.5f);
            const auto* t = static_cast<const space3d::Transform*>(scene.Find(e)->FindComponent(tf.name)->value.Get());
            CHECK(t->position.x > 2.99f && t->position.x < 3.01f);  // 速さ 3 で 1 秒、右へ
            runner.Stop(scene, world, module, {});
            CHECK(told.find("「Player」を矢印キー・WASD・ゲームパッドで動かせます") != std::string::npos);
            CHECK(told.find("2 コマ動きました") != std::string::npos);
            CHECK(runner.Crashed() == 0);
        }
        {
            // 作って壊す、エンジンの Transform を足す、新しい種類の欄 - 本物の DLL で。
            const ScriptType* shooter = module.Find("Shooter");
            CHECK(shooter != nullptr);
            if (shooter) {
                const reflect::TypeDesc& tf = reflect::TypeOf<space3d::Transform>();
                Scene src;
                src.SetComponentTypes({&tf}, {});
                const int enemyRoot = src.Create("Enemy");
                const json::Value enemyPrefab = MakePrefab(src, enemyRoot);
                const std::string guid = "0123456789abcdef0123456789abcdef";
                world.prefab = [&](const AssetRef& r) -> const json::Value* { return r.guid == guid ? &enemyPrefab : nullptr; };
                Scene scene;
                scene.SetComponentTypes({&tf, &shooter->desc}, {&shooter->desc});
                const int gun = scene.Create("Gun");
                scene.AddComponent(gun, shooter->desc);
                json::Value set = json::Value::MakeObject();
                set.Set("enemy", json::Value(guid));
                reflect::Load(shooter->desc, scene.Find(gun)->FindComponent("Shooter")->value.Get(), set, nullptr, nullptr);
                const int old = scene.Create("Old");
                scene.Create("Under the old", old);
                ScriptRunner runner;
                runner.Frame(scene, world, module, {}, 0.1f);
                std::vector<float> xs;
                for (const SceneEntity& e : scene.Entities()) {
                    if (e.data.name != "Bullet") continue;
                    const Component* t = e.FindComponent(tf.name);
                    if (t) xs.push_back(static_cast<const space3d::Transform*>(t->value.Get())->position.x);
                }
                CHECK((xs == std::vector<float>{0, 1, 2}));  // count 2 + Burst
                CHECK(!scene.Find(old) && scene.Entities().size() == 5);  // Gun、弾 3 つ、敵 (古いものとその子は消えた)
                CHECK(std::any_of(scene.Entities().begin(), scene.Entities().end(), [](const SceneEntity& e) { return e.data.name == "Enemy" && e.prefab.Root(); }));
                const json::Value data = reflect::Save(shooter->desc, scene.Find(gun)->FindComponent("Shooter")->value.Get());
                CHECK(data.Get("shot").AsString() == "Burst" && data.Get("numbers").Size() == 3 && data.Get("pattern").Get("count").AsInt() == 2);
                CHECK(data.Get("pattern").Get("tags").At(0).AsString() == std::string(30, 's') && data.Get("extra").At(0).Get("count").AsInt() == 5);
                CHECK(data.Get("enemy").AsString() == guid && data.Get("fired").AsInt() == 3);
                runner.Stop(scene, world, module, {});
                CHECK(runner.Crashed() == 0);
                world.prefab = nullptr;
            }
        }
        {
            // スクリプトからのグローバル変数: Global::score += 5、++、文字、ベクトル、bool を名前で。
            // rm::Save を Library/Saves に。
            const ScriptType* scorer = module.Find("Scorer");
            CHECK(scorer != nullptr);
            if (scorer) {
                Scene scene;
                scene.SetComponentTypes({&scorer->desc}, {&scorer->desc});
                scene.AddComponent(scene.Create("Game"), scorer->desc);
                ScriptRunner runner;
                runner.Frame(scene, world, module, {}, 0.1f);
                runner.Frame(scene, world, module, {}, 0.1f);
                runner.Stop(scene, world, module, {});
                CHECK(world.globals.Find("score")->v[0] == 22 && world.globals.Find("player")->s == "P!!");
                CHECK(world.globals.Find("home")->v[1] == 2 && world.globals.Find("cleared")->v[0] == 1);
                CHECK(world.globals.Find("speed")->v[0] == 6);
                CHECK(SaveExists(ProjectSaveFolder(project), "1"));
                CHECK(runner.Crashed() == 0);
            }
        }
        {
            // 本物の DLL での ABI 7: コルーチンは a, b (次のフレーム), c (0.25 秒後), d (イベントが来た) を
            // 動かした。タイマーは 1 回、0.1 秒ごとに 3 回。イベントは OnEvent に届いた。タグと親は設定された。
            // 数学。止めた後: コルーチンは残らない。
            const ScriptType* waiter = module.Find("Waiter");
            CHECK(waiter != nullptr && waiter->script->onEvent != nullptr);
            if (waiter) {
                Scene scene;
                scene.SetComponentTypes({&waiter->desc}, {&waiter->desc});
                const int e = scene.Create("Hero");
                scene.AddComponent(e, waiter->desc);
                ScriptRunner runner;
                for (int i = 0; i < 10; ++i) runner.Frame(scene, world, module, {}, 0.1f);
                const json::Value data = reflect::Save(waiter->desc, scene.Find(e)->FindComponent("Waiter")->value.Get());
                CHECK(data.Get("log").AsString() == "ahibcd");
                CHECK(data.Get("timers").AsInt() == 1 && data.Get("events").AsInt() == 1);
                CHECK(data.Get("sum").AsDouble() > 5.49 && data.Get("sum").AsDouble() < 5.51);
                CHECK(data.Get("steps").AsInt() == 1114);
                CHECK(scene.Find(e)->data.tag == "Hero");
                const std::vector<int> kids = scene.Children(e);
                CHECK(kids.size() == 1 && scene.Find(kids[0])->data.name == "Child");
                runner.Stop(scene, world, module, {});
                CHECK(world.coroutines.empty() && runner.Crashed() == 0);
            }
        }
        // もう一度ビルドする: 別の名前 (読み込んだものには上書きできない)。
        const ScriptBuildResult again = BuildScripts(req);
        CHECK(again.ok && again.dll.filename() == L"Scripts_0002.dll");
        CHECK(LatestScriptDll(ScriptDllFolder(project, req.debug)).filename() == L"Scripts_0002.dll");
        // 1 つのプロジェクトを同時に 2 回ビルドする (ビルド中に閉じたエディターを、また開いた): 片方は
        // もう片方を待つ - 番号は 2 つ、どちらもビルドされる。
        ScriptBuildResult first, second;
        std::thread a([&] { first = BuildScripts(req); });
        std::thread b([&] { second = BuildScripts(req); });
        a.join();
        b.join();
        CHECK(first.ok && second.ok && first.dll != second.dll);
    }
    fs::remove_all(project, ec);
}

RM_TEST(Process_HandsDownOnlyItsOwnPipe) {
    // このプロセスの別の継承できるパイプ (たとえば書き出しの): 子はそれを持ってはいけない。
    // 持つと、それを読んでいる側が子の終わりを待ってしまう。
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    CHECK(CreatePipe(&readEnd, &writeEnd, &sa, 0));
    std::thread child([] {
        std::string error;
        platform::RunProcess(L"cmd.exe /c ping -n 3 127.0.0.1", {}, {}, &error);  // 約 2 秒
    });
    Sleep(500);
    CloseHandle(writeEnd);  // こちらの唯一の書き込み側: 子も持っていない限り、パイプは壊れる
    DWORD avail = 0;
    const BOOL peek = PeekNamedPipe(readEnd, nullptr, 0, nullptr, &avail, nullptr);
    CHECK(!peek && GetLastError() == ERROR_BROKEN_PIPE);
    child.join();
    CloseHandle(readEnd);
}
