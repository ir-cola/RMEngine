#include "rm_engine/game_production/editor/GameScripts.h"

#include "base/Log.h"
#include "editor/lang/Lang.h"
#include "base/Utf.h"
#include "editor/project/Project.h"
#include "platform/Shell.h"
#include "platform/Win.h"
#include "rm_engine/game_production/editor/ScriptBuild.h"

#include <shellapi.h>

#include <system_error>

namespace rm::game_production {

ScriptBuildRequest ScriptRequestFor(const editor::SoftwareHost& host) {
    ScriptBuildRequest r;
    r.project = host.GetProject().Root();
    r.engine = platform::ExeDirectory();
    r.sdk = {r.engine / L"Sdk"};
#ifdef _DEBUG
    r.debug = true;  // the same C runtime as this engine (/MDd)
#endif
    return r;
}

namespace {

ScriptBuildRequest RequestFor(const editor::SoftwareHost& host) { return ScriptRequestFor(host); }

// The keyboard of this editor: only while one of its windows is in front (a key pressed in another
// app never moves the game).
bool KeyDown(int vk) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

}  // namespace

GameScripts::GameScripts(editor::SoftwareHost& host) : host_(host), module_(std::make_unique<ScriptModule>()) {
    world_.parts = ScriptApiParts(host.Registry());
    world_.keyDown = &KeyDown;
    world_.log = [](int level, std::string_view text) {
        if (level >= 2) {
            log::Error("スクリプト: {}", text);
        } else if (level == 1) {
            log::Warn("スクリプト: {}", text);
        } else {
            log::Info("スクリプト: {}", text);
        }
    };
    api_ = MakeScriptApi(world_);
}

void GameScripts::Open() {
    const ScriptBuildRequest r = RequestFor(host_);
    std::string why;
    if (!WriteScriptProject(r, &why)) log::Warn("スクリプトの Visual Studio のプロジェクト: {}", why);
    const long long sources = ScriptSourcesTime(r.project);
    seenSources_ = sources;
    lastLook_ = platform::NowSeconds();
    const std::filesystem::path dll = LatestScriptDll(ScriptDllFolder(r.project, r.debug));
    if (sources != 0 && !dll.empty()) {
        if (auto m = LoadModule(dll)) Swap(std::move(m));
    }
    if (sources == 0) return;
    // Built before what was written since (or never built): build now.
    std::error_code ec;
    const auto built = dll.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(dll, ec);
    if (dll.empty() || ec || built.time_since_epoch().count() < sources - 1000) Build();
}

void GameScripts::Build() {
    if (building_) {
        again_ = true;
        return;
    }
    building_ = true;
    again_ = false;
    log::Info("スクリプトをビルドしています…");
    const ScriptBuildRequest r = RequestFor(host_);
    worker_.Run([this, r](std::stop_token, const editor::Worker::Post& post) {
        ScriptBuildResult result = BuildScripts(r);
        post([this, result = std::move(result)] { Finished(result); });
    });
}

void GameScripts::Finished(const ScriptBuildResult& r) {
    building_ = false;
    if (r.noVisualStudio) {
        if (!toldNoVs_) log::Warn("{}", r.summary);
        toldNoVs_ = true;
    } else if (r.nothingToBuild) {
        if (module_->Loaded()) {
            log::Info("{}", r.summary);
            ready_ = std::filesystem::path();  // every script is gone: use none
        }
    } else if (r.ok) {
        log::Info("{}", r.summary);
        ready_ = r.dll;
    } else {
        for (const std::string& e : r.errors) log::Error("{}", e);
        log::Error("{}", r.summary);
    }
    host_.Redraw();
    if (again_) Build();
}

void GameScripts::OpenVisualStudio() {
    const std::filesystem::path sln = ScriptSolution(host_.GetProject().Root());
    std::string why;
    if (!WriteScriptProject(RequestFor(host_), &why)) {
        log::Error("Visual Studio のプロジェクトを書けませんでした: {}", why);
        return;
    }
    // After this frame (opening another program is not done while drawing).
    editor::PostToUi([sln] {
        const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", sln.c_str(), nullptr, sln.parent_path().c_str(), SW_SHOWNORMAL));
        if (r <= 32) log::Error("Visual Studio で開けませんでした ({})。Visual Studio 2022 が入っているか確かめてください", PathToUtf8(sln));
    });
}

void GameScripts::Poll(ui::Context& c) {
    const double now = platform::NowSeconds();
    const std::filesystem::path root = host_.GetProject().Root();
    if (now - lastAliveCheck_ >= 1.0) {
        // (Assets/Scripts may come later - a first script made in Explorer - or be deleted and made
        // again: the watch follows the old folder, so it is started again on the new one)
        lastAliveCheck_ = now;
        if (watching_ && watch_.Lost()) {
            watching_ = false;
            changedAt_ = now;  // (what is there now is looked at)
        }
        std::error_code ec;
        const std::filesystem::path folder = ScriptsFolder(root);
        if (!watching_ && std::filesystem::is_directory(folder, ec)) watching_ = watch_.Start(folder, true, wake_.Signal());
    }
    if (watching_) {
        if (watch_.Changed()) {
            while (watch_.Changed()) {
            }
            changedAt_ = now;
        }
        if (changedAt_ < 0) return;
        if (now - changedAt_ < 0.5) {
            c.RequestAnimation();  // (the next frames come without input until it is looked at)
            return;
        }
        changedAt_ = -1;
    } else if (now - lastLook_ < 1.0) {
        return;  // (no watch: looked at once a second while frames come)
    }
    lastLook_ = now;
    const long long sources = ScriptSourcesTime(root);
    if (sources == seenSources_) return;
    seenSources_ = sources;
    Build();
}

std::optional<std::filesystem::path> GameScripts::TakeReady(bool playing) {
    if (playing || !ready_) return std::nullopt;
    auto r = std::move(ready_);
    ready_.reset();
    return r;
}

std::unique_ptr<ScriptModule> GameScripts::LoadModule(const std::filesystem::path& dll) {
    auto m = std::make_unique<ScriptModule>();
    if (dll.empty()) return m;
    std::string why;
    if (!m->Load(dll, api_, &why)) {
        log::Error("スクリプト: {}", why);
        return nullptr;
    }
    log::Info("スクリプトを読みました ({} 個: {})", m->Types().size(), PathToUtf8(dll.filename()));
    return m;
}

void GameScripts::Swap(std::unique_ptr<ScriptModule> next) {
    runner_.Reset();
    module_ = std::move(next);  // the old DLL goes now: nothing of its types may be left
}

}  // namespace rm::game_production
