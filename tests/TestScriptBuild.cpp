// The whole road with a real Visual Studio: the sample script of the template 「ゲーム」 (ArrowMover)
// built by MSBuild into a DLL (rmgen --scripts first), loaded by the runtime, attached to an entity and
// run: BeginPlay, Tick moving it with the right arrow key, EndPlay. Skipped (and said) on a PC without
// Visual Studio 2022. Also the text of the generated Visual Studio project and the error lines.

#include "Test.h"

#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "platform/Process.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <windows.h>

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
    // A script of the global variables and saves (Global::<name> from the header the build writes).
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

    ScriptBuildRequest req;
    req.globals = {{"score", GlobalType::Int, "10"},
                   {"speed", GlobalType::Float, "1.5"},
                   {"cleared", GlobalType::Bool, "false"},
                   {"player", GlobalType::String, "P"},
                   {"home", GlobalType::Vec3, ""}};
    req.project = project;
    req.engine = ExeFolder();  // rmgen.exe is built next to the tests
    req.sdk = {Repo() / L"plugins/rm_engine/game_production/runtime/sdk", Repo() / L"plugins/rm_engine/game_x_3d/runtime/sdk",
               Repo() / L"plugins/rm_engine/game_x_2d/runtime/sdk"};
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
        world.parts = {game_x_3d::ScriptTransform3DPart(), game_x_2d::ScriptTransform2DPart()};
        // → held (the sample reads the action MoveX of the default 入力, not the key).
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
            Scene scene;  // (gone before the module: its scripts' code is in the DLL)
            const reflect::TypeDesc& tf = reflect::TypeOf<space3d::Transform>();
            scene.SetComponentTypes({&tf, &mover->desc}, {&mover->desc});
            const int e = scene.Create("Player");
            scene.AddComponent(e, tf);
            scene.AddComponent(e, mover->desc);
            ScriptRunner runner;
            runner.Frame(scene, world, module, {}, 0.5f);
            runner.Frame(scene, world, module, {}, 0.5f);
            const auto* t = static_cast<const space3d::Transform*>(scene.Find(e)->FindComponent(tf.name)->value.Get());
            CHECK(t->position.x > 2.99f && t->position.x < 3.01f);  // speed 3 for one second, to the right
            runner.Stop(scene, world, module, {});
            CHECK(told.find("「Player」を矢印キー・WASD・ゲームパッドで動かせます") != std::string::npos);
            CHECK(told.find("2 コマ動きました") != std::string::npos);
            CHECK(runner.Crashed() == 0);
        }
        {
            // The global variables from a script: Global::score += 5, ++, a text, a vector, a bool, by
            // name; rm::Save into Library/Saves.
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
        // Built again: another name (the loaded one cannot be written over).
        const ScriptBuildResult again = BuildScripts(req);
        CHECK(again.ok && again.dll.filename() == L"Scripts_0002.dll");
        CHECK(LatestScriptDll(ScriptDllFolder(project, req.debug)).filename() == L"Scripts_0002.dll");
        // Two builds of one project at once (an editor closed while building, opened again): one waits
        // for the other - two numbers, both built.
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
    // Another inheritable pipe of this process (an export's, say): the child must not hold it, or the
    // one reading it would wait for the child's end.
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    CHECK(CreatePipe(&readEnd, &writeEnd, &sa, 0));
    std::thread child([] {
        std::string error;
        platform::RunProcess(L"cmd.exe /c ping -n 3 127.0.0.1", {}, {}, &error);  // about 2 seconds
    });
    Sleep(500);
    CloseHandle(writeEnd);  // our only write end: the pipe is broken unless the child has one too
    DWORD avail = 0;
    const BOOL peek = PeekNamedPipe(readEnd, nullptr, 0, nullptr, &avail, nullptr);
    CHECK(!peek && GetLastError() == ERROR_BROKEN_PIPE);
    child.join();
    CloseHandle(readEnd);
}
