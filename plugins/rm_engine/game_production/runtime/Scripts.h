#pragma once
// C++ scripts (the moves of a game): loaded from a DLL, seen by the engine as reflected types, run
// while the game plays. The RUNTIME part: the exported game uses the same code as the editor's Play
// (design TestEngine NewEngineBluePrint 5 / 6 / 6-b / 20-c, RME_PLATFORM 4-b).
//
//   host (editor / game)                       this file
//     ScriptModule::Load(dll, api)          ─▶  LoadLibrary, RmModuleInit, the ABI checked, every
//                                                script made a reflect::TypeDesc (MakeScriptTypes)
//     scene.SetComponentTypes(... + Types())     scripts are components of entities: the inspector,
//                                                saving, undo and copying work on them as on any
//     ScriptRunner::Frame(scene, world, dt)  ─▶  BeginPlay of the new ones, then Tick, type by type
//     ScriptRunner::Stop(scene, world)       ─▶  EndPlay
//
// Which DLL is the host's business: the editor reads the newest built one of <project>/Library/
// Scripts/ (LatestScriptDll), an exported game the Scripts.dll next to its exe.
//
// Every hook runs inside __try / __except (ScriptGuard.cpp): a script that crashes is stopped alone,
// once told on the console; the host goes on.

#include "base/Json.h"
#include "core/RuntimeRegistry.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/DebugDraw.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rm::game_production {

// --- The plugins' parts of the API (RmApi::part) ------------------------------------------------
// RuntimeRegistry point: items are ScriptApiPart (static). The connectors offer what knows a space:
// "ゲーム制作 × 3D" Transform in 3D (RM3D.h), "ゲーム制作 × 2D" in 2D (RM2D.h).
inline constexpr char kScriptApiPoint[] = "rm_engine.game_production.script_api";
struct ScriptApiPart {
    const char* name;  // e.g. "rm_engine.game_x_3d.transform"
    uint32_t version;
    const void* table;  // the part's C struct of functions; they get RmApi::ctx = ScriptWorld*
};

// What the scripts see while they run: RmApi::ctx points at this.
struct ScriptWorld {
    Scene* scene = nullptr;  // the scene that plays (the copy, in the editor)
    float dt = 0;
    double time = 0;
    uint64_t camera = 0;  // SystemContext::camera: the game's camera, switched by scripts too
    // The host's keyboard (a Windows virtual-key code: held down now).
    std::function<bool(int vk)> keyDown;
    // A script's message (level 0 info / 1 warning / 2 error).
    std::function<void(int level, std::string_view text)> log;
    // The plugins' parts of the API (what they offered at kScriptApiPoint: ScriptApiParts).
    std::vector<ScriptApiPart> parts;
    // Where rm::Debug's lines go: the editor's buffer while it plays (DebugDraw.h). Null - an exported
    // game - and the debug parts do nothing.
    DebugDraw* debug = nullptr;
    std::array<uint8_t, 256> keysNow{}, keysBefore{};
    // The game's scenes (Game::LoadScene): their names in the list's order, the one playing (-1: not
    // one of them) and the one asked for this frame (-1: none; switched between frames: SwitchAskedScene).
    std::vector<std::string> sceneNames;
    int sceneIndex = -1;
    int sceneAsked = -1;

    // Reads the keyboard once a frame (pressed / released compare with the last frame).
    void ReadKeys();
};

// The engine's table for a world: RmApi::ctx is the world (it must live as long as the table is used).
RmApi MakeScriptApi(ScriptWorld& world);
const void* FindScriptApiPart(std::span<const ScriptApiPart> parts, const char* name, uint32_t version);
// What the plugins offered (RuntimeRegistry: the editor's PluginRegistry, or the game's).
std::vector<ScriptApiPart> ScriptApiParts(const RuntimeRegistry& registry);

// --- The scripts of a module as reflected types (pure: tests feed a module built by hand) --------
struct ScriptType {
    reflect::TypeDesc desc{};
    std::vector<reflect::FieldDesc> fields;
    const RmScriptDesc* script = nullptr;  // its hooks (in the DLL)
    reflect::StringAccess strings{};        // its std::string fields: through the DLL (RmModule::stringGet / stringSet)
};
// This engine is a Debug build (its scripts must be too: RmModuleFlag_Debug).
#ifdef _DEBUG
inline constexpr bool kEngineDebug = true;
#else
inline constexpr bool kEngineDebug = false;
#endif
// Why a module cannot be used (empty = fine): another ABI, built as Debug for a Release engine (or the
// other way), no string functions, a type too strictly aligned, a field of a kind this version does
// not know, two scripts of one name.
std::string CheckModule(const RmModule* module, uint32_t engineAbi = RM_ABI_VERSION, bool engineDebug = kEngineDebug);
std::vector<std::unique_ptr<ScriptType>> MakeScriptTypes(const RmModule& module);

// --- A loaded script DLL --------------------------------------------------------------------------
class ScriptModule {
public:
    ScriptModule() = default;
    ScriptModule(const ScriptModule&) = delete;
    ScriptModule& operator=(const ScriptModule&) = delete;
    ~ScriptModule() { Unload(); }

    // Loads `dll` and reads its scripts. False with the reason (Japanese) when it cannot be used.
    bool Load(const std::filesystem::path& dll, const RmApi& api, std::string* why);
    // Every object of these types must be gone first (their code is in the DLL).
    void Unload();
    bool Loaded() const { return module_ != nullptr; }
    const std::filesystem::path& Path() const { return path_; }
    std::vector<const reflect::TypeDesc*> Types() const;
    const ScriptType* Find(std::string_view name) const;

    // For tests: a module that is not a DLL (made by hand).
    void Adopt(const RmModule& module);

private:
    void* library_ = nullptr;  // HMODULE
    const RmModule* module_ = nullptr;
    std::filesystem::path path_;
    std::vector<std::unique_ptr<ScriptType>> types_;
};

// The newest DLL the editor built in `folder` (<project>/Library/Scripts): "Scripts_<number>.dll"
// with the largest number; empty when there is none.
std::filesystem::path LatestScriptDll(const std::filesystem::path& folder);
// "Scripts_0007.dll" -> 7 (-1: not that form).
int ScriptDllNumber(std::wstring_view fileName);

// --- The order scripts run in -----------------------------------------------------------------------
// The number of a type in the game settings' 実行順 (0 when it is not listed).
int ScriptOrderOf(std::string_view type, std::span<const ScriptOrderEntry> order);
// Types by (number, name): the listed ones before / after the middle bucket of the others (0, by name).
std::vector<std::string> SortScriptTypes(std::vector<std::string> types, std::span<const ScriptOrderEntry> order);

// --- Running them -------------------------------------------------------------------------------
class ScriptRunner {
public:
    // One frame: the keyboard read; EndPlay of the started scripts switched off since (their entity or a
    // parent made not 有効); BeginPlay of every running script not started yet (new, or switched on
    // again); then Tick of every running one - type by type in the run order; inside a type by entity
    // id, then in the order they are attached. BeginPlay and EndPlay always come in pairs (a script
    // whose entity was deleted is gone with it). A script that crashed is not run again.
    void Frame(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order, float dt);
    // EndPlay of every started script (Stop), then all forgotten - and the world no longer points at the
    // scene (it is about to go).
    void Stop(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    void Reset();
    size_t Crashed() const { return crashed_.size(); }

private:
    struct Instance {
        int entity;
        const ScriptType* type;
        void* data;
        bool active;  // it and every parent 有効
    };
    // Every script of the scene (the switched-off ones too), in the run order.
    std::vector<Instance> Collect(Scene& scene, const ScriptModule& module, std::span<const ScriptOrderEntry> order) const;
    // Runs a hook guarded; false (and the script stopped, told once) when it crashed.
    bool Call(ScriptWorld& world, const Instance& in, int hook, float dt);

    std::unordered_set<void*> started_;
    std::unordered_set<void*> crashed_;
};

// --- Switching the scene (Game::LoadScene) -------------------------------------------------------
enum class SceneSwitch { None, Switched, Failed };
// Between frames: the scene the scripts asked for during the frame just played. `load` reads it (its
// place in world.sceneNames) into the scene given (its component types already set); then EndPlay of
// every started script of `playing`, which becomes the new scene - its scripts get BeginPlay at the next
// Frame. None: nothing was asked. Failed (*error why): `playing` goes on as it was.
SceneSwitch SwitchAskedScene(Scene& playing, ScriptWorld& world, ScriptRunner& runner, const ScriptModule& module,
                             std::span<const ScriptOrderEntry> order, const std::function<bool(size_t index, Scene& into, std::string* error)>& load,
                             std::string* error);

// The hooks, called inside __try / __except (ScriptGuard.cpp). 0 = fine, else the exception code.
// hook: 0 = BeginPlay, 1 = Tick, 2 = EndPlay.
unsigned long CallScriptHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt);

}  // namespace rm::game_production
