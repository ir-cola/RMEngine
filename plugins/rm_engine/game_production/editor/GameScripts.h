#pragma once
// The editor's side of a game's C++ scripts: building them (MSBuild on a worker thread), watching the
// sources, loading the newest DLL and handing it over at a frame boundary (hot reload), and the
// Visual Studio project. What loads and runs a DLL is the runtime's (runtime/Scripts.h): the same in
// an exported game.
//
//   saved a .cpp ──(1 s later)──▶ build on the worker ──▶ ok: Scripts_0008.dll ready
//        editing:  GameSoftware takes it at the next frame (TakeReady) and swaps (values by name)
//        playing:  kept until Stop ("Stop で反映されます"); Stop puts the scene back, then it is taken
//        failed:   the errors on the console; the DLL in use goes on

#include "editor/app/Worker.h"
#include "editor/plugin/Software.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace rm::game_production {

class GameScripts {
public:
    explicit GameScripts(editor::SoftwareHost& host);

    // Opening a project: the Visual Studio project written, the newest DLL built before loaded (so the
    // scene's scripts are known when it is read), a build started when the sources are newer than it.
    void Open();
    // A build now (again after the one running, when one is).
    void Build();
    // Opens Scripts.sln (Visual Studio), after this frame.
    void OpenVisualStudio();
    // Every frame: watches Assets/Scripts (once a second) and builds when something changed.
    void Poll();
    // A built DLL waiting to be used, given once - not while `playing` (it waits for Stop). An empty
    // path: the scripts are all gone (use none).
    std::optional<std::filesystem::path> TakeReady(bool playing);
    // Loads `dll` (empty: none) into a module of its own; the caller swaps it in with Swap once the
    // objects of the old one are gone. Null when it cannot be used (told on the console).
    std::unique_ptr<ScriptModule> LoadModule(const std::filesystem::path& dll);
    void Swap(std::unique_ptr<ScriptModule> next);

    const ScriptModule& Module() const { return *module_; }
    ScriptWorld& World() { return world_; }
    ScriptRunner& Runner() { return runner_; }
    bool Building() const { return building_; }
    bool WaitingForStop() const { return ready_.has_value(); }

private:
    void Finished(const struct ScriptBuildResult& r);

    editor::SoftwareHost& host_;
    std::unique_ptr<ScriptModule> module_;
    ScriptWorld world_;
    RmApi api_{};
    ScriptRunner runner_;
    editor::Worker worker_;
    bool building_ = false;
    bool again_ = false;
    bool toldNoVs_ = false;
    std::optional<std::filesystem::path> ready_;
    long long seenSources_ = -1;
    double lastLook_ = 0;
};

}  // namespace rm::game_production
