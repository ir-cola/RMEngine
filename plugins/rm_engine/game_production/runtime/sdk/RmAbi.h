/* RM Engine - the boundary between the engine and a game's scripts (a DLL). Plain C: what crosses it
 * is numbers, plain structs, UTF-8 `const char*` and function pointers - never std::string, never a
 * virtual function (design: TestEngine docs/NewEngineBluePrint.md 6 / 6-b).
 *
 *   engine (editor or exported game)                       script DLL (Scripts_0007.dll)
 *     LoadLibrary -> GetProcAddress("RmModuleInit")
 *     RmModuleInit(&api, RM_ABI_VERSION)  ───────────────▶  checks the version, keeps &api,
 *                                         ◀───────────────  returns the scripts it has (RmModule)
 *     each frame: the hooks of every script (BeginPlay / Tick / EndPlay), through function pointers;
 *     the scripts call back through api->core (and the plugins' parts: api->part(...))
 *
 * The engine never touches a script's std::string itself: the DLL has its own C runtime (it is a DLL,
 * /MD; the engine may be /MT), so its strings are read and written through the DLL's own two functions
 * (RmModule::stringGet / stringSet). Nothing else here allocates on one side and frees on the other.
 *
 * You do not include this file yourself: RM.h does. Change anything here -> raise RM_ABI_VERSION
 * (ABI 7: RmScriptDesc::onEvent; the parts below - time, coroutines, events, entity, mouse, input 2). */
#pragma once

#include <stdint.h>

#define RM_ABI_VERSION 7u

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t RmEntityId; /* 0 = none */

/* The kinds of fields a script may have (RM_PROPERTY). */
enum RmFieldType {
    RmField_Bool = 0,
    RmField_Int = 1,
    RmField_Float = 2,
    RmField_String = 3, /* std::string */
    RmField_Vec2 = 4,
    RmField_Vec3 = 5,
    RmField_Color = 6,
    RmField_EntityRef = 7,
    /* ABI 4 */
    RmField_Enum = 8,   /* an RM_ENUM enum class (RmFieldDesc::enumDesc) */
    RmField_Struct = 9, /* an RM_STRUCT struct (RmFieldDesc::structDesc) */
    RmField_List = 10,  /* std::vector of one of the others (RmFieldDesc::list; the element's enumDesc /
                           structDesc / assetKinds are the field's) */
    RmField_Asset = 11  /* rm::PrefabRef / rm::ImageRef / rm::ModelRef / rm::SoundRef: char guid[RM_ASSET_ID_SIZE]
                           (32 hex digits; "" = none) - plain bytes, read and written by the engine */
};

#define RM_ASSET_ID_SIZE 40

enum RmFieldFlags {
    RmFieldFlag_Edit = 1,      /* shown in the inspector */
    RmFieldFlag_ReadOnly = 2,
    RmFieldFlag_Transient = 4, /* RM_TRANSIENT: not saved, not shown */
    RmFieldFlag_HasMin = 8,
    RmFieldFlag_HasMax = 16,
    RmFieldFlag_HasStep = 32
};

typedef struct RmFieldDesc {
    const char* name; /* C++ name = key in the scene file */
    uint32_t type;    /* RmFieldType */
    uint32_t flags;   /* RmFieldFlags */
    void* (*access)(void* self);
    const char* label; /* UTF-8, may be null */
    const char* tooltip;
    double min, max, step;
    int32_t line; /* in the header */
    /* ABI 4: what the kinds above need (null for the others) */
    const struct RmEnumDesc* enumDesc;
    const struct RmStructDesc* structDesc;
    const struct RmListDesc* list;
    const char* assetKinds; /* RmField_Asset: "image" / "model" / "audio" / ".rmprefab" (core/asset's kinds, or an extension) */
} RmFieldDesc;

typedef struct RmEnumItem {
    const char* name;  /* saved in files */
    const char* label; /* shown; null: the name */
    int64_t value;
} RmEnumItem;

typedef struct RmEnumDesc {
    const char* name; /* C++ name */
    const RmEnumItem* items;
    uint32_t itemCount;
    int64_t (*get)(const void* p);
    void (*set)(void* p, int64_t v);
} RmEnumDesc;

/* An RM_STRUCT inside a script (a value: no hooks). */
typedef struct RmStructDesc {
    const char* name;
    const char* label;
    uint64_t size, align;
    const struct RmFieldDesc* fields;
    uint32_t fieldCount;
    void (*construct)(void* p);
    void (*destruct)(void* p);
    void (*copy)(void* dst, const void* src);
} RmStructDesc;

/* A std::vector field, worked by the DLL (its C runtime owns the memory). */
typedef struct RmListDesc {
    uint32_t element; /* RmFieldType of an element (never RmField_List) */
    uint32_t reserved;
    uint64_t (*size)(const void* vec);
    void (*resize)(void* vec, uint64_t n);
    void* (*at)(void* vec, uint64_t i);
} RmListDesc;

typedef struct RmScriptDesc {
    const char* name;   /* C++ name ("Mover", "game::Mover") = what the scene saves */
    const char* label;  /* RM_SCRIPT(Label="...") or the name */
    const char* header; /* the header it is declared in (for messages) */
    uint64_t size, align;
    const RmFieldDesc* fields;
    uint32_t fieldCount;
    void (*construct)(void* p); /* placement new with the default member values */
    void (*destruct)(void* p);
    void (*copy)(void* dst, const void* src);
    /* null when the script does not have the hook */
    void (*beginPlay)(void* self, RmEntityId self_entity);
    void (*tick)(void* self, RmEntityId self_entity, float dt);
    void (*endPlay)(void* self, RmEntityId self_entity);
    /* ABI 4: after the scripts were built again while the game plays (the values carried over by
       name; BeginPlay is not called again). Null when the script does not have it. */
    void (*onReloaded)(void* self, RmEntityId self_entity);
    /* ABI 5: what the physics tells (RMPhysics.h). `other` is the other entity. Null when the script
       does not have the hook. Called after the frame's fixed steps, on started scripts only. */
    void (*onHit)(void* self, RmEntityId self_entity, RmEntityId other);          /* began touching (not a trigger) */
    void (*onTriggerEnter)(void* self, RmEntityId self_entity, RmEntityId other); /* one of the two is a trigger */
    void (*onTriggerExit)(void* self, RmEntityId self_entity, RmEntityId other);
    /* ABI 6: a game UI button of the entity was pressed (RMUI.h: the mouse, or 決定 on the focused one).
       Called after the frame's systems, on started scripts only. Null when the script does not have it. */
    void (*onClick)(void* self, RmEntityId self_entity);
    /* ABI 7: an event sent by rm::Events (RM.h) reached the entity. Called after the frame's Ticks (and
       after the systems' / services' hooks), on started scripts only. Null when the script does not have it. */
    void (*onEvent)(void* self, RmEntityId self_entity, const struct RmEvent* event);
} RmScriptDesc;

/* An event (ABI 7): a name, who sent it (0: the engine / nobody) and what it carries. The strings live
   until the hook returns (copy what you keep). */
typedef struct RmEvent {
    const char* name;   /* UTF-8 */
    RmEntityId sender;  /* the entity whose script sent it (0: none) */
    double number;
    const char* text;   /* UTF-8, never null ("" when none) */
    RmEntityId entity;  /* an entity it carries (0: none) */
} RmEvent;

/* The hooks by number (a system asks for them by these: game_production::HookCall::hook). */
enum RmHook {
    RM_HOOK_BEGIN_PLAY = 0,
    RM_HOOK_TICK = 1,
    RM_HOOK_END_PLAY = 2,
    RM_HOOK_RELOADED = 3,
    RM_HOOK_HIT = 4,
    RM_HOOK_TRIGGER_ENTER = 5,
    RM_HOOK_TRIGGER_EXIT = 6,
    RM_HOOK_CLICK = 7, /* ABI 6 */
    RM_HOOK_EVENT = 8  /* ABI 7 (called with an RmEvent) */
};

enum RmModuleFlags {
    RmModuleFlag_Debug = 1 /* built as Debug (the debug C runtime): only a Debug engine takes it */
};

typedef struct RmModule {
    uint32_t abi;   /* RM_ABI_VERSION the DLL was built with */
    uint32_t flags; /* RmModuleFlags */
    uint32_t scriptCount;
    const RmScriptDesc* scripts;
    /* A script's std::string field (the pointer RmFieldDesc::access gives), by the DLL itself:
       get copies what fits into buf (0-terminated) and returns the full length in bytes. */
    uint64_t (*stringGet)(const void* field, char* buf, uint64_t cap);
    void (*stringSet)(void* field, const char* utf8, uint64_t len);
} RmModule;

/* What every engine gives (game production's part). `ctx` is the engine's: pass it back as it is. */
typedef struct RmCoreApi {
    void (*log)(void* ctx, int level, const char* utf8); /* 0 = info, 1 = warning, 2 = error */
    float (*deltaTime)(void* ctx);                       /* seconds of this frame */
    double (*time)(void* ctx);                           /* seconds since Play */
    RmEntityId (*findByName)(void* ctx, const char* utf8);
    int (*exists)(void* ctx, RmEntityId e);
    int (*isActive)(void* ctx, RmEntityId e);
    void (*setActive)(void* ctx, RmEntityId e, int active);
    /* Copies the name (UTF-8, 0-terminated, cut to cap) and returns its full length in bytes. */
    uint32_t (*getName)(void* ctx, RmEntityId e, char* buf, uint32_t cap);
    RmEntityId (*parent)(void* ctx, RmEntityId e);
    /* Keys by Windows virtual-key code (VK_LEFT = 0x25, 'A' = 0x41 ...). */
    int (*keyDown)(void* ctx, int vk);
    int (*keyPressed)(void* ctx, int vk);  /* went down this frame */
    int (*keyReleased)(void* ctx, int vk); /* went up this frame */
    RmEntityId (*camera)(void* ctx);       /* the camera the game sees through */
    void (*setCamera)(void* ctx, RmEntityId e);
    /* The nth script of type `typeName` (RmScriptDesc::name) of the entity; null: none. */
    void* (*getScript)(void* ctx, RmEntityId e, const char* typeName, uint32_t nth);
    /* Scenes (ABI 3). The game's scenes are the list of the game settings, in its order; a scene's name is
       its file's name without .rmscene. loadScene / loadSceneAt ask to switch at the end of this frame
       (EndPlay of this scene, the new one read, its BeginPlay): 1 = asked, 0 = no such scene (told). */
    int (*loadScene)(void* ctx, const char* nameUtf8);
    int (*loadSceneAt)(void* ctx, int32_t index);
    /* The scene playing: its name (copied like getName; the full length returned), its place in the
       list (-1: not in it) and how many the list has. */
    uint32_t (*sceneName)(void* ctx, char* buf, uint32_t cap);
    int32_t (*sceneIndex)(void* ctx);
    int32_t (*sceneCount)(void* ctx);
    /* Entities and components (ABI 4). spawn / spawnPrefab make the entity now (under `parent`, 0: the
       top) and return it; its scripts start at the next frame (BeginPlay before their first Tick).
       destroy / removeComponent take effect at the end of this frame: EndPlay first, the entity's
       children with it; an id of something gone is never used again (exists() says 0). */
    RmEntityId (*spawn)(void* ctx, const char* nameUtf8, RmEntityId parent);
    RmEntityId (*spawnPrefab)(void* ctx, const char* assetId, RmEntityId parent); /* 0: not a prefab of the game (told) */
    void (*destroy)(void* ctx, RmEntityId e);
    /* A component by its type's name (a script's RmScriptDesc::name, or an engine one's, e.g.
       "rm::space3d::Transform"): added with its default values (one that cannot be twice: the one there)
       and returned; null: no such type / no such entity. */
    void* (*addComponent)(void* ctx, RmEntityId e, const char* typeName);
    int (*removeComponent)(void* ctx, RmEntityId e, const char* typeName, uint32_t nth); /* 1: will go */
    uint32_t (*countComponents)(void* ctx, RmEntityId e, const char* typeName);
} RmCoreApi;

typedef struct RmApi {
    uint32_t abi; /* RM_ABI_VERSION of the engine */
    uint32_t reserved;
    void* ctx;
    const RmCoreApi* core;
    /* A plugin's part of the API by name and version (RM3D.h: Transform in 3D ...); null when the
       plugin is not in this engine / game. */
    const void* (*part)(void* ctx, const char* name, uint32_t version);
} RmApi;

/* --- Game production's own parts (asked by name with RmApi::part, like a plugin's) -------------
 * A part has its own version: adding one never changes RmCoreApi nor RM_ABI_VERSION, and an engine
 * without it gives null (RM.h then answers "nothing"). Change a part -> a new version of that part. */

/* Input by action name (the game settings' 入力): "rm_engine.game_production.input", version 1. */
#define RM_PART_INPUT "rm_engine.game_production.input"
#define RM_PART_INPUT_VERSION 1u
typedef struct RmInputApi {
    int (*down)(void* ctx, const char* action);     /* held now */
    int (*pressed)(void* ctx, const char* action);  /* went down this frame */
    int (*released)(void* ctx, const char* action); /* went up this frame */
    float (*axis)(void* ctx, const char* action);   /* -1..1 */
} RmInputApi;

/* Input, version 2: the same by player, and what the player may change. `player`: -1 = everyone (every
 * game pad, the keyboard and the mouse - as version 1); 0..3 = that player: the game pad in that slot,
 * and the keyboard and the mouse when the game settings' キーボードとマウスのプレイヤー is that one.
 * The bindings are the player's: the game settings' 入力 until a script changes them (setBinding ...);
 * saveBindings writes them to the player's PC (input.json next to the saves), loadBindings reads them
 * back (the game does that by itself when it starts). */
#define RM_PART_INPUT_VERSION2 2u
typedef struct RmInputApi2 {
    int (*down)(void* ctx, const char* action, int32_t player);
    int (*pressed)(void* ctx, const char* action, int32_t player);
    int (*released)(void* ctx, const char* action, int32_t player);
    float (*axis)(void* ctx, const char* action, int32_t player);
    int (*padConnected)(void* ctx, int32_t player);
    /* Shakes the player's pad: low = the left (heavy) motor, high = the right (light) one, 0..1. */
    void (*vibrate)(void* ctx, int32_t player, float low, float high, float seconds);
    uint32_t (*bindingCount)(void* ctx, const char* action);
    /* control: the game settings' InputControl value (a key's virtual-key code, a pad's 0x100 + ...). */
    int (*binding)(void* ctx, const char* action, uint32_t index, int32_t* control, float* scale);
    int (*setBinding)(void* ctx, const char* action, uint32_t index, int32_t control, float scale); /* index == count: added */
    int (*removeBinding)(void* ctx, const char* action, uint32_t index);
    void (*resetBindings)(void* ctx); /* back to the game settings' */
    int (*saveBindings)(void* ctx);
    int (*loadBindings)(void* ctx);
    /* What went down this frame for `player` (a key, a mouse button, a pad's button; a stick or a trigger
       pushed past half way): its control, and *direction 1 / -1 (a stick to the left: -1); 0: nothing.
       For "press the key for Jump". */
    int32_t (*anyPressed)(void* ctx, int32_t player, float* direction);
    /* A control's name ("Space", "A", "PadA", "MouseLeft" ...), copied like getName. */
    uint32_t (*controlName)(void* ctx, int32_t control, char* buf, uint32_t cap);
} RmInputApi2;

/* The mouse on the game's screen: "rm_engine.game_production.mouse", 1. The screen is the game
 * settings' 画面 (DIPs, (0, 0) at the top left, y down) - the editor's game view is scaled to it. */
#define RM_PART_MOUSE "rm_engine.game_production.mouse"
#define RM_PART_MOUSE_VERSION 1u
typedef struct RmMouseApi {
    int (*position)(void* ctx, float out[2]); /* 0: not over the game (out: where it was last) */
    void (*delta)(void* ctx, float out[2]);   /* moved since the last frame (0 while not over the game) */
    float (*wheel)(void* ctx);                /* the wheel's notches this frame: + turned away from you */
    void (*screenSize)(void* ctx, float out[2]);
} RmMouseApi;

/* Time: "rm_engine.game_production.time", 1. The scale multiplies every dt of the game (the scripts'
 * Tick, the systems, the physics, the particles); 0 stops them (a pause menu) - Tick is still called. */
#define RM_PART_TIME "rm_engine.game_production.time"
#define RM_PART_TIME_VERSION 1u
typedef struct RmTimeApi {
    float (*scale)(void* ctx);
    void (*setScale)(void* ctx, float scale);
    float (*unscaledDeltaTime)(void* ctx);
    double (*unscaledTime)(void* ctx);
    uint64_t (*frameCount)(void* ctx);
} RmTimeApi;

/* Coroutines: "rm_engine.game_production.coroutines", 1. The coroutine's frame is the DLL's (C++20
 * coroutines: RM.h's rm::Task); the engine keeps when to run it next. resume(frame, &next) runs it to its
 * next wait and writes the wait; it returns 0 when the coroutine ended. destroy(frame) frees it (ended,
 * or stopped early). The owner is the script whose hook (or coroutine) is running when it is started:
 * its EndPlay (or its entity destroyed, the scene switched, the game stopped, the scripts built again)
 * stops them. */
enum RmWaitKind {
    RmWait_Frames = 0,      /* `frames` frames (1: the next frame) */
    RmWait_Seconds = 1,     /* `seconds` of game time (the time scale counts) */
    RmWait_RealSeconds = 2, /* `seconds` of real time */
    RmWait_Coroutine = 3    /* until the coroutine `coroutine` has ended */
};
typedef struct RmWait {
    uint32_t kind;
    uint32_t frames;
    double seconds;
    uint64_t coroutine;
} RmWait;
#define RM_PART_COROUTINES "rm_engine.game_production.coroutines"
#define RM_PART_COROUTINES_VERSION 1u
typedef struct RmCoroutinesApi {
    /* Runs it now to its first wait; its id (0: it ended at once - destroy was called). */
    uint64_t (*start)(void* ctx, void* frame, int (*resume)(void* frame, RmWait* next), void (*destroy)(void* frame));
    void (*stop)(void* ctx, uint64_t id);
    void (*stopAll)(void* ctx); /* every coroutine of the script that is running */
    int (*running)(void* ctx, uint64_t id);
} RmCoroutinesApi;

/* Events: "rm_engine.game_production.events", 1. Sent now, delivered after this frame's Ticks (the
 * scripts' OnEvent, in their order); one sent in an OnEvent comes right after. scope: 0 = every entity,
 * 1 = `target` only, 2 = `target` and everything below it. */
#define RM_PART_EVENTS "rm_engine.game_production.events"
#define RM_PART_EVENTS_VERSION 1u
typedef struct RmEventsApi {
    void (*send)(void* ctx, uint32_t scope, RmEntityId target, const char* name, double number, const char* text, uint64_t textLen,
                 RmEntityId entity);
} RmEventsApi;

/* Entities, more: "rm_engine.game_production.entity", 1. Tags and layers are names (the game settings
 * list them); "" = none (a layer "": the default one, number 0). */
#define RM_PART_ENTITY "rm_engine.game_production.entity"
#define RM_PART_ENTITY_VERSION 1u
typedef struct RmEntityApi {
    uint32_t (*getTag)(void* ctx, RmEntityId e, char* buf, uint32_t cap);
    void (*setTag)(void* ctx, RmEntityId e, const char* utf8);
    uint32_t (*getLayer)(void* ctx, RmEntityId e, char* buf, uint32_t cap);
    void (*setLayer)(void* ctx, RmEntityId e, const char* utf8);
    /* A layer's number: "" 0, the game settings' レイヤー 1, 2 ... in their order; -1: not there. */
    int32_t (*layerIndex)(void* ctx, const char* name);
    /* The entities with this tag (in the 階層's order): how many there are, the first `cap` copied. */
    uint32_t (*findWithTag)(void* ctx, const char* tag, RmEntityId* out, uint32_t cap);
    /* The children of `e` (0: the top level), in order: how many, the first `cap` copied. */
    uint32_t (*children)(void* ctx, RmEntityId e, RmEntityId* out, uint32_t cap);
    /* Moves `e` (with what is below it) under `parent` (0: the top) at `index` among its children
       (-1: the end). keepWorld: it stays where it is in the world (its Transform changed). 0: refused
       (into itself, no such entity). */
    int (*setParent)(void* ctx, RmEntityId e, RmEntityId parent, int keepWorld, int32_t index);
    void (*setName)(void* ctx, RmEntityId e, const char* utf8);
} RmEntityApi;

/* Global variables (the game settings' グローバル変数) by name: "rm_engine.game_production.globals", 1.
 * `type` is how the script holds it: RmField_Bool (int32_t 0 / 1), RmField_Int (int32_t), RmField_Float
 * (float), RmField_Vec2 (float[2]), RmField_Vec3 (float[3]); the engine converts from / to the
 * variable's own type (a text variable is read / written only as text). 0 = no such variable (told
 * once; `out` untouched). */
#define RM_PART_GLOBALS "rm_engine.game_production.globals"
#define RM_PART_GLOBALS_VERSION 1u
typedef struct RmGlobalsApi {
    int (*get)(void* ctx, const char* name, uint32_t type, void* out);
    int (*set)(void* ctx, const char* name, uint32_t type, const void* value);
    /* As text (a number variable: its text, "3" / "1.5" / "true" / "1, 2"): copied like getName, the
       full length returned. */
    uint64_t (*getString)(void* ctx, const char* name, char* buf, uint64_t cap);
    int (*setString)(void* ctx, const char* name, const char* utf8, uint64_t len);
} RmGlobalsApi;

/* Saving every global variable to a slot ("0", "auto": letters, digits, _ and -): "rm_engine.
 * game_production.save", 1. 1 = done; 0 = not (told on the console). */
#define RM_PART_SAVE "rm_engine.game_production.save"
#define RM_PART_SAVE_VERSION 1u
typedef struct RmSaveApi {
    int (*write)(void* ctx, const char* slot);
    int (*read)(void* ctx, const char* slot);
    int (*exists)(void* ctx, const char* slot);
    int (*remove)(void* ctx, const char* slot);
} RmSaveApi;

/* The game itself: "rm_engine.game_production.game", 1. quit: the game ends after this frame (the
 * exported game's window closes; the editor's Play stops, as ■). */
#define RM_PART_GAME "rm_engine.game_production.game"
#define RM_PART_GAME_VERSION 1u
typedef struct RmGameApi {
    void (*quit)(void* ctx);
} RmGameApi;

/* The one function a script DLL exports (rmgen writes it). Returns null when `abi` is not the
   version it was built for. */
typedef const RmModule* (*RmModuleInitFn)(const RmApi* api, uint32_t abi);

#ifdef __cplusplus
}
#endif
