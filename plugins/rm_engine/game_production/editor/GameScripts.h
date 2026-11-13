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
#include "platform/Shell.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace rm::game_production {

// What the scripts of this project are built with (the editor's builds and the export's).
struct ScriptBuildRequest;
ScriptBuildRequest ScriptRequestFor(const editor::SoftwareHost& host);

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
    // The game settings' グローバル変数 (RmGlobals.h is written from them: Global::<name>). Set before
    // Open, and every frame: a change of the names or types writes the header again and builds - once
    // the list has been still for a second (typing a name is one build).
    void SetGlobals(const std::vector<GlobalVariable>& globals);
    // Every frame: builds when something in Assets/Scripts changed. A save is seen at once - even while
    // the editor is idle (the folder watch wakes it) - and built half a second later (a few saves in a
    // row are one build); it asks for frames until then.
    void Poll(ui::Context& c);
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
    ScriptBuildRequest Request() const;

    editor::SoftwareHost& host_;
    std::unique_ptr<ScriptModule> module_;
    InputReader input_;
public:
    // Whether the mouse is over the game view now (the game takes the mouse buttons only then).
    std::function<bool()> mouseInGame;

private:
    std::vector<GlobalVariable> globals_;
    double globalsChangedAt_ = -1;  // a change waiting to be written (-1: none)
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
    double changedAt_ = -1;  // when the watch last told of a change (-1: none waiting)
    editor::UiWake wake_{[this] { host_.Redraw(); }};
    platform::FolderWatch watch_;  // Assets/Scripts, file names and contents (after wake_: it calls it)
    bool watching_ = false;
    double lastAliveCheck_ = 0;
};

}  // namespace rm::game_production
