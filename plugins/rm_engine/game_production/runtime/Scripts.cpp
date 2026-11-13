#include "rm_engine/game_production/runtime/Scripts.h"

#include "rm_engine/game_production/runtime/Systems.h"

#include "base/Utf.h"
#include "platform/Win.h"
#include "rm_engine/game_production/runtime/SceneFile.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <system_error>
#include <utility>

namespace rm::game_production {

namespace {

ScriptWorld& W(void* ctx) { return *static_cast<ScriptWorld*>(ctx); }

SceneEntity* EntityOf(void* ctx, RmEntityId id) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || id == 0 || id > static_cast<RmEntityId>(std::numeric_limits<int>::max())) return nullptr;
    return w.scene->Find(static_cast<int>(id));
}

// --- The core table (RmCoreApi): every engine gives it ---------------------------------------
void ApiLog(void* ctx, int level, const char* text) {
    if (W(ctx).log) W(ctx).log(level, text ? text : "");
}
float ApiDeltaTime(void* ctx) { return W(ctx).dt; }
double ApiTime(void* ctx) { return W(ctx).time; }
RmEntityId ApiFindByName(void* ctx, const char* name) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || !name) return 0;
    for (const TreeEntry& t : w.scene->TreeOrder()) {
        if (t.entity->data.name == name) return static_cast<RmEntityId>(t.entity->data.id);
    }
    return 0;
}
int ApiExists(void* ctx, RmEntityId e) { return EntityOf(ctx, e) ? 1 : 0; }
int ApiIsActive(void* ctx, RmEntityId e) {
    ScriptWorld& w = W(ctx);
    size_t steps = 0;
    for (const SceneEntity* x = EntityOf(ctx, e); x && steps++ <= w.scene->Entities().size(); x = w.scene->Find(x->data.parent)) {
        if (!x->data.active) return 0;
        if (x->data.parent == 0) return 1;
    }
    return 0;
}
void ApiSetActive(void* ctx, RmEntityId e, int active) {
    if (SceneEntity* x = EntityOf(ctx, e)) x->data.active = active != 0;
}
uint32_t ApiGetName(void* ctx, RmEntityId e, char* buf, uint32_t cap) {
    const SceneEntity* x = EntityOf(ctx, e);
    const std::string& name = x ? x->data.name : std::string();
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(name.size(), cap - 1);
        std::memcpy(buf, name.data(), n);
        buf[n] = '\0';
    }
    return static_cast<uint32_t>(name.size());
}
RmEntityId ApiParent(void* ctx, RmEntityId e) {
    const SceneEntity* x = EntityOf(ctx, e);
    return x ? static_cast<RmEntityId>(x->data.parent) : 0;
}
bool KeyIn(const InputFrame& f, int vk) { return vk > 0 && vk < 256 && f.keys[static_cast<size_t>(vk)] != 0; }
int ApiKeyDown(void* ctx, int vk) { return KeyIn(W(ctx).inputNow, vk) ? 1 : 0; }
int ApiKeyPressed(void* ctx, int vk) { return KeyIn(W(ctx).inputNow, vk) && !KeyIn(W(ctx).inputBefore, vk) ? 1 : 0; }
int ApiKeyReleased(void* ctx, int vk) { return !KeyIn(W(ctx).inputNow, vk) && KeyIn(W(ctx).inputBefore, vk) ? 1 : 0; }
RmEntityId ApiCamera(void* ctx) { return W(ctx).camera; }
void ApiSetCamera(void* ctx, RmEntityId e) { W(ctx).camera = e; }
void* ApiGetScript(void* ctx, RmEntityId e, const char* typeName, uint32_t nth) {
    SceneEntity* x = EntityOf(ctx, e);
    Component* c = x && typeName ? x->FindComponent(typeName, nth) : nullptr;
    return c && c->Known() ? c->value.Get() : nullptr;
}
int ApiLoadSceneAt(void* ctx, int32_t index) {
    ScriptWorld& w = W(ctx);
    if (index < 0 || static_cast<size_t>(index) >= w.sceneNames.size()) {
        if (w.log) w.log(1, std::format("Game::LoadScene({}): その番号のシーンはありません (ゲームのシーンは {} 個)", index, w.sceneNames.size()));
        return 0;
    }
    w.sceneAsked = index;
    return 1;
}
int ApiLoadScene(void* ctx, const char* name) {
    ScriptWorld& w = W(ctx);
    const std::string_view n = name ? name : "";
    if (const auto at = FindSceneByName(w.sceneNames, n)) {
        w.sceneAsked = static_cast<int>(*at);
        return 1;
    }
    if (w.log) w.log(1, std::format("Game::LoadScene(\"{}\"): そのシーンはゲームのシーンの一覧にありません (シーン パネルで足します)", n));
    return 0;
}
uint32_t ApiSceneName(void* ctx, char* buf, uint32_t cap) {
    const ScriptWorld& w = W(ctx);
    static const std::string kNone;
    const std::string& name = w.sceneIndex >= 0 && static_cast<size_t>(w.sceneIndex) < w.sceneNames.size() ? w.sceneNames[static_cast<size_t>(w.sceneIndex)] : kNone;
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(name.size(), cap - 1);
        std::memcpy(buf, name.data(), n);
        buf[n] = '\0';
    }
    return static_cast<uint32_t>(name.size());
}
int32_t ApiSceneIndex(void* ctx) { return W(ctx).sceneIndex; }
int32_t ApiSceneCount(void* ctx) { return static_cast<int32_t>(W(ctx).sceneNames.size()); }

// --- Game production's own parts (RmApi::part by name, like a plugin's) -------------------------
// Input by action name: this frame and the last, evaluated on the game settings' 入力.
int ApiInputDown(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a && EvaluateAction(*a, W(ctx).inputNow).down ? 1 : 0;
}
int ApiInputPressed(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a && EvaluateAction(*a, W(ctx).inputNow).down && !EvaluateAction(*a, W(ctx).inputBefore).down ? 1 : 0;
}
int ApiInputReleased(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a && !EvaluateAction(*a, W(ctx).inputNow).down && EvaluateAction(*a, W(ctx).inputBefore).down ? 1 : 0;
}
float ApiInputAxis(void* ctx, const char* name) {
    const InputAction* a = W(ctx).Action(name);
    return a ? EvaluateAction(*a, W(ctx).inputNow).axis : 0.0f;
}
constexpr RmInputApi kInput{&ApiInputDown, &ApiInputPressed, &ApiInputReleased, &ApiInputAxis};

// Global variables by name: numbers convert between the script's type and the variable's; a text
// variable is read and written only as text.
int Components(uint32_t type) { return type == RmField_Vec3 ? 3 : type == RmField_Vec2 ? 2 : 1; }
bool NumberKind(uint32_t type) {
    return type == RmField_Bool || type == RmField_Int || type == RmField_Float || type == RmField_Vec2 || type == RmField_Vec3;
}
int ApiGlobalGet(void* ctx, const char* name, uint32_t type, void* out) {
    ScriptWorld& w = W(ctx);
    const GlobalValue* g = w.Global(name);
    if (!g || !out || !NumberKind(type)) return 0;
    if (g->type == GlobalType::String) {
        w.TellOnce(std::string("global-text:") + name, std::format("グローバル変数「{}」は文字です。GlobalVar<std::string> で読みます", name));
        return 0;
    }
    if (type == RmField_Bool) {
        *static_cast<int32_t*>(out) = g->v[0] != 0 ? 1 : 0;
    } else if (type == RmField_Int) {
        *static_cast<int32_t*>(out) = static_cast<int32_t>(g->v[0]);
    } else {
        for (int i = 0; i < Components(type); ++i) static_cast<float*>(out)[i] = static_cast<float>(g->v[i]);
    }
    return 1;
}
int ApiGlobalSet(void* ctx, const char* name, uint32_t type, const void* value) {
    ScriptWorld& w = W(ctx);
    GlobalValue* g = w.Global(name);
    if (!g || !value || !NumberKind(type)) return 0;
    if (g->type == GlobalType::String) {
        w.TellOnce(std::string("global-text:") + name, std::format("グローバル変数「{}」は文字です。GlobalVar<std::string> で書きます", name));
        return 0;
    }
    double in[3]{};
    const int n = Components(type);
    if (type == RmField_Bool || type == RmField_Int) {
        in[0] = static_cast<double>(*static_cast<const int32_t*>(value));
    } else {
        for (int i = 0; i < n; ++i) in[i] = static_cast<double>(static_cast<const float*>(value)[i]);
    }
    // Into the variable's type: a plain number takes x; a vector takes what the script gives (the rest kept).
    GlobalValue next = *g;
    if (g->type == GlobalType::Vec2 || g->type == GlobalType::Vec3) {
        for (int i = 0; i < n && i < (g->type == GlobalType::Vec3 ? 3 : 2); ++i) next.v[i] = in[i];
    } else if (auto fitted = GlobalFromJson(g->type, json::Value(in[0]))) {
        next = std::move(*fitted);
    }
    *g = next;
    return 1;
}
uint64_t ApiGlobalGetString(void* ctx, const char* name, char* buf, uint64_t cap) {
    const GlobalValue* g = W(ctx).Global(name);
    const std::string text = g ? FormatGlobalValue(*g) : std::string();
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(text.size(), static_cast<size_t>(cap - 1));
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
    }
    return text.size();
}
int ApiGlobalSetString(void* ctx, const char* name, const char* utf8, uint64_t len) {
    ScriptWorld& w = W(ctx);
    GlobalValue* g = w.Global(name);
    if (!g) return 0;
    const std::string_view text = utf8 ? std::string_view(utf8, static_cast<size_t>(len)) : std::string_view();
    auto v = ParseGlobalValue(g->type, text);
    if (!v) {
        w.TellOnce(std::string("global-parse:") + name, std::format("グローバル変数「{}」に「{}」は入りません (型が違います)", name, text));
        return 0;
    }
    *g = std::move(*v);
    return 1;
}
constexpr RmGlobalsApi kGlobals{&ApiGlobalGet, &ApiGlobalSet, &ApiGlobalGetString, &ApiGlobalSetString};

// Saving: every global variable to <saveFolder>/save_<slot>.json.
int ApiSaveWrite(void* ctx, const char* slot) {
    ScriptWorld& w = W(ctx);
    std::string why;
    const std::string s = slot ? slot : "";
    if (WriteSave(w.globals, w.saveFolder, s, &why)) return 1;
    if (w.log) w.log(1, std::format("Save::Write(\"{}\"): 書けませんでした: {}", s, why));
    return 0;
}
int ApiSaveRead(void* ctx, const char* slot) {
    ScriptWorld& w = W(ctx);
    std::string why;
    std::vector<std::string> warnings;
    const std::string s = slot ? slot : "";
    const bool ok = ReadSave(w.globals, w.saveFolder, s, &warnings, &why);
    if (w.log) {
        for (const std::string& m : warnings) w.log(1, m);
        // (no save yet is the usual case at a game's first start - the script's false says it; only a
        // save that is there and cannot be read is told)
        if (!ok && SaveExists(w.saveFolder, s)) w.log(1, std::format("Save::Read(\"{}\"): 読めませんでした: {}", s, why));
    }
    return ok ? 1 : 0;
}
int ApiSaveExists(void* ctx, const char* slot) { return SaveExists(W(ctx).saveFolder, slot ? slot : "") ? 1 : 0; }
int ApiSaveRemove(void* ctx, const char* slot) {
    ScriptWorld& w = W(ctx);
    std::string why;
    const std::string s = slot ? slot : "";
    if (DeleteSave(w.saveFolder, s, &why)) return 1;
    if (w.log) w.log(1, std::format("Save::Delete(\"{}\"): 消せませんでした: {}", s, why));
    return 0;
}
constexpr RmSaveApi kSave{&ApiSaveWrite, &ApiSaveRead, &ApiSaveExists, &ApiSaveRemove};

const void* ApiPart(void* ctx, const char* name, uint32_t version) {
    if (name && version == RM_PART_INPUT_VERSION && std::strcmp(name, RM_PART_INPUT) == 0) return &kInput;
    if (name && version == RM_PART_GLOBALS_VERSION && std::strcmp(name, RM_PART_GLOBALS) == 0) return &kGlobals;
    if (name && version == RM_PART_SAVE_VERSION && std::strcmp(name, RM_PART_SAVE) == 0) return &kSave;
    return FindScriptApiPart(W(ctx).parts, name, version);
}

// Entities and components (ABI 4).
RmEntityId ApiSpawn(void* ctx, const char* name, RmEntityId parent) {
    ScriptWorld& w = W(ctx);
    if (!w.scene) return 0;
    const int under = EntityOf(ctx, parent) ? static_cast<int>(parent) : 0;
    return static_cast<RmEntityId>(w.scene->Create(name && *name ? name : "エンティティ", under));
}
RmEntityId ApiSpawnPrefab(void* ctx, const char* assetId, RmEntityId parent) {
    ScriptWorld& w = W(ctx);
    if (!w.scene || !assetId) return 0;
    const int under = EntityOf(ctx, parent) ? static_cast<int>(parent) : 0;
    reflect::Warnings warnings;
    const int made = SpawnPrefab(*w.scene, AssetRef{assetId}, under, w.prefab, warnings);
    for (const std::string& m : warnings) w.TellOnce("spawn:" + m, m);
    if (!made) w.TellOnce(std::string("prefab:") + assetId, std::format("rm::Spawn: Prefab ({}) がゲームにありません (消されたか、書き出しに入っていません)", assetId));
    return static_cast<RmEntityId>(made);
}
void ApiDestroy(void* ctx, RmEntityId e) {
    if (EntityOf(ctx, e)) W(ctx).destroyAsked.push_back(static_cast<int>(e));
}
const reflect::TypeDesc* ComponentType(const ScriptWorld& w, const char* typeName) {
    if (!w.scene || !typeName) return nullptr;
    for (const reflect::TypeDesc* t : w.scene->ComponentTypes()) {
        if (std::strcmp(t->name, typeName) == 0) return t;
    }
    return nullptr;
}
void* ApiAddComponent(void* ctx, RmEntityId e, const char* typeName) {
    ScriptWorld& w = W(ctx);
    SceneEntity* x = EntityOf(ctx, e);
    const reflect::TypeDesc* type = ComponentType(w, typeName);
    if (!x || !type) {
        if (x && typeName) w.TellOnce(std::string("add:") + typeName, std::format("Add: 部品「{}」はこのゲームにありません", typeName));
        return nullptr;
    }
    if (!w.scene->AllowsMultiple(type->name)) {
        if (Component* there = x->FindComponent(type->name)) return there->Known() ? there->value.Get() : nullptr;  // (one at most: the one there)
    }
    if (!w.scene->AddComponent(x->data.id, *type)) return nullptr;
    Component* made = EntityOf(ctx, e)->FindComponent(type->name, EntityOf(ctx, e)->CountComponents(type->name) - 1);
    return made ? made->value.Get() : nullptr;
}
int ApiRemoveComponent(void* ctx, RmEntityId e, const char* typeName, uint32_t nth) {
    SceneEntity* x = EntityOf(ctx, e);
    Component* c = x && typeName ? x->FindComponent(typeName, nth) : nullptr;
    if (!c) return 0;
    W(ctx).removeAsked.push_back({x->data.id, c->value.Get()});
    return 1;
}
uint32_t ApiCountComponents(void* ctx, RmEntityId e, const char* typeName) {
    const SceneEntity* x = EntityOf(ctx, e);
    return x && typeName ? static_cast<uint32_t>(x->CountComponents(typeName)) : 0;
}

constexpr RmCoreApi kCore{&ApiLog,        &ApiDeltaTime,   &ApiTime,        &ApiFindByName,  &ApiExists,
                          &ApiIsActive,   &ApiSetActive,   &ApiGetName,     &ApiParent,      &ApiKeyDown,
                          &ApiKeyPressed, &ApiKeyReleased, &ApiCamera,      &ApiSetCamera,   &ApiGetScript,
                          &ApiLoadScene,  &ApiLoadSceneAt, &ApiSceneName,   &ApiSceneIndex,  &ApiSceneCount,
                          &ApiSpawn,      &ApiSpawnPrefab, &ApiDestroy,     &ApiAddComponent, &ApiRemoveComponent,
                          &ApiCountComponents};

// The C++ type of every script, as far as the engine is concerned (a script's own type lives in the DLL).
struct ScriptTag {};
const std::type_info& ScriptCppType() { return typeid(ScriptTag); }

bool KnownField(uint32_t type) { return type <= RmField_Asset; }

reflect::FieldType FieldTypeOf(uint32_t t) {
    switch (t) {
        case RmField_Bool: return reflect::FieldType::Bool;
        case RmField_Int: return reflect::FieldType::Int;
        case RmField_Float: return reflect::FieldType::Float;
        case RmField_String: return reflect::FieldType::String;
        case RmField_Vec2: return reflect::FieldType::Vec2;
        case RmField_Vec3: return reflect::FieldType::Vec3;
        case RmField_Color: return reflect::FieldType::Color;
        case RmField_Enum: return reflect::FieldType::Enum;
        case RmField_Struct: return reflect::FieldType::Struct;
        case RmField_List: return reflect::FieldType::Array;
        case RmField_Asset: return reflect::FieldType::AssetRef;
        default: return reflect::FieldType::EntityRef;
    }
}

const char* HookName(int hook) {
    switch (hook) {
        case RM_HOOK_BEGIN_PLAY: return "BeginPlay";
        case RM_HOOK_TICK: return "Tick";
        case RM_HOOK_RELOADED: return "OnReloaded";
        case RM_HOOK_HIT: return "OnHit";
        case RM_HOOK_TRIGGER_ENTER: return "OnTriggerEnter";
        case RM_HOOK_TRIGGER_EXIT: return "OnTriggerExit";
        default: return "EndPlay";
    }
}

// A script's asset field: char guid[RM_ASSET_ID_SIZE], read and written here (plain bytes).
size_t AssetGet(const void*, const void* field, char* buf, size_t cap) {
    const char* g = static_cast<const char*>(field);
    const size_t n = strnlen(g, RM_ASSET_ID_SIZE - 1);
    if (buf && cap > 0) {
        const size_t k = std::min(n, cap - 1);
        std::memcpy(buf, g, k);
        buf[k] = '\0';
    }
    return n;
}
void AssetSet(const void*, void* field, const char* utf8, size_t len) {
    char* g = static_cast<char*>(field);
    const size_t n = std::min<size_t>(utf8 ? len : 0, RM_ASSET_ID_SIZE - 1);
    if (n > 0) std::memcpy(g, utf8, n);
    std::memset(g + n, 0, RM_ASSET_ID_SIZE - n);
}

// Why the fields `fields` (of a script or a struct) cannot be used; `seen`: structs already looked at.
std::string FieldsProblem(const RmFieldDesc* fields, uint32_t count, std::unordered_set<const RmStructDesc*>& seen, int depth) {
    if (count > 0 && !fields) return "欄の一覧が空です";
    if (depth > 16) return "構造体の入れ子が深すぎます";
    auto element = [&](uint32_t type, const RmFieldDesc& f) -> std::string {
        if (type == RmField_Enum && (!f.enumDesc || !f.enumDesc->get || !f.enumDesc->set || (f.enumDesc->itemCount > 0 && !f.enumDesc->items))) {
            return std::format("欄 {} の列挙の説明がありません", f.name);
        }
        if (type == RmField_Struct) {
            const RmStructDesc* s = f.structDesc;
            if (!s || !s->name || s->size == 0 || !s->construct || !s->destruct || !s->copy) return std::format("欄 {} の構造体の説明がありません", f.name);
            if (s->align > 16) return std::format("構造体 {} は alignas が大きすぎます (16 まで)", s->name);
            if (seen.insert(s).second) {
                if (std::string why = FieldsProblem(s->fields, s->fieldCount, seen, depth + 1); !why.empty()) return why;
            }
        }
        return {};
    };
    for (uint32_t i = 0; i < count; ++i) {
        const RmFieldDesc& f = fields[i];
        if (!f.name || !f.access || !KnownField(f.type)) return "この版のエンジンが知らない欄があります";
        if (f.type == RmField_List) {
            if (!f.list || !f.list->size || !f.list->resize || !f.list->at || !KnownField(f.list->element) || f.list->element == RmField_List) {
                return std::format("欄 {} の一覧の説明がありません", f.name);
            }
            if (std::string why = element(f.list->element, f); !why.empty()) return why;
        } else if (std::string why = element(f.type, f); !why.empty()) {
            return why;
        }
    }
    return {};
}

const reflect::TypeDesc* StructOf(ScriptType& t, const RmStructDesc* s);
const reflect::EnumDesc* EnumOf(ScriptType& t, const RmEnumDesc* e);

// The value of a field (or of a list's elements: `type` is the element's).
reflect::ValueType ValueOf(ScriptType& t, uint32_t type, const RmFieldDesc& in) {
    reflect::ValueType v{FieldTypeOf(type), nullptr, nullptr};
    if (type == RmField_Struct) v.structType = StructOf(t, in.structDesc);
    if (type == RmField_Enum) v.enumType = EnumOf(t, in.enumDesc);
    return v;
}

reflect::FieldDesc MakeField(ScriptType& t, const RmFieldDesc& in) {
    reflect::FieldDesc fd{};
    fd.name = in.name;
    fd.access = in.access;
    fd.transient = (in.flags & RmFieldFlag_Transient) != 0;
    fd.edit = (in.flags & RmFieldFlag_Edit) != 0;
    fd.readOnly = (in.flags & RmFieldFlag_ReadOnly) != 0;
    fd.label = in.label;
    fd.tooltip = in.tooltip;
    fd.hasMin = (in.flags & RmFieldFlag_HasMin) != 0;
    fd.hasMax = (in.flags & RmFieldFlag_HasMax) != 0;
    fd.hasStep = (in.flags & RmFieldFlag_HasStep) != 0;
    fd.min = in.min;
    fd.max = in.max;
    fd.step = in.step;
    fd.line = in.line;
    const uint32_t base = in.type == RmField_List ? in.list->element : in.type;
    if (in.type == RmField_List) {
        auto ops = std::make_unique<reflect::ArrayOps>();
        ops->element = ValueOf(t, base, in);
        ops->size = in.list->size;  // (the DLL's: it owns the vector's memory)
        ops->resize = in.list->resize;
        ops->at = in.list->at;
        fd.value = {reflect::FieldType::Array, nullptr, nullptr};
        fd.array = ops.get();
        t.lists.push_back(std::move(ops));
    } else {
        fd.value = ValueOf(t, base, in);
    }
    // Text: a std::string only through the DLL; an asset's id as plain bytes.
    if (base == RmField_String) fd.stringAccess = &t.strings;
    if (base == RmField_Asset) {
        fd.stringAccess = &t.assets;
        fd.assetKinds = in.assetKinds && *in.assetKinds ? in.assetKinds : nullptr;
    }
    return fd;
}

const reflect::TypeDesc* StructOf(ScriptType& t, const RmStructDesc* s) {
    if (auto it = t.structs.find(s); it != t.structs.end()) return &it->second->desc;
    auto n = std::make_unique<ScriptType::Nested>();
    ScriptType::Nested* at = n.get();
    t.structs.emplace(s, std::move(n));  // first: a struct with a list of itself finds it
    for (uint32_t f = 0; f < s->fieldCount; ++f) at->fields.push_back(MakeField(t, s->fields[f]));
    at->desc = reflect::TypeDesc{s->name,
                                 s->label && *s->label ? s->label : s->name,
                                 reflect::TypeKind::Struct,
                                 static_cast<size_t>(s->size),
                                 std::span<const reflect::FieldDesc>(at->fields),
                                 s->construct,
                                 s->destruct,
                                 s->copy,
                                 &ScriptCppType,
                                 ""};
    return &at->desc;
}

const reflect::EnumDesc* EnumOf(ScriptType& t, const RmEnumDesc* e) {
    if (auto it = t.enums.find(e); it != t.enums.end()) return &it->second->desc;
    auto n = std::make_unique<ScriptType::EnumType>();
    for (uint32_t i = 0; i < e->itemCount; ++i) {
        const RmEnumItem& it = e->items[i];
        n->items.push_back({it.name, it.label ? it.label : it.name, it.value});
    }
    n->desc = reflect::EnumDesc{e->name, std::span<const reflect::EnumItem>(n->items), e->get, e->set, &ScriptCppType};
    const reflect::EnumDesc* out = &n->desc;
    t.enums.emplace(e, std::move(n));
    return out;
}

}  // namespace

// The module's init, guarded (ScriptGuard.cpp).
unsigned long CallModuleInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out);

void ScriptWorld::ReadInput() {
    inputBefore = inputNow;
    inputNow = readInput ? readInput() : InputFrame{};
}

GlobalValue* ScriptWorld::Global(const char* name) {
    if (!name) return nullptr;
    if (GlobalValue* g = globals.Find(name)) return g;
    TellOnce(std::string("global:") + name, std::format("グローバル変数「{}」はゲームの設定の「グローバル変数」にありません", name));
    return nullptr;
}

void ScriptWorld::TellOnce(const std::string& key, std::string_view text) {
    if (told.insert(key).second && log) log(1, text);
}

const InputAction* ScriptWorld::Action(const char* name) {
    if (!name) return nullptr;
    if (const InputAction* a = actions ? FindAction(*actions, name) : nullptr) return a;
    if (told.insert(std::string("input:") + name).second && log) {
        log(1, std::format("入力のアクション「{}」はゲームの設定の「入力」にありません (いつも押されていない扱いです)", name));
    }
    return nullptr;
}

RmApi MakeScriptApi(ScriptWorld& world) {
    RmApi api{};
    api.abi = RM_ABI_VERSION;
    api.ctx = &world;
    api.core = &kCore;
    api.part = &ApiPart;
    return api;
}

const void* FindScriptApiPart(std::span<const ScriptApiPart> parts, const char* name, uint32_t version) {
    if (!name) return nullptr;
    for (const ScriptApiPart& p : parts) {
        if (p.version == version && std::strcmp(p.name, name) == 0) return p.table;
    }
    return nullptr;
}

std::vector<ScriptApiPart> ScriptApiParts(const RuntimeRegistry& registry) {
    std::vector<ScriptApiPart> out;
    for (const auto& c : registry.Contributions<ScriptApiPart>(kScriptApiPoint)) out.push_back(*c.item);
    return out;
}

std::string CheckModule(const RmModule* m, uint32_t engineAbi, bool engineDebug) {
    if (!m) return "スクリプトの DLL がスクリプトを返しませんでした (RmModuleInit)。エンジンと RM.h の版が違うかもしれません。ビルドし直してください";
    if (m->abi != engineAbi) {
        return std::format("スクリプトの DLL の版 ({}) がエンジン ({}) と違います。ビルドし直してください", m->abi, engineAbi);
    }
    if (((m->flags & RmModuleFlag_Debug) != 0) != engineDebug) {
        return engineDebug ? "スクリプトの DLL が Release でビルドされています。このエンジン (Debug) ではビルドし直してから使います"
                           : "スクリプトの DLL が Debug でビルドされています。このエンジン (Release) ではビルドし直してから使います";
    }
    if (!m->stringGet || !m->stringSet) return "スクリプトの DLL に文字の欄を読み書きする関数がありません。ビルドし直してください";
    if (m->scriptCount > 0 && !m->scripts) return "スクリプトの DLL のスクリプトの一覧が空です";
    std::unordered_set<std::string> names;
    for (uint32_t i = 0; i < m->scriptCount; ++i) {
        const RmScriptDesc& s = m->scripts[i];
        if (!s.name || !*s.name) return "名前の無いスクリプトがあります";
        if (!names.insert(s.name).second) return std::format("スクリプト「{}」が 2 つあります", s.name);
        if (s.size == 0 || !s.construct || !s.destruct || !s.copy) return std::format("スクリプト「{}」の作り方がありません", s.name);
        if (s.align > 16) return std::format("スクリプト「{}」は alignas が大きすぎます (16 まで)", s.name);
        if (s.fieldCount > 0 && !s.fields) return std::format("スクリプト「{}」の欄の一覧が空です", s.name);
        std::unordered_set<const RmStructDesc*> seen;
        if (std::string why = FieldsProblem(s.fields, s.fieldCount, seen, 0); !why.empty()) {
            return std::format("スクリプト「{}」: {}", s.name, why);
        }
    }
    return {};
}

std::vector<std::unique_ptr<ScriptType>> MakeScriptTypes(const RmModule& m) {
    std::vector<std::unique_ptr<ScriptType>> out;
    for (uint32_t i = 0; i < m.scriptCount; ++i) {
        const RmScriptDesc& s = m.scripts[i];
        auto t = std::make_unique<ScriptType>();
        t->script = &s;
        // Strings: only through the DLL (its C runtime owns their memory).
        t->strings.ctx = &m;
        t->strings.get = [](const void* ctx, const void* field, char* buf, size_t cap) -> size_t {
            return static_cast<size_t>(static_cast<const RmModule*>(ctx)->stringGet(field, buf, cap));
        };
        t->strings.set = [](const void* ctx, void* field, const char* utf8, size_t len) {
            static_cast<const RmModule*>(ctx)->stringSet(field, utf8, len);
        };
        t->assets = {nullptr, &AssetGet, &AssetSet};
        for (uint32_t f = 0; f < s.fieldCount; ++f) t->fields.push_back(MakeField(*t, s.fields[f]));
        t->desc = reflect::TypeDesc{s.name,
                                    s.label && *s.label ? s.label : s.name,
                                    reflect::TypeKind::Script,
                                    static_cast<size_t>(s.size),
                                    std::span<const reflect::FieldDesc>(t->fields),
                                    s.construct,
                                    s.destruct,
                                    s.copy,
                                    &ScriptCppType,
                                    s.header ? s.header : ""};
        out.push_back(std::move(t));
    }
    return out;
}

bool ScriptModule::Load(const std::filesystem::path& dll, const RmApi& api, std::string* why) {
    Unload();
    auto fail = [&](std::string msg) {
        if (why) *why = std::move(msg);
        Unload();
        return false;
    };
    HMODULE h = LoadLibraryW(dll.c_str());
    if (!h) return fail(std::format("スクリプトの DLL ({}) を読めませんでした (エラー {})", PathToUtf8(dll.filename()), GetLastError()));
    library_ = h;
    path_ = dll;
    const auto init = reinterpret_cast<RmModuleInitFn>(reinterpret_cast<void*>(GetProcAddress(h, "RmModuleInit")));
    if (!init) return fail("スクリプトの DLL に RmModuleInit がありません (RM Engine のスクリプトの DLL ではありません)");
    const RmModule* m = nullptr;
    if (const unsigned long code = CallModuleInit(init, &api, RM_ABI_VERSION, &m); code != 0) {
        return fail(std::format("スクリプトの DLL を始めるところで止まりました (例外 0x{:08X})", code));
    }
    if (std::string problem = CheckModule(m); !problem.empty()) return fail(problem);
    module_ = m;
    types_ = MakeScriptTypes(*m);
    return true;
}

void ScriptModule::Unload() {
    types_.clear();
    module_ = nullptr;
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
    path_.clear();
}

void ScriptModule::Adopt(const RmModule& module) {
    Unload();
    module_ = &module;
    types_ = MakeScriptTypes(module);
}

std::vector<const reflect::TypeDesc*> ScriptModule::Types() const {
    std::vector<const reflect::TypeDesc*> out;
    for (const auto& t : types_) out.push_back(&t->desc);
    return out;
}

const ScriptType* ScriptModule::Find(std::string_view name) const {
    for (const auto& t : types_) {
        if (name == t->desc.name) return t.get();
    }
    return nullptr;
}

int ScriptDllNumber(std::wstring_view f) {
    constexpr std::wstring_view pre = L"Scripts_", ext = L".dll";
    if (f.size() <= pre.size() + ext.size() || f.substr(0, pre.size()) != pre || f.substr(f.size() - ext.size()) != ext) return -1;
    const std::wstring_view digits = f.substr(pre.size(), f.size() - pre.size() - ext.size());
    int n = 0;
    for (const wchar_t c : digits) {
        if (c < L'0' || c > L'9' || n > 100000000) return -1;
        n = n * 10 + (c - L'0');
    }
    return n;
}

std::filesystem::path LatestScriptDll(const std::filesystem::path& folder) {
    std::filesystem::path best;
    int bestN = -1;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const int n = ScriptDllNumber(it->path().filename().native());
        if (n > bestN) {
            bestN = n;
            best = it->path();
        }
    }
    return best;
}

int ScriptOrderOf(std::string_view type, std::span<const ScriptOrderEntry> order) {
    for (const ScriptOrderEntry& e : order) {
        if (e.type == type) return e.order;
    }
    return 0;
}

std::vector<std::string> SortScriptTypes(std::vector<std::string> types, std::span<const ScriptOrderEntry> order) {
    std::sort(types.begin(), types.end(), [&](const std::string& a, const std::string& b) {
        const int oa = ScriptOrderOf(a, order), ob = ScriptOrderOf(b, order);
        return oa != ob ? oa < ob : a < b;
    });
    types.erase(std::unique(types.begin(), types.end()), types.end());
    return types;
}

std::vector<ScriptRunner::Instance> ScriptRunner::Collect(Scene& scene, const ScriptModule& module,
                                                          std::span<const ScriptOrderEntry> order) const {
    std::vector<Instance> out;
    std::vector<std::string> present;
    for (const TreeEntry& t : scene.TreeOrder()) {
        SceneEntity* e = scene.Find(t.entity->data.id);
        for (Component& c : e->components) {
            if (!c.Known()) continue;
            if (const ScriptType* type = module.Find(c.type)) {
                out.push_back({e->data.id, type, c.value.Get(), t.activeInHierarchy});
                present.push_back(c.type);
            }
        }
    }
    const std::vector<std::string> sorted = SortScriptTypes(present, order);
    std::unordered_map<std::string_view, size_t> rank;
    for (size_t i = 0; i < sorted.size(); ++i) rank[sorted[i]] = i;
    std::stable_sort(out.begin(), out.end(), [&](const Instance& a, const Instance& b) {
        const size_t ra = rank[a.type->desc.name], rb = rank[b.type->desc.name];
        return ra != rb ? ra < rb : a.entity < b.entity;  // (same entity: the order they are attached)
    });
    return out;
}

bool ScriptRunner::Call(ScriptWorld& world, const Instance& in, int hook, float dt, int other) {
    const unsigned long code =
        CallScriptHook(*in.type->script, hook, in.data, static_cast<RmEntityId>(in.entity), dt, static_cast<RmEntityId>(std::max(other, 0)));
    if (code == 0) return true;
    crashed_.insert(in.data);
    if (world.log) {
        const SceneEntity* e = world.scene ? world.scene->Find(in.entity) : nullptr;
        world.log(2, std::format("スクリプト「{}」(エンティティ「{}」) が {} の中で止まりました (例外 0x{:08X})。このスクリプトだけを止めます",
                                 in.type->desc.label, e ? e->data.name : std::string(), HookName(hook), code));
    }
    return false;
}

void ScriptRunner::Frame(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order, float dt) {
    world.scene = &scene;
    world.dt = dt;
    world.ReadInput();
    const std::vector<Instance> list = Collect(scene, module, order);
    std::unordered_set<void*> present;
    for (const Instance& in : list) present.insert(in.data);
    // Switched off since the last frame: EndPlay (BeginPlay again when switched on).
    for (const Instance& in : list) {
        if (in.active || !started_.contains(in.data)) continue;
        if (!crashed_.contains(in.data)) Call(world, in, 2, 0);
        started_.erase(in.data);
    }
    // Gone with its entity: its object is gone too (nothing left to call).
    std::erase_if(started_, [&](void* p) { return !present.contains(p); });
    std::erase_if(crashed_, [&](void* p) { return !present.contains(p); });
    // BeginPlay of the new ones first (also what the last frame made), then every Tick.
    for (const Instance& in : list) {
        if (!in.active || started_.contains(in.data) || crashed_.contains(in.data)) continue;
        started_.insert(in.data);
        Call(world, in, 0, 0);
    }
    for (const Instance& in : list) {
        if (!in.active || crashed_.contains(in.data) || !started_.contains(in.data)) continue;
        Call(world, in, 1, dt);
    }
    ApplyAsked(scene, world, module, order);  // the frame's end: what was destroyed / removed goes
    world.time += dt;
}

void ScriptRunner::Deliver(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                           std::span<const HookCall> calls) {
    if (calls.empty() || !module.Loaded()) return;
    world.scene = &scene;
    const std::vector<Instance> list = Collect(scene, module, order);
    for (const HookCall& h : calls) {
        for (const Instance& in : list) {
            // Only what BeginPlay was called on (and EndPlay was not): the hooks come between the two.
            if (in.entity != h.entity || !in.active || !started_.contains(in.data) || crashed_.contains(in.data)) continue;
            Call(world, in, h.hook, 0, h.other);
        }
    }
    ApplyAsked(scene, world, module, order);
}

void ScriptRunner::ApplyAsked(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order) {
    for (int round = 0; round < 8 && (!world.destroyAsked.empty() || !world.removeAsked.empty()); ++round) {
        const std::vector<int> destroy = std::exchange(world.destroyAsked, {});
        const std::vector<std::pair<int, const void*>> remove = std::exchange(world.removeAsked, {});
        std::vector<int> roots;
        for (const int id : destroy) {
            if (scene.Find(id) && std::find(roots.begin(), roots.end(), id) == roots.end()) roots.push_back(id);
        }
        const std::unordered_set<int> gone = scene.Subtree(roots);
        std::unordered_set<const void*> goneObjects;
        for (const auto& [id, object] : remove) goneObjects.insert(object);
        // EndPlay of the scripts that go (a script started only: BeginPlay and EndPlay in pairs).
        for (const Instance& in : Collect(scene, module, order)) {
            if (!gone.contains(in.entity) && !goneObjects.contains(in.data)) continue;
            if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 2, 0);
            started_.erase(in.data);  // (its memory may hold a new script next: never taken for this one)
            crashed_.erase(in.data);
        }
        for (const auto& [id, object] : remove) {
            SceneEntity* e = scene.Find(id);
            if (!e || gone.contains(id)) continue;
            for (const Component& c : e->components) {
                if (c.value.Get() != object) continue;
                scene.RemoveComponent(id, c.type, e->NthOf(c));
                break;
            }
        }
        if (!roots.empty()) scene.Delete(roots);
    }
    world.destroyAsked.clear();
    world.removeAsked.clear();
}

std::vector<ScriptRunner::RunState> ScriptRunner::Remember(const Scene& scene) const {
    std::vector<RunState> out;
    for (const SceneEntity& e : scene.Entities()) {
        for (const Component& c : e.components) {
            void* data = const_cast<void*>(c.value.Get());
            const bool started = data && started_.contains(data), crashed = data && crashed_.contains(data);
            if (started || crashed) out.push_back({e.data.id, c.type, e.NthOf(c), started, crashed});
        }
    }
    return out;
}

void ScriptRunner::Resume(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order,
                          std::span<const RunState> before) {
    Reset();
    world.scene = &scene;
    for (const RunState& r : before) {
        SceneEntity* e = scene.Find(r.entity);
        Component* c = e ? e->FindComponent(r.type, r.nth) : nullptr;
        if (!c || !c->Known() || !module.Find(r.type)) continue;  // (a script that went: kept as a missing one)
        if (r.started) started_.insert(c->value.Get());
        if (r.crashed) crashed_.insert(c->value.Get());
    }
    for (const Instance& in : Collect(scene, module, order)) {
        if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 3, 0);
    }
}

void ScriptRunner::Stop(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order) {
    world.scene = &scene;
    for (const Instance& in : Collect(scene, module, order)) {
        if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 2, 0);
    }
    Reset();
    world.destroyAsked.clear();  // (the scene goes as a whole)
    world.removeAsked.clear();
    // The scene that played is thrown away next: a script's default value that asks the world (a
    // script made when a component is added, or when scripts are swapped) must not see it.
    world.scene = nullptr;
}

SceneSwitch SwitchAskedScene(Scene& playing, ScriptWorld& world, ScriptRunner& runner, const ScriptModule& module,
                             std::span<const ScriptOrderEntry> order, const std::function<bool(size_t index, Scene& into, std::string* error)>& load,
                             std::string* error) {
    const int asked = std::exchange(world.sceneAsked, -1);
    if (asked < 0) return SceneSwitch::None;
    // Read first: a scene that cannot be read leaves the one playing as it is (its scripts go on).
    Scene next;
    next.SetComponentTypes(playing.ComponentTypes(), playing.MultipleTypes());
    if (!load(static_cast<size_t>(asked), next, error)) return SceneSwitch::Failed;
    if (module.Loaded()) runner.Stop(playing, world, module, order);  // EndPlay of this scene's scripts
    runner.Reset();
    playing = std::move(next);
    world.sceneIndex = asked;
    world.sceneAsked = -1;  // (asked again in an EndPlay: not taken - the switch is this one)
    world.scene = &playing;
    return SceneSwitch::Switched;
}

void ScriptRunner::Reset() {
    started_.clear();
    crashed_.clear();
}

}  // namespace rm::game_production
