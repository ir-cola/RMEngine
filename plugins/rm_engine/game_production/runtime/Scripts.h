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
#include "rm_engine/game_production/runtime/Globals.h"
#include "rm_engine/game_production/runtime/Input.h"
#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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

// A coroutine of a script (rm::Task: its frame is the DLL's) and when it runs on (RmCoroutinesApi).
struct CoroutineSlot {
    uint64_t id = 0;
    void* frame = nullptr;
    int (*resume)(void* frame, RmWait* next) = nullptr;
    void (*destroy)(void* frame) = nullptr;
    const void* owner = nullptr;  // the script that started it (null: none - stopped at the game's end only)
    int ownerEntity = 0;
    RmWait wait{};
    uint64_t waitFrom = 0;  // the frame its wait began in (a wait ends in a later frame at the earliest)
    double until = 0;       // RmWait_Seconds: game time; RmWait_RealSeconds: real time
    bool dead = false;      // ended or stopped (taken out after the pass)
    bool running = false;   // inside its resume now (stopping it then frees it after it returns)
};

// An event sent by a script (rm::Events), delivered after the frame's Ticks (ScriptRunner::DeliverEvents).
struct QueuedEvent {
    std::string name;
    int sender = 0;
    double number = 0;
    std::string text;
    int entity = 0;
    uint32_t scope = 0;  // 0 every entity, 1 the target only, 2 the target and what is below it
    int target = 0;
};

// What the scripts see while they run: RmApi::ctx points at this.
struct ScriptWorld {
    Scene* scene = nullptr;  // the scene that plays (the copy, in the editor)
    float dt = 0;            // this frame's seconds, the time scale applied (Tick's dt)
    double time = 0;         // game seconds before this frame (the time scale applied)
    // The time scale (rm::SetTimeScale): the host multiplies its frame's real seconds by it for the scripts,
    // the systems and what draws (particles). The real ones: unscaledDt / unscaledTime. frame: frames before
    // this one.
    float timeScale = 1;
    float unscaledDt = 0;
    double unscaledTime = 0;
    uint64_t frame = 0;
    // The mouse on the game screen (the game settings' 画面, DIPs): the host sets it each frame
    // (SetPointer) before the scripts run. nullopt: not over the game.
    std::optional<Vec2> pointer;
    Vec2 lastPointer{0, 0};
    Vec2 pointerDelta{0, 0};
    Vec2 screen{1280, 720};
    // The plugins (Entity::SetParent keeping the world place asks the spaces: PlaceInfo::keepWorld). Null: none.
    const RuntimeRegistry* registry = nullptr;
    // The game settings' layer names (rm::LayerIndex: 1, 2 ... in this order); null: none.
    const std::vector<std::string>* layers = nullptr;
    uint64_t camera = 0;  // SystemContext::camera: the game's camera, switched by scripts too
    // The host's input, read once a frame (both hosts: an InputReader - only while one of their windows
    // is in front; tests give their own). Empty: nothing is ever down.
    std::function<InputFrame()> readInput;
    // The game settings' 入力 (the host's GameSettings::input; it lives as long as the world is used).
    const std::vector<InputAction>* actions = nullptr;
    // ... as the player changed them (rm::Input::SetBinding, read from input.json); nullopt: the settings'.
    std::optional<std::vector<InputAction>> playerActions;
    // Who has the keyboard and the mouse (0 = 1P: the game settings' キーボードとマウスのプレイヤー - 1).
    int keyboardPlayer = 0;
    // Shakes a player's pad (the host's InputReader::Vibrate); empty: nothing shakes.
    std::function<void(int player, float low, float high, float seconds)> vibrate;
    // A script's message (level 0 info / 1 warning / 2 error).
    std::function<void(int level, std::string_view text)> log;
    // The plugins' parts of the API (what they offered at kScriptApiPoint: ScriptApiParts).
    std::vector<ScriptApiPart> parts;
    // Where rm::Debug's lines go: the editor's buffer while it plays (DebugDraw.h). Null - an exported
    // game - and the debug parts do nothing.
    DebugDraw* debug = nullptr;
    // The game's scenes (Game::LoadScene): their names in the list's order, the one playing (-1: not
    // one of them) and the one asked for this frame (-1: none; switched between frames: SwitchAskedScene).
    std::vector<std::string> sceneNames;
    // Their asset ids, in the same order ("" where not known): a scene chosen as an asset (a game UI
    // button's シーンを切り替える) is found by its id - two scenes of one name in two folders are two.
    std::vector<std::string> sceneIds;
    int sceneIndex = -1;
    int sceneAsked = -1;
    // The game asked to end (a game UI button's ゲームを終わる): the host ends it after this frame (the
    // exported game closes its window; the editor's Play stops, as ■).
    bool quitAsked = false;
    InputFrame inputNow, inputBefore;
    std::unordered_set<std::string> told;  // names asked for that are not there (told once each)
    // The game's global variables (one store for the whole game: switching the scene keeps them; the
    // host Resets them from the settings at Play / Stop / when the game starts) and where rm::Save
    // writes them (empty: rm::Save does nothing).
    GlobalStore globals;
    std::filesystem::path saveFolder;
    // rm::Spawn(prefab): the prefab files (the host's: the editor's Assets, the game's .rmpak). Empty:
    // spawning a prefab gives nothing.
    PrefabReader prefab;
    // What plugins keep while the game plays (音 ...: PlayServices.h) - the host's; null when none
    // (a test): the parts that use one do nothing.
    PlayServices* services = nullptr;
    // Asked during the frame, done at its end (ScriptRunner::Frame): EndPlay first, then away.
    std::vector<int> destroyAsked;                            // entities (with what is below them)
    std::vector<std::pair<int, const void*>> removeAsked;     // components: their entity and object
    // The coroutines (rm::StartCoroutine) and the events sent this frame (rm::Events).
    std::vector<CoroutineSlot> coroutines;
    uint64_t nextCoroutine = 1;
    bool inCoroutinePass = false;
    std::vector<QueuedEvent> events;
    // The script whose hook (or coroutine) runs now, and its entity (null / 0: none) - what a coroutine
    // started now belongs to and what an event sent now comes from.
    const void* currentScript = nullptr;
    int currentEntity = 0;

    // Reads the input once a frame (pressed / released compare with the last frame).
    void ReadInput();
    // The mouse for this frame (the host's: over the game screen or not) and the screen's size.
    void SetPointer(std::optional<Vec2> at, Vec2 screenSize);
    // The input actions in use: the player's (playerActions) or the game settings'.
    const std::vector<InputAction>& Actions() const;
    // The player's bindings from <saveFolder>/input.json (nothing there: the settings'); false and *why
    // when a file is there and cannot be read. Save writes them.
    bool LoadBindings(std::string* why = nullptr);
    bool SaveBindings(std::string* why = nullptr);
    std::filesystem::path BindingsFile() const;
    // Coroutines: start one (runs it to its first wait now; its id, 0 when it ended at once), stop one /
    // every one of a script / all of them (before the scripts' DLL is let go). ResumeCoroutines: after the
    // Ticks, every coroutine whose wait is over runs on.
    uint64_t StartCoroutine(void* frame, int (*resume)(void*, RmWait*), void (*destroy)(void*));
    void StopCoroutine(uint64_t id);
    void StopCoroutinesOf(const void* owner);
    void ClearCoroutines();
    void ResumeCoroutines();
    bool CoroutineRunning(uint64_t id) const;
    // An action of the game settings by name; null (and told once) when there is none.
    const InputAction* Action(const char* name);
    // A global variable by name; null (and told once) when there is none.
    GlobalValue* Global(const char* name);
    // Tells a script's mistake once (a warning on the console): `key` says which.
    void TellOnce(const std::string& key, std::string_view text);
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
    reflect::StringAccess assets{};         // its asset fields (char guid[RM_ASSET_ID_SIZE]: plain bytes)
    // What its fields need, owned here (their addresses stay): the RM_STRUCTs, the RM_ENUMs and the
    // lists (worked by the DLL's functions).
    struct Nested {
        reflect::TypeDesc desc{};
        std::vector<reflect::FieldDesc> fields;
    };
    struct EnumType {
        reflect::EnumDesc desc{};
        std::vector<reflect::EnumItem> items;
    };
    std::unordered_map<const RmStructDesc*, std::unique_ptr<Nested>> structs;
    std::unordered_map<const RmEnumDesc*, std::unique_ptr<EnumType>> enums;
    std::vector<std::unique_ptr<reflect::ArrayOps>> lists;
    // The guarded stand-ins its TypeDescs use for the DLL's construct / destruct / copy (a crash there
    // stops that object, not the editor or the game), given back when it goes.
    std::vector<size_t> guards;
    ScriptType() = default;
    ScriptType(const ScriptType&) = delete;
    ScriptType& operator=(const ScriptType&) = delete;
    ~ScriptType();
};
// This engine is a Debug build (its scripts must be too: RmModuleFlag_Debug).
#ifdef _DEBUG
inline constexpr bool kEngineDebug = true;
#else
inline constexpr bool kEngineDebug = false;
#endif
// Why a module cannot be used (empty = fine): another ABI, built as Debug for a Release engine (or the
// other way), no string functions, a type too strictly aligned, a field of a kind this version does
// not know (or an enum / struct / list without its description), two scripts of one name.
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
    // One frame: the input read; EndPlay of the started scripts switched off since (their entity or a
    // parent made not 有効); BeginPlay of every running script not started yet (new, or switched on
    // again); then Tick of every running one - type by type in the run order; inside a type by entity
    // id, then in the order they are attached. BeginPlay and EndPlay always come in pairs (a script
    // whose entity was deleted is gone with it). A script that crashed is not run again.
    void Frame(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order, float dt);
    // What was asked during the frame (Entity::Destroy, Remove<T>): the EndPlay of the scripts that go
    // (in the run order; a script started only), then the components and entities removed. Asked
    // again in those EndPlays: done too (a few rounds at most). Frame does it at its end.
    void ApplyAsked(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    // The hooks the systems asked for this frame (SystemRunner::Hooks: OnHit, OnTriggerEnter / Exit ...),
    // in the order asked; for each, every started script of its entity that is on, in the scripts'
    // order (the same as Tick's). Then what they destroyed / removed goes (ApplyAsked).
    void Deliver(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                 std::span<const HookCall> calls);
    // The events sent (rm::Events): each to the started, switched-on scripts with OnEvent it reaches, in the
    // scripts' order; one sent meanwhile right after (a few rounds at most). Then what they destroyed goes.
    // Frame and Deliver do it at their ends.
    void DeliverEvents(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    // The scripts built again while the game plays (design BluePrint 20-c): before, which scripts of the
    // scene had started / crashed (by entity, type and place among that type's - their objects are
    // made again); after the scene was read back with the new types, the same taken again and
    // OnReloaded called on each started one (in the run order; BeginPlay is not called again).
    struct RunState {
        int entity = 0;
        std::string type;
        size_t nth = 0;
        bool started = false, crashed = false;
    };
    std::vector<RunState> Remember(const Scene& scene) const;
    void Resume(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                std::span<const RunState> before);
    // EndPlay of every started script (Stop), then all forgotten - and the world no longer points at the
    // scene (it is about to go).
    void Stop(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order);
    void Reset();
    size_t Crashed() const { return crashed_.size(); }

    // How long the scripts took in the last Frame (and the Delivers after it), type by type (the most
    // first), and the coroutines run on after the Ticks - measured only while asked (the デバッグ panel).
    struct ScriptTime {
        const ScriptType* type = nullptr;  // null: the coroutines
        double ms = 0;
        int calls = 0;
    };
    void SetTiming(bool on) { timing_ = on; }
    std::vector<ScriptTime> Times() const;

private:
    struct Instance {
        int entity;
        const ScriptType* type;
        void* data;
        bool active;  // it and every parent 有効
    };
    // Every script of the scene (the switched-off ones too), in the run order.
    std::vector<Instance> Collect(Scene& scene, const ScriptModule& module, std::span<const ScriptOrderEntry> order) const;
    // Runs a hook guarded; false (and the script stopped, told once) when it crashed. EndPlay (and a crash)
    // stops the script's coroutines.
    bool Call(ScriptWorld& world, const Instance& in, int hook, float dt, int other = 0, const RmEvent* event = nullptr);

    std::unordered_set<void*> started_;
    std::unordered_set<void*> crashed_;
    bool timing_ = false;
    std::unordered_map<const ScriptType*, ScriptTime> times_;  // (null key: the coroutines)
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
unsigned long CallScriptHook(const RmScriptDesc& script, int hook, void* self, RmEntityId entity, float dt, RmEntityId other = 0,
                             const RmEvent* event = nullptr);
// A coroutine's resume / destroy, guarded the same way (0 = fine). *alive: resume's answer.
unsigned long CallCoroutineResume(int (*resume)(void*, RmWait*), void* frame, RmWait* next, int* alive);
unsigned long CallCoroutineDestroy(void (*destroy)(void*), void* frame);
// A script's (or its RM_STRUCT's) object made, destroyed, copied by the DLL's code - guarded the same way.
unsigned long CallScriptConstruct(void (*construct)(void*), void* p);
unsigned long CallScriptDestruct(void (*destruct)(void*), void* p);
unsigned long CallScriptCopy(void (*copy)(void*, const void*), void* dst, const void* src);

}  // namespace rm::game_production
