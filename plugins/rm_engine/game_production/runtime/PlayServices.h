#pragma once
// Things that live while the game plays, offered by plugins - game production does not know what they
// do (音: the sound device and every sound playing - rm_engine/game_x_audio). Both hosts (the editor's
// Play, the exported game) run them the same way:
//
//   Play / the game starts     PlayServices::Start   every offered service made (PlayServiceInfo::create)
//   each frame                 Frame                 after the scripts and the systems (the scene as moved)
//   ⏸ / ▶                      SetPaused
//   the scene switched         SceneSwitched         (Game::LoadScene: between frames)
//   ■ / the game ends          Stop                  every service gone (a sound stops with it)
//
//   // in a plugin's runtime part (runtime/Register.cpp):
//   static const game_production::PlayServiceInfo kAudio{"rm_engine.audio", &CreateAudioService};
//   registry.Contribute(game_production::kPlayServicePoint, kAudio);
//
// Scripts reach a service through their API part (ScriptWorld::services->Find(name)).
//
// Where an entity is (its world position and which way it faces) is the space's business: the plugins
// that know a space offer a PlaceInfo (ゲーム制作 × 3D, × 2D) and a service asks EntityPose.

#include "base/Json.h"
#include "base/Math.h"
#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rm::game_production {

// What the host gives the services (it lives as long as they do).
struct PlayHost {
    const RuntimeRegistry* registry = nullptr;
    const GameSettings* settings = nullptr;  // the game settings (the editor's: edited while playing - read as they are)
    // The bytes of an asset (the editor: its file; the game: what the export put in for it - baked or
    // as its file); nullopt when it is not there.
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    // Its import settings (the .meta's "import": the editor). The game gives none (null): what an asset
    // needs of them was baked into it.
    std::function<json::Value(const AssetRef&)> import;
    // Its file's name, for messages ("jump.wav"; empty: not known).
    std::function<std::string(const AssetRef&)> name;
    // A message for the console (level 0 info / 1 warning / 2 error; Japanese).
    std::function<void(int level, std::string_view text)> log;
    bool silent = false;  // nothing is heard (the tests): a service opens no device
};

// One frame, after the scripts and the systems.
struct PlayFrame {
    Scene& scene;
    uint64_t camera = 0;  // the entity the game sees through (SystemContext::camera)
    float dt = 0;
    // The game's screen (DIPs: the exported game's window; the editor's game view as if it were the game
    // settings' 画面) and the mouse on it (nullopt: not over it - another panel, outside the window).
    Vec2 screen{1280, 720};
    std::optional<Vec2> pointer;
    // The input of this frame and the last (pressed / released compare them; the mouse buttons too),
    // and the game settings' 入力. Null in a test that gives none: nothing is down.
    const InputFrame* input = nullptr;
    const InputFrame* inputBefore = nullptr;
    const std::vector<InputAction>* actions = nullptr;
    // Script hooks a service asks for (a game UI's OnClick): the host gives them to the scripts after
    // every service's Frame (ScriptRunner::Deliver). Null: none can be asked.
    std::vector<HookCall>* hooks = nullptr;
};

class PlayService {
public:
    virtual ~PlayService() = default;  // Stop: everything it does ends here
    virtual void Frame(const PlayFrame& f) { (void)f; }
    virtual void SetPaused(bool paused) { (void)paused; }
    // Game::LoadScene switched the scene (its entities are other ones now - the same ids may come again).
    virtual void SceneSwitched() {}
};

// RuntimeRegistry point: items are PlayServiceInfo (static).
inline constexpr char kPlayServicePoint[] = "rm_engine.game_production.play_service";
struct PlayServiceInfo {
    const char* name = "";  // how scripts' API parts find it ("rm_engine.audio")
    std::unique_ptr<PlayService> (*create)(const PlayHost& host) = nullptr;
};

// The services of one Play (of the game).
class PlayServices {
public:
    PlayServices() = default;
    PlayServices(const PlayServices&) = delete;
    PlayServices& operator=(const PlayServices&) = delete;
    ~PlayServices() { Stop(); }

    // Every service the plugins offered, made for `host` (kept here: the services hold on to it).
    void Start(PlayHost host);
    void Frame(const PlayFrame& f);
    void SetPaused(bool paused);
    void SceneSwitched();
    // Every service gone, the last made first.
    void Stop();
    bool Running() const { return host_ != nullptr; }
    PlayService* Find(std::string_view name) const;

private:
    std::unique_ptr<PlayHost> host_;
    std::vector<std::pair<std::string, std::unique_ptr<PlayService>>> list_;
};

// --- Where an entity is ---------------------------------------------------------------------------
// In the world, right-handed, Y up (a 2D game: x y on the screen, z = 0; facing into it).
struct Pose {
    Vec3 position{0, 0, 0};
    Vec3 forward{0, 0, -1};  // where it looks (a camera)
    Vec3 up{0, 1, 0};
};

// RuntimeRegistry point: items are PlaceInfo (static).
inline constexpr char kPlacePoint[] = "rm_engine.game_production.place";
struct PlaceInfo {
    const char* space = "";  // as SceneViewInfo::space ("3d", "2d")
    // False when the entity has nothing of this space (no Transform).
    bool (*pose)(const Scene& scene, int entity, Pose& out) = nullptr;
};

// The first space that knows where `entity` is; false when none does (no such entity, no Transform).
bool EntityPose(const RuntimeRegistry& registry, const Scene& scene, int entity, Pose& out);

}  // namespace rm::game_production
