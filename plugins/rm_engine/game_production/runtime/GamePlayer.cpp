#include "rm_engine/game_production/runtime/GamePlayer.h"

#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "game/Player.h"
#include "platform/Win.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/GameDraw.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <unordered_map>

namespace rm::game_production {

namespace {

class GameFromProject final : public game::Game {
public:
    explicit GameFromProject(game::Host& host) : host_(host) {}

    bool Load(std::string* error) override {
        auto fail = [&](std::string msg) {
            if (error) *error = std::move(msg);
            return false;
        };
        std::string err;
        const auto text = host_.Read(kGameDataPath, &err);
        if (!text) return fail(err);
        json::ParseError pe;
        auto doc = json::Parse(*text, &pe);
        if (!doc || doc->Get("kind").AsString() != kGameDataKind) return fail("ゲームのデータ (game.json) が壊れています");
        if (doc->Get("format").AsInt(0) > kGameDataFormat) return fail("新しい版のエンジンで書き出したゲームです");
        reflect::Warnings warnings;
        reflect::Load(settings_, doc->Get("settings"), &unknowns_, &warnings);
        for (const auto& w : warnings) host_.Log(1, "ゲームの設定: " + w);
        if (const std::string& title = doc->Get("title").AsString(); !title.empty()) host_.SetTitle(title);
        for (const auto& [guid, path] : doc->Get("assets").Members()) assets_[guid] = path.AsString();

        // The component types the plugins offer (their runtime parts) - and the scripts' (Scripts.dll).
        std::vector<const reflect::TypeDesc*> types, multiple;
        for (const auto& c : host_.Registry().Contributions<ComponentInfo>(kComponentPoint)) {
            const reflect::TypeDesc* t = &c.item->type();
            if (std::find(types.begin(), types.end(), t) != types.end()) continue;
            types.push_back(t);
            if (c.item->multiple) multiple.push_back(t);
        }
        world_.parts = ScriptApiParts(host_.Registry());
        world_.log = [this](int level, std::string_view t) { host_.Log(level, t); };
        // The keys, the mouse buttons and the game pads: only while the game's window is in front (a key
        // typed into another app never moves the game - as the editor's Play); read by action name.
        world_.readInput = [this] { return input_.Read(); };
        world_.actions = &settings_.input;
        // rm::Spawn(prefab): the prefab files in the .rmpak (read once each).
        world_.prefab = [this](const AssetRef& ref) -> const json::Value* {
            if (const auto it = prefabs_.find(ref.guid); it != prefabs_.end()) return it->second ? &*it->second : nullptr;
            std::optional<json::Value> doc;
            if (const auto at = assets_.find(ref.guid); at != assets_.end()) {
                std::string err;
                if (const auto bytes = host_.Read(at->second, &err)) {
                    doc = json::Parse(*bytes);
                    if (doc && !IsPrefab(*doc)) doc.reset();
                }
                if (!doc) host_.Log(1, std::format("Prefab ({}) を読めません {}", at->second, err));
            }
            const std::optional<json::Value>& slot = prefabs_.emplace(ref.guid, std::move(doc)).first->second;
            return slot ? &*slot : nullptr;
        };
        // The global variables at their 初めの値 (the whole game: a scene switched keeps them); rm::Save
        // into %LOCALAPPDATA%\<exe name>\Saves.
        {
            std::vector<std::string> told;
            world_.globals.Reset(settings_.globals, &told);
            for (const auto& w : told) host_.Log(1, w);
            wchar_t exe[MAX_PATH] = {};
            const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
            world_.saveFolder = GameSaveFolder(std::filesystem::path(std::wstring(exe, n)).stem().native());
        }
        api_ = MakeScriptApi(world_);
        const std::filesystem::path dll = host_.Folder() / game::kScriptsDll;
        std::error_code ec;
        if (std::filesystem::exists(dll, ec)) {
            std::string why;
            if (scripts_.Load(dll, api_, &why)) {
                for (const reflect::TypeDesc* t : scripts_.Types()) {
                    types.push_back(t);
                    multiple.push_back(t);
                }
                host_.Log(0, std::format("スクリプト: {} 個", scripts_.Types().size()));
            } else {
                host_.Log(2, "スクリプト (Scripts.dll) を使えません: " + why);
            }
        }
        scene_.SetComponentTypes(std::move(types), std::move(multiple));

        // The game's scenes (the list's order: Game::LoadScene counts in it), and the first one.
        for (const auto& s : doc->Get("scenes").Items()) {
            scenePaths_.push_back(s.AsString());
            world_.sceneNames.push_back(SceneNameOf(s.AsString()));
        }
        const json::Value* firstKey = doc->Find("first");
        const size_t first = static_cast<size_t>(std::max<int64_t>(0, (firstKey ? *firstKey : doc->Get("open")).AsInt(0)));
        if (first < scenePaths_.size()) {
            if (!ReadScene(first, scene_, &err)) return fail(err);
            world_.sceneIndex = static_cast<int>(first);
        }
        host_.Log(0, std::format("シーン {} 個。最初のシーン: {}", scenePaths_.size(), first < scenePaths_.size() ? world_.sceneNames[first] : std::string("(なし)")));
        cameraTypes_ = CameraTypes(host_.Registry());

        // The systems, and the drawer of the game's space.
        runner_.SetSystems(RegisteredSystems(host_.Registry()));
        runner_.SetCamera(SceneCamera(scene_, cameraTypes_, settings_.startCamera.id));
        const GameDrawInfo* chosen = nullptr;
        for (const auto& d : host_.Registry().Contributions<GameDrawInfo>(kGameDrawPoint)) {
            if (!settings_.space.empty() && settings_.space == d.item->space) {
                chosen = d.item;
                break;
            }
            if (!chosen || d.item->order < chosen->order) chosen = d.item;
        }
        if (chosen && chosen->create) drawer_ = chosen->create();
        // The other spaces' drawers draw their layers over it (2D over a 3D game).
        for (const auto& d : host_.Registry().Contributions<GameDrawInfo>(kGameDrawPoint)) {
            if (d.item != chosen && d.item->create) overlays_.push_back(d.item->create());
        }
        if (!drawer_) host_.Log(1, "このゲームには描くプラグイン (3D / 2D) が入っていないので、背景の色だけを塗ります");
        host_.Log(0, std::format("システム {} 個・エンティティ {} 個", runner_.Systems().size(), scene_.Entities().size()));
        return true;
    }

    // The game settings' 画面 (幅 / 高さ / 表示のしかた / 垂直同期) and 目標 FPS.
    Display GetDisplay() const override {
        Display d;
        d.width = settings_.display.width;
        d.height = settings_.display.height;
        d.mode = settings_.display.mode == ScreenMode::Fullscreen   ? Display::Mode::Fullscreen
                 : settings_.display.mode == ScreenMode::Borderless ? Display::Mode::Borderless
                                                                    : Display::Mode::Windowed;
        d.vsync = settings_.display.vsync;
        d.targetFps = settings_.targetFps;
        return d;
    }

    void Update(double dt) override {
        // The scripts (they read the keys), then the systems and their fixed steps.
        if (scripts_.Loaded()) {
            world_.scene = &scene_;
            world_.dt = static_cast<float>(dt);
            world_.time = runner_.Time();
            world_.camera = runner_.Camera();
            scriptRunner_.Frame(scene_, world_, scripts_, settings_.scriptOrder, static_cast<float>(dt));
            runner_.SetCamera(world_.camera);
        }
        runner_.Advance(scene_, dt, settings_.fixedTimestep);
        // A scene the scripts asked for (Game::LoadScene): switched here, between frames.
        const std::string from = world_.sceneIndex >= 0 ? world_.sceneNames[static_cast<size_t>(world_.sceneIndex)] : std::string();
        std::string err;
        const SceneSwitch sw = SwitchAskedScene(
            scene_, world_, scriptRunner_, scripts_, settings_.scriptOrder,
            [this](size_t index, Scene& into, std::string* why) { return ReadScene(index, into, why); }, &err);
        if (sw == SceneSwitch::Switched) {
            runner_.SetCamera(SceneCamera(scene_, cameraTypes_, settings_.startCamera.id));
            host_.Log(0, std::format("シーンを切り替えました: {} → {} (エンティティ {} 個)", from, world_.sceneNames[static_cast<size_t>(world_.sceneIndex)],
                                     scene_.Entities().size()));
        } else if (sw == SceneSwitch::Failed) {
            host_.Log(2, "シーンを切り替えられません: " + err);
        }
    }

    void Draw(ID2D1DeviceContext* dc, float width, float height) override {
        if (!drawer_) {
            const Color c = settings_.clearColor;
            dc->Clear(D2D1::ColorF(c.r, c.g, c.b, 1));
            return;
        }
        GameDrawFrame f{scene_, runner_.Camera(), settings_.clearColor, host_.Graphics(), dc, width, height,
                        [this](const AssetRef& ref) -> std::optional<std::string> {
                            const auto it = assets_.find(ref.guid);
                            if (it == assets_.end()) return std::nullopt;
                            std::string err;
                            auto bytes = host_.Read(it->second, &err);
                            if (!bytes) host_.Log(1, err);
                            return bytes;
                        },
                        [this](std::string_view path) -> std::optional<std::string> {
                            std::string err;
                            auto bytes = host_.Read(path, &err);
                            if (!bytes) host_.Log(1, err);
                            return bytes;
                        }};
        drawer_->Draw(f);
        for (const auto& o : overlays_) o->DrawOverlay(f);
    }

    void Stop() override {
        if (scripts_.Loaded()) scriptRunner_.Stop(scene_, world_, scripts_, settings_.scriptOrder);
    }

private:
    // The scene at `index` of the list, read from the .rmpak into `into` (its component types set).
    bool ReadScene(size_t index, Scene& into, std::string* error) {
        if (index >= scenePaths_.size()) return false;
        const std::string& path = scenePaths_[index];
        std::string err;
        const auto bytes = host_.Read(path, &err);
        const auto scene = bytes ? ReadSceneFile(*bytes, &err) : std::nullopt;
        if (!scene) {
            if (error) *error = std::format("シーン ({}) を読めません: {}", path, err);
            return false;
        }
        reflect::Warnings warnings;
        into.Load(*scene, &warnings);
        for (const auto& w : warnings) host_.Log(1, "シーン: " + w);
        return true;
    }

    game::Host& host_;
    // The scripts' DLL first: members go in the reverse order, so the scene (holding the scripts'
    // objects, whose code is in the DLL) is gone before the DLL is let go.
    InputReader input_;
    ScriptWorld world_;
    RmApi api_{};
    ScriptModule scripts_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;
    Scene scene_;
    SystemRunner runner_;
    std::unordered_map<std::string, std::string> assets_;  // asset id -> its path in the .rmpak
    std::unordered_map<std::string, std::optional<json::Value>> prefabs_;  // rm::Spawn's prefabs, read (nullopt: not one)
    std::vector<std::string> scenePaths_;                   // the game's scenes, in the list's order
    std::vector<std::string> cameraTypes_;                  // what the game sees through (SceneCamera)
    std::unique_ptr<GameDrawer> drawer_;
    std::vector<std::unique_ptr<GameDrawer>> overlays_;
    ScriptRunner scriptRunner_;
};

std::unique_ptr<game::Game> CreateGame(game::Host& host) { return std::make_unique<GameFromProject>(host); }

// What the exported game registers of game production (its generated main calls it by the C name
// below): the game itself (the frame, game/Player.h, knows no software). The editor never has it.
void RegisterForGame(RuntimeRegistry& registry) {
    static const game::GameInfo kGame{&CreateGame};
    registry.Contribute(game::kGamePoint, kGame);
}

}  // namespace

}  // namespace rm::game_production

RM_RUNTIME_ENTRY(rm_engine, game_production, rm::game_production::RegisterForGame)
