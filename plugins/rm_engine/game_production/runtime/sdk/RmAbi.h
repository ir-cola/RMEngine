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
 * You do not include this file yourself: RM.h does. Change anything here -> raise RM_ABI_VERSION. */
#pragma once

#include <stdint.h>

#define RM_ABI_VERSION 3u

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
    RmField_EntityRef = 7
};

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
} RmFieldDesc;

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
} RmScriptDesc;

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

/* The one function a script DLL exports (rmgen writes it). Returns null when `abi` is not the
   version it was built for. */
typedef const RmModule* (*RmModuleInitFn)(const RmApi* api, uint32_t abi);

#ifdef __cplusplus
}
#endif
