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
 * The script DLL and the engine are built with the same toolset and the same C runtime (/MD), so a
 * script's std::string field can be read and written by the engine; but no function here takes one.
 *
 * You do not include this file yourself: RM.h does. Change anything here -> raise RM_ABI_VERSION. */
#pragma once

#include <stdint.h>

#define RM_ABI_VERSION 1u

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

typedef struct RmModule {
    uint32_t abi; /* RM_ABI_VERSION the DLL was built with */
    uint32_t scriptCount;
    const RmScriptDesc* scripts;
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

/* The one function a script DLL exports (rmgen writes it). Returns null when `abi` is not the
   version it was built for. */
typedef const RmModule* (*RmModuleInitFn)(const RmApi* api, uint32_t abi);

#ifdef __cplusplus
}
#endif
