#include "rm_engine/game_production/runtime/Scripts.h"

#include "base/Utf.h"
#include "platform/Win.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <system_error>

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
int ApiKeyDown(void* ctx, int vk) { return vk > 0 && vk < 256 && W(ctx).keysNow[static_cast<size_t>(vk)] ? 1 : 0; }
int ApiKeyPressed(void* ctx, int vk) {
    return vk > 0 && vk < 256 && W(ctx).keysNow[static_cast<size_t>(vk)] && !W(ctx).keysBefore[static_cast<size_t>(vk)] ? 1 : 0;
}
int ApiKeyReleased(void* ctx, int vk) {
    return vk > 0 && vk < 256 && !W(ctx).keysNow[static_cast<size_t>(vk)] && W(ctx).keysBefore[static_cast<size_t>(vk)] ? 1 : 0;
}
RmEntityId ApiCamera(void* ctx) { return W(ctx).camera; }
void ApiSetCamera(void* ctx, RmEntityId e) { W(ctx).camera = e; }
void* ApiGetScript(void* ctx, RmEntityId e, const char* typeName, uint32_t nth) {
    SceneEntity* x = EntityOf(ctx, e);
    Component* c = x && typeName ? x->FindComponent(typeName, nth) : nullptr;
    return c && c->Known() ? c->value.Get() : nullptr;
}
const void* ApiPart(void* ctx, const char* name, uint32_t version) { return FindScriptApiPart(W(ctx).parts, name, version); }

constexpr RmCoreApi kCore{&ApiLog,        &ApiDeltaTime, &ApiTime,         &ApiFindByName,  &ApiExists,
                          &ApiIsActive,   &ApiSetActive, &ApiGetName,      &ApiParent,      &ApiKeyDown,
                          &ApiKeyPressed, &ApiKeyReleased, &ApiCamera,     &ApiSetCamera,   &ApiGetScript};

// The C++ type of every script, as far as the engine is concerned (a script's own type lives in the DLL).
struct ScriptTag {};
const std::type_info& ScriptCppType() { return typeid(ScriptTag); }

bool KnownField(uint32_t type) { return type <= RmField_EntityRef; }

reflect::FieldType FieldTypeOf(uint32_t t) {
    switch (t) {
        case RmField_Bool: return reflect::FieldType::Bool;
        case RmField_Int: return reflect::FieldType::Int;
        case RmField_Float: return reflect::FieldType::Float;
        case RmField_String: return reflect::FieldType::String;
        case RmField_Vec2: return reflect::FieldType::Vec2;
        case RmField_Vec3: return reflect::FieldType::Vec3;
        case RmField_Color: return reflect::FieldType::Color;
        default: return reflect::FieldType::EntityRef;
    }
}

const char* HookName(int hook) { return hook == 0 ? "BeginPlay" : hook == 1 ? "Tick" : "EndPlay"; }

}  // namespace

// The module's init, guarded (ScriptGuard.cpp).
unsigned long CallModuleInit(RmModuleInitFn init, const RmApi* api, uint32_t abi, const RmModule** out);

void ScriptWorld::ReadKeys() {
    keysBefore = keysNow;
    for (int vk = 1; vk < 256; ++vk) keysNow[static_cast<size_t>(vk)] = keyDown && keyDown(vk) ? 1 : 0;
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

std::string CheckModule(const RmModule* m, uint32_t engineAbi) {
    if (!m) return "スクリプトの DLL がスクリプトを返しませんでした (RmModuleInit)。エンジンと RM.h の版が違うかもしれません。ビルドし直してください";
    if (m->abi != engineAbi) {
        return std::format("スクリプトの DLL の版 ({}) がエンジン ({}) と違います。ビルドし直してください", m->abi, engineAbi);
    }
    if (m->scriptCount > 0 && !m->scripts) return "スクリプトの DLL のスクリプトの一覧が空です";
    std::unordered_set<std::string> names;
    for (uint32_t i = 0; i < m->scriptCount; ++i) {
        const RmScriptDesc& s = m->scripts[i];
        if (!s.name || !*s.name) return "名前の無いスクリプトがあります";
        if (!names.insert(s.name).second) return std::format("スクリプト「{}」が 2 つあります", s.name);
        if (s.size == 0 || !s.construct || !s.destruct || !s.copy) return std::format("スクリプト「{}」の作り方がありません", s.name);
        if (s.align > 16) return std::format("スクリプト「{}」は alignas が大きすぎます (16 まで)", s.name);
        if (s.fieldCount > 0 && !s.fields) return std::format("スクリプト「{}」の欄の一覧が空です", s.name);
        for (uint32_t f = 0; f < s.fieldCount; ++f) {
            if (!s.fields[f].name || !s.fields[f].access || !KnownField(s.fields[f].type)) {
                return std::format("スクリプト「{}」に、この版のエンジンが知らない欄があります", s.name);
            }
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
        for (uint32_t f = 0; f < s.fieldCount; ++f) {
            const RmFieldDesc& in = s.fields[f];
            reflect::FieldDesc fd{};
            fd.name = in.name;
            fd.value = {FieldTypeOf(in.type), nullptr, nullptr};
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
            t->fields.push_back(fd);
        }
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
        if (!t.activeInHierarchy) continue;
        SceneEntity* e = scene.Find(t.entity->data.id);
        for (Component& c : e->components) {
            if (!c.Known()) continue;
            if (const ScriptType* type = module.Find(c.type)) {
                out.push_back({e->data.id, type, c.value.Get()});
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

bool ScriptRunner::Call(ScriptWorld& world, const Instance& in, int hook, float dt) {
    const unsigned long code = CallScriptHook(*in.type->script, hook, in.data, static_cast<RmEntityId>(in.entity), dt);
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
    world.ReadKeys();
    const std::vector<Instance> list = Collect(scene, module, order);
    // Forget what is gone (a deleted entity, a switched-off one: started again when it comes back).
    std::unordered_set<void*> present;
    for (const Instance& in : list) present.insert(in.data);
    std::erase_if(started_, [&](void* p) { return !present.contains(p); });
    // BeginPlay of the new ones first (also what the last frame made), then every Tick.
    for (const Instance& in : list) {
        if (started_.contains(in.data) || crashed_.contains(in.data)) continue;
        started_.insert(in.data);
        Call(world, in, 0, 0);
    }
    for (const Instance& in : list) {
        if (crashed_.contains(in.data) || !started_.contains(in.data)) continue;
        Call(world, in, 1, dt);
    }
    world.time += dt;
}

void ScriptRunner::Stop(Scene& scene, ScriptWorld& world, const ScriptModule& module, std::span<const ScriptOrderEntry> order) {
    world.scene = &scene;
    for (const Instance& in : Collect(scene, module, order)) {
        if (started_.contains(in.data) && !crashed_.contains(in.data)) Call(world, in, 2, 0);
    }
    Reset();
}

void ScriptRunner::Reset() {
    started_.clear();
    crashed_.clear();
}

}  // namespace rm::game_production
