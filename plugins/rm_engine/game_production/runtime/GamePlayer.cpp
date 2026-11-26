#include "rm_engine/game_production/runtime/GamePlayer.h"

#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "game/Player.h"
#include "platform/Win.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/GameDraw.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Register.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <unordered_map>
#include <utility>

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

        // プラグインが出す部品の型 (そのランタイム側) と、スクリプト (Scripts.dll) の型。
        std::vector<const reflect::TypeDesc*> types, multiple;
        for (const auto& c : host_.Registry().Contributions<ComponentInfo>(kComponentPoint)) {
            const reflect::TypeDesc* t = &c.item->type();
            if (std::find(types.begin(), types.end(), t) != types.end()) continue;
            types.push_back(t);
            if (c.item->multiple) multiple.push_back(t);
        }
        world_.parts = ScriptApiParts(host_.Registry());
        world_.log = [this](int level, std::string_view t) { host_.Log(level, t); };
        // キー、マウスのボタン、ゲームパッド: ゲームのウィンドウが前にあるときだけ (他のアプリに打った
        // キーでゲームが動くことはない - エディターの Play と同じ)。アクション名で読む。
        world_.readInput = [this] { return input_.Read(); };
        world_.actions = &settings_.input;
        world_.keyboardPlayer = std::clamp(settings_.keyboardPlayer - 1, 0, kPlayers - 1);
        world_.vibrate = [this](int player, float low, float high, float seconds) { input_.Vibrate(player, low, high, seconds); };
        world_.registry = &host_.Registry();
        world_.layers = &settings_.layers;
        // rm::Spawn(prefab): .rmpak の中の Prefab ファイル (それぞれ 1 回だけ読む)。
        world_.prefab = [this](const AssetRef& ref) -> const json::Value* {
            if (const auto it = prefabs_.find(ref.guid); it != prefabs_.end()) return it->second ? &*it->second : nullptr;
            std::optional<json::Value> doc;
            if (const auto at = assets_.find(ref.guid); at != assets_.end()) {
                std::string err;
                if (const auto bytes = host_.Read(at->second, &err)) {
                    doc = json::Parse(*bytes);
                    if (doc && !IsPrefab(*doc)) doc.reset();
                    if (doc) UpgradePrefab(*doc, host_.Registry());
                }
                if (!doc) host_.Log(1, std::format("Prefab ({}) を読めません {}", at->second, err));
            }
            const std::optional<json::Value>& slot = prefabs_.emplace(ref.guid, std::move(doc)).first->second;
            return slot ? &*slot : nullptr;
        };
        // グローバル変数は 初めの値 で (ゲーム全体: 切り替えたシーンも引き継ぐ)。rm::Save は
        // %LOCALAPPDATA%\<exe name>\Saves に書く。
        {
            std::vector<std::string> told;
            world_.globals.Reset(settings_.globals, &told);
            for (const auto& w : told) host_.Log(1, w);
            wchar_t exe[MAX_PATH] = {};
            const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
            world_.saveFolder = GameSaveFolder(std::filesystem::path(std::wstring(exe, n)).stem().native());
            // プレイヤー自身の割り当て (rm::Input::SaveBindings)。あるときだけ。
            std::string why;
            if (!world_.LoadBindings(&why) && !why.empty()) host_.Log(1, "入力の割り当て (input.json) を読めません: " + why);
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
        scene_.SetSettingTypes(SettingTypes(host_.Registry()));

        // ゲームのシーン (一覧の順: Game::LoadScene はこの中で数える) と、最初の 1 つ。
        for (const auto& s : doc->Get("scenes").Items()) {
            scenePaths_.push_back(s.AsString());
            world_.sceneNames.push_back(SceneNameOf(s.AsString()));
            // そのアセット id。ゲームが id で名前を指すとき (ゲーム UI のボタンの シーンを切り替える)。
            std::string id;
            for (const auto& [guid, path] : assets_) {
                if (path == s.AsString()) id = guid;
            }
            world_.sceneIds.push_back(std::move(id));
        }
        const json::Value* firstKey = doc->Find("first");
        const size_t first = static_cast<size_t>(std::max<int64_t>(0, (firstKey ? *firstKey : doc->Get("open")).AsInt(0)));
        if (first < scenePaths_.size()) {
            if (!ReadScene(first, scene_, &err)) return fail(err);
            world_.sceneIndex = static_cast<int>(first);
            world_.sceneBase = scene_.NextId();  // (世界のセーブ: これ以上の id はゲームの中で作った物)
        }
        host_.Log(0, std::format("シーン {} 個。最初のシーン: {}", scenePaths_.size(), first < scenePaths_.size() ? world_.sceneNames[first] : std::string("(なし)")));
        cameraTypes_ = CameraTypes(host_.Registry());

        // システムと、ゲームの空間の描画担当。
        runner_.SetSystems(RegisteredSystems(host_.Registry()));
        runner_.SetRegistry(&host_.Registry());
        // システムは、書き出しが入れたままの形で .rmpak からアセットを読む (メッシュコライダー: ベイクしたモデル)。
        runner_.SetAssets({[this](const AssetRef& ref) -> std::optional<std::string> {
                               const auto it = assets_.find(ref.guid);
                               if (it == assets_.end()) return std::nullopt;
                               std::string err;
                               auto bytes = host_.Read(it->second, &err);
                               if (!bytes) host_.Log(1, err);
                               return bytes;
                           },
                           nullptr});
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
        // 他の空間の描画担当が、その上に自分の層を描く (3D のゲームに重ねる 2D)。
        for (const auto& d : host_.Registry().Contributions<GameDrawInfo>(kGameDrawPoint)) {
            if (d.item != chosen && d.item->create) overlays_.push_back(d.item->create());
        }
        if (!drawer_) host_.Log(1, "このゲームには描くプラグイン (3D / 2D) が入っていないので、背景の色だけを塗ります");
        // 画面全体の上に、空間の後で、その順に描く (ゲーム UI)。
        std::vector<const ScreenDrawInfo*> screens;
        for (const auto& d : host_.Registry().Contributions<ScreenDrawInfo>(kScreenDrawPoint)) screens.push_back(d.item);
        std::stable_sort(screens.begin(), screens.end(), [](const ScreenDrawInfo* a, const ScreenDrawInfo* b) { return a->order < b->order; });
        for (const ScreenDrawInfo* s : screens) {
            if (s->create) screenDrawers_.push_back(s->create());
        }
        screen_ = {static_cast<float>(settings_.display.width), static_cast<float>(settings_.display.height)};
        host_.Log(0, std::format("システム {} 個・エンティティ {} 個", runner_.Systems().size(), scene_.Entities().size()));

        // ゲームが動いている間、プラグインが持っているもの (音: PlayServices.h) - ゲームが終わるまで。
        PlayHost play;
        play.registry = &host_.Registry();
        play.settings = &settings_;
        play.asset = [this](const AssetRef& ref) { return ReadAsset(ref); };
        play.name = [this](const AssetRef& ref) {
            const auto it = assets_.find(ref.guid);
            return it == assets_.end() ? std::string() : PathToUtf8(PathFromUtf8(it->second).filename());
        };
        play.log = [this](int level, std::string_view t) { host_.Log(level, t); };
        services_.Start(std::move(play));
        world_.services = &services_;
        return true;
    }

    // ゲーム設定の 画面 (幅 / 高さ / 表示のしかた / 垂直同期) と 目標 FPS。
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

    void Update(double realDt) override {
        // ゲームの時間: 実際の秒数にタイムスケール (rm::SetTimeScale) をかけたもの - スクリプト、システム、
        // パーティクル用。ゲーム UI は実時間で動き続ける (0 のときのポーズメニューも動く)。
        const double dt = realDt * world_.timeScale;
        movedFor_ += static_cast<float>(dt);
        world_.unscaledDt = static_cast<float>(realDt);
        world_.SetPointer(host_.Pointer(), screen_);
        // スクリプト (キーを読む)、それからシステムと、その固定ステップ。
        if (scripts_.Loaded()) {
            world_.scene = &scene_;
            world_.dt = static_cast<float>(dt);
            world_.time = runner_.Time();
            world_.camera = runner_.Camera();
            scriptRunner_.Frame(scene_, world_, scripts_, settings_.scriptOrder, static_cast<float>(dt));
            runner_.SetCamera(world_.camera);
        } else {
            world_.ReadInput();  // (ゲーム UI もこれを読む)
        }
        runner_.Advance(scene_, dt, settings_.fixedTimestep);
        QueueSystemEvents(world_, runner_.Events());  // (クリップのイベント ...: OnEvent へ)
        // システムの固定ステップがスクリプトに伝えること (物理の接触など)。
        if (scripts_.Loaded()) scriptRunner_.Deliver(scene_, world_, scripts_, settings_.scriptOrder, runner_.Hooks());
        // サービス (音、ゲーム UI など) - 画面、マウス、入力を渡す。サービスが求めるフック (ボタンの OnClick) は
        // 今はスクリプトに渡る。
        std::vector<HookCall> asked;
        PlayFrame pf{scene_, runner_.Camera(), static_cast<float>(realDt)};
        pf.screen = screen_;
        pf.pointer = host_.Pointer();
        const game::Host::PointerPresses presses = host_.TakePointerPresses();
        pf.pointerWentDown = presses.down > 0;
        pf.pointerWentUp = presses.up > 0;
        pf.input = &world_.inputNow;
        pf.inputBefore = &world_.inputBefore;
        pf.actions = &world_.Actions();  // (プレイヤーの割り当て)
        pf.hooks = &asked;
        pf.world = &world_;
        // 打たれた文字 (ゲーム UI の入力欄)。サービスが「打っている」と言えば、ウィンドウが IME をつなぐ。
        const ui::TypedText typed = host_.TakeTyped();
        pf.typed = &typed;
        TextInputAsk textAsk;
        pf.textInput = &textAsk;
        services_.Frame(pf);
        host_.SetTextInput(textAsk.want, textAsk.x, textAsk.y, textAsk.w, textAsk.h);
        if (scripts_.Loaded()) {
            scriptRunner_.Deliver(scene_, world_, scripts_, settings_.scriptOrder, asked);
        } else {
            world_.events.clear();  // (受け取るスクリプトが無い)
        }
        if (std::exchange(world_.quitAsked, false)) {
            host_.Log(0, "ゲームを終わります");
            host_.Quit();
        }
        // スクリプトが頼んだシーン (Game::LoadScene): ここで、フレームの合間に切り替える。
        const std::string from = world_.sceneIndex >= 0 ? world_.sceneNames[static_cast<size_t>(world_.sceneIndex)] : std::string();
        std::string err;
        const SceneSwitch sw = SwitchAskedScene(
            scene_, world_, scriptRunner_, scripts_, settings_.scriptOrder,
            [this](size_t index, Scene& into, std::string* why) { return ReadScene(index, into, why); }, &err);
        if (sw == SceneSwitch::Switched) {
            services_.SceneSwitched();
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
            DrawScreen(dc, width, height);
            return;
        }
        GameDrawFrame f{scene_, runner_.Camera(), settings_.clearColor, host_.Graphics(), dc, width, height,
                        [this](const AssetRef& ref) { return ReadAsset(ref); },
                        [this](std::string_view path) -> std::optional<std::string> {
                            std::string err;
                            auto bytes = host_.Read(path, &err);
                            if (!bytes) host_.Log(1, err);
                            return bytes;
                        }};
        f.registry = &host_.Registry();
        f.dt = std::exchange(movedFor_, 0.f);
        drawer_->Draw(f);
        for (const auto& o : overlays_) o->DrawOverlay(f);
        DrawScreen(dc, width, height);
    }

    // ゲーム UI など、すべての上に: ウィンドウがゲームの画面になる。
    void DrawScreen(ID2D1DeviceContext* dc, float width, float height) {
        screen_ = {width, height};
        if (screenDrawers_.empty()) return;
        ScreenDrawFrame f{scene_, settings_, host_.Graphics(), dc, 0, 0, width, height, screen_,
                          [this](const AssetRef& ref) { return ReadAsset(ref); }, true};
        for (const auto& s : screenDrawers_) s->Draw(f);
    }

    void Stop() override {
        if (scripts_.Loaded()) scriptRunner_.Stop(scene_, world_, scripts_, settings_.scriptOrder);
        input_.StopVibration();
        world_.services = nullptr;
        services_.Stop();  // (すべての音が止まる)
    }

private:
    // アセットのバイト列: 書き出しがそのために .rmpak に入れたもの (ベイクしたものか、ファイルそのもの)。
    std::optional<std::string> ReadAsset(const AssetRef& ref) {
        const auto it = assets_.find(ref.guid);
        if (it == assets_.end()) return std::nullopt;
        std::string err;
        auto bytes = host_.Read(it->second, &err);
        if (!bytes) host_.Log(1, err);
        return bytes;
    }

    // 一覧の `index` 番目のシーンを .rmpak から `into` に読む (部品の型を設定しておく)。
    bool ReadScene(size_t index, Scene& into, std::string* error) {
        if (index >= scenePaths_.size()) return false;
        const std::string& path = scenePaths_[index];
        std::string err;
        const auto bytes = host_.Read(path, &err);
        const auto scene = bytes ? ReadSceneFile(*bytes, &err, &host_.Registry()) : std::nullopt;
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
    // スクリプトの DLL が先: メンバーは逆の順に消えるので、シーン (スクリプトのオブジェクトを持ち、
    // そのコードは DLL の中にある) が、DLL を手放す前になくなる。
    InputReader input_;
    ScriptWorld world_;
    RmApi api_{};
    ScriptModule scripts_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;
    Scene scene_;
    SystemRunner runner_;
    PlayServices services_;
    std::unordered_map<std::string, std::string> assets_;  // アセット id -> .rmpak の中のパス
    std::unordered_map<std::string, std::optional<json::Value>> prefabs_;  // rm::Spawn の Prefab、読んだもの (nullopt: Prefab ではない)
    std::vector<std::string> scenePaths_;                   // ゲームのシーン、一覧の順
    std::vector<std::string> cameraTypes_;                  // ゲームが通して見るもの (SceneCamera)
    std::unique_ptr<GameDrawer> drawer_;
    std::vector<std::unique_ptr<GameDrawer>> overlays_;
    std::vector<std::unique_ptr<ScreenDrawer>> screenDrawers_;  // すべての上 (ゲーム UI)
    Vec2 screen_{1280, 720};                                    // 最後の Draw のときのウィンドウのサイズ (DIPs)
    float movedFor_ = 0;  // 最後の Draw から Update がゲームを進めた秒数 (GameDrawFrame::dt)
    ScriptRunner scriptRunner_;
};

std::unique_ptr<game::Game> CreateGame(game::Host& host) { return std::make_unique<GameFromProject>(host); }

// 書き出したゲームがゲーム制作について登録するもの (生成された main が、下の C の名前で呼ぶ):
// ゲーム自身 (枠の game/Player.h) はソフトを知らない。エディターにはこれはない。
void RegisterForGame(RuntimeRegistry& registry) {
    RegisterRuntime(registry);  // (エディターと同じ部品: セーブする ...)
    static const game::GameInfo kGame{&CreateGame};
    registry.Contribute(game::kGamePoint, kGame);
}

}  // namespace

}  // namespace rm::game_production

RM_RUNTIME_ENTRY(rm_engine, game_production, rm::game_production::RegisterForGame)
