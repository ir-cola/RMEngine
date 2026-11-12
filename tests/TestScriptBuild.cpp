// The whole road with a real Visual Studio: the sample script of the template 「ゲーム」 (ArrowMover)
// built by MSBuild into a DLL (rmgen --scripts first), loaded by the runtime, attached to an entity and
// run: BeginPlay, Tick moving it with the right arrow key, EndPlay. Skipped (and said) on a PC without
// Visual Studio 2022. Also the text of the generated Visual Studio project and the error lines.

#include "Test.h"

#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_x_2d/runtime/ScriptTransform2D.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <windows.h>

#include <cstdio>
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

    ScriptBuildRequest req;
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
        world.keyDown = [](int vk) { return vk == VK_RIGHT; };
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
            CHECK(told.find("「Player」を矢印キーで動かせます") != std::string::npos);
            CHECK(told.find("2 コマ動きました") != std::string::npos);
            CHECK(runner.Crashed() == 0);
        }
        // Built again: another name (the loaded one cannot be written over).
        const ScriptBuildResult again = BuildScripts(req);
        CHECK(again.ok && again.dll.filename() == L"Scripts_0002.dll");
        CHECK(LatestScriptDll(ScriptBuildFolder(project)).filename() == L"Scripts_0002.dll");
    }
    fs::remove_all(project, ec);
}
