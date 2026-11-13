// Tests for the rest of C++ scripts (ABI 4): the new kinds of fields (an enum, a list, a nested struct,
// an asset's id - saved, read back, compared, strings only through the module), making and destroying
// entities from a script (made at once, started at the next frame; destroyed at the frame's end after
// EndPlay, children too; an id of something gone does nothing), components added / got / removed, and
// a prefab spawned. A module is built here by hand (the tables rmgen writes into a script DLL).

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

using namespace rm;
using namespace rm::game_production;

namespace {

std::string g_log;
const RmApi* g_api = nullptr;
int g_strings = 0;

template <class T>
void Construct(void* p) { new (p) T(); }
template <class T>
void Destruct(void* p) { static_cast<T*>(p)->~T(); }
template <class T>
void Copy(void* d, const void* s) { *static_cast<T*>(d) = *static_cast<const T*>(s); }
template <class V>
uint64_t VecSize(const void* v) { return static_cast<const V*>(v)->size(); }
template <class V>
void VecResize(void* v, uint64_t n) { static_cast<V*>(v)->resize(static_cast<size_t>(n)); }
template <class V>
void* VecAt(void* v, uint64_t i) { return &(*static_cast<V*>(v))[static_cast<size_t>(i)]; }

uint64_t StringGet(const void* field, char* buf, uint64_t cap) {
    ++g_strings;
    const std::string& s = *static_cast<const std::string*>(field);
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(s.size(), static_cast<size_t>(cap - 1));
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
    }
    return s.size();
}
void StringSet(void* field, const char* utf8, uint64_t len) {
    ++g_strings;
    static_cast<std::string*>(field)->assign(utf8, static_cast<size_t>(len));
}

// --- A script with every new kind of field ------------------------------------------------------
enum class Mode : int { Walk = 0, Run = 5 };
struct Wave {
    int count = 3;
    std::string tag = "w";
};
struct Rich {
    Mode mode = Mode::Walk;
    std::vector<int> numbers{1, 2};
    std::vector<std::string> names{"a"};
    Wave wave;
    std::vector<Wave> waves;
    char prefab[RM_ASSET_ID_SIZE] = {};  // rm::PrefabRef
};

int64_t ModeGet(const void* p) { return static_cast<int64_t>(*static_cast<const Mode*>(p)); }
void ModeSet(void* p, int64_t v) { *static_cast<Mode*>(p) = static_cast<Mode>(v); }
const RmEnumItem kModeItems[] = {{"Walk", "歩く", 0}, {"Run", nullptr, 5}};
const RmEnumDesc kMode{"test::Mode", kModeItems, 2, &ModeGet, &ModeSet};

const RmFieldDesc kWaveFields[] = {
    {"count", RmField_Int, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Wave*>(o)->count; }, nullptr, nullptr, 0, 0, 0, 1},
    {"tag", RmField_String, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Wave*>(o)->tag; }, nullptr, nullptr, 0, 0, 0, 2},
};
const RmStructDesc kWave{"test::Wave", "波", sizeof(Wave), alignof(Wave), kWaveFields, 2, &Construct<Wave>, &Destruct<Wave>, &Copy<Wave>};
const RmListDesc kInts{RmField_Int, 0, &VecSize<std::vector<int>>, &VecResize<std::vector<int>>, &VecAt<std::vector<int>>};
const RmListDesc kNames{RmField_String, 0, &VecSize<std::vector<std::string>>, &VecResize<std::vector<std::string>>, &VecAt<std::vector<std::string>>};
const RmListDesc kWaves{RmField_Struct, 0, &VecSize<std::vector<Wave>>, &VecResize<std::vector<Wave>>, &VecAt<std::vector<Wave>>};

const RmFieldDesc kRichFields[] = {
    {"mode", RmField_Enum, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Rich*>(o)->mode; }, nullptr, nullptr, 0, 0, 0, 1, &kMode},
    {"numbers", RmField_List, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Rich*>(o)->numbers; }, nullptr, nullptr, 0, 0, 0, 2,
     nullptr, nullptr, &kInts},
    {"names", RmField_List, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Rich*>(o)->names; }, nullptr, nullptr, 0, 0, 0, 3,
     nullptr, nullptr, &kNames},
    {"wave", RmField_Struct, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Rich*>(o)->wave; }, nullptr, nullptr, 0, 0, 0, 4,
     nullptr, &kWave},
    {"waves", RmField_List, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Rich*>(o)->waves; }, nullptr, nullptr, 0, 0, 0, 5,
     nullptr, &kWave, &kWaves},
    {"prefab", RmField_Asset, RmFieldFlag_Edit, [](void* o) -> void* { return static_cast<Rich*>(o)->prefab; }, nullptr, nullptr, 0, 0, 0, 6,
     nullptr, nullptr, nullptr, ".rmprefab"},
};

// --- Scripts that make, destroy and count ----------------------------------------------------------
struct Counter {
    int ticks = 0;
};
void CounterBegin(void*, RmEntityId e) { g_log += "B" + std::to_string(e); }
void CounterTick(void* s, RmEntityId e, float) {
    ++static_cast<Counter*>(s)->ticks;
    g_log += "T" + std::to_string(e);
}
void CounterEnd(void*, RmEntityId e) { g_log += "E" + std::to_string(e); }
void CounterReloaded(void*, RmEntityId e) { g_log += "R" + std::to_string(e); }
const RmFieldDesc kCounterFields[] = {
    {"ticks", RmField_Int, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Counter*>(o)->ticks; }, nullptr, nullptr, 0, 0, 0, 1}};

struct Spawner {
    int frame = 0;
};
RmEntityId g_made = 0;
void SpawnerTick(void* s, RmEntityId, float) {
    if (++static_cast<Spawner*>(s)->frame != 1) return;
    const RmCoreApi& c = *g_api->core;
    void* ctx = g_api->ctx;
    g_log += "S";
    // Made now (set up at once), started next frame; the target (and what is under it) at the frame's end.
    g_made = c.spawn(ctx, "Bullet", 0);
    c.addComponent(ctx, g_made, "test::Counter");
    c.destroy(ctx, c.findByName(ctx, "Target"));
    // Made and destroyed in the same frame: never started, never ended.
    const RmEntityId brief = c.spawn(ctx, "Brief", 0);
    c.addComponent(ctx, brief, "test::Counter");
    c.destroy(ctx, brief);
}
const RmFieldDesc kSpawnerFields[] = {
    {"frame", RmField_Int, 0, [](void* o) -> void* { return &static_cast<Spawner*>(o)->frame; }, nullptr, nullptr, 0, 0, 0, 1}};

const RmScriptDesc kScripts[] = {
    {"test::Rich", "いろいろ", "Rich.h", sizeof(Rich), alignof(Rich), kRichFields, 6, &Construct<Rich>, &Destruct<Rich>, &Copy<Rich>},
    {"test::Counter", "数える", "Counter.h", sizeof(Counter), alignof(Counter), kCounterFields, 1, &Construct<Counter>, &Destruct<Counter>,
     &Copy<Counter>, &CounterBegin, &CounterTick, &CounterEnd, &CounterReloaded},
    {"Spawner", "作る", "Spawner.h", sizeof(Spawner), alignof(Spawner), kSpawnerFields, 1, &Construct<Spawner>, &Destruct<Spawner>, &Copy<Spawner>,
     nullptr, &SpawnerTick, nullptr},
};
constexpr uint32_t kFlags = kEngineDebug ? RmModuleFlag_Debug : 0;
const RmModule kModule{RM_ABI_VERSION, kFlags, 3, kScripts, &StringGet, &StringSet};

Scene SceneWith(const ScriptModule& m, bool counterMany = true) {
    Scene s;
    const auto types = m.Types();
    std::vector<const reflect::TypeDesc*> many;
    if (counterMany) many.push_back(&m.Find("test::Counter")->desc);
    s.SetComponentTypes(types, many);
    return s;
}

std::string Id(int e) { return std::to_string(e); }

}  // namespace

RM_TEST(ScriptsMore_NewFieldTypes_SavedReadBackCompared) {
    ScriptModule m;
    m.Adopt(kModule);
    const ScriptType* t = m.Find("test::Rich");
    CHECK(t != nullptr);
    if (!t) return;
    const reflect::TypeDesc& d = t->desc;
    reflect::Object o(d);
    g_strings = 0;
    json::Value j = reflect::Save(d, o.Get());
    CHECK(j.Get("mode").AsString() == "Walk" && j.Get("numbers").Size() == 2 && j.Get("numbers").At(1).AsInt() == 2);
    CHECK(j.Get("names").At(0).AsString() == "a" && g_strings > 0);  // (a string of a list: through the module)
    CHECK(j.Get("wave").Get("count").AsInt() == 3 && j.Get("wave").Get("tag").AsString() == "w" && j.Get("waves").Size() == 0);
    CHECK(j.Get("prefab").AsString().empty());

    const std::string guid = "0123456789abcdef0123456789abcdef";
    j.Set("mode", json::Value("Run"));
    json::Value nums = json::Value::MakeArray();
    for (int i : {4, 5, 6}) nums.Push(i);
    j.Set("numbers", nums);
    json::Value names = json::Value::MakeArray();
    names.Push("x");
    names.Push(std::string(40, 'y'));  // longer than the small-string buffer: the DLL allocates it
    j.Set("names", names);
    json::Value wave = j.Get("wave");
    wave.Set("count", json::Value(9));
    j.Set("wave", wave);
    json::Value waves = json::Value::MakeArray();
    json::Value one = json::Value::MakeObject();
    one.Set("count", json::Value(1));
    one.Set("tag", json::Value("first"));
    waves.Push(one);
    j.Set("waves", waves);
    j.Set("prefab", json::Value(guid));
    reflect::Object back(d);
    reflect::Warnings warnings;
    reflect::Load(d, back.Get(), j, nullptr, &warnings);
    const Rich& r = *static_cast<const Rich*>(back.Get());
    CHECK(r.mode == Mode::Run && (r.numbers == std::vector<int>{4, 5, 6}) && r.names.size() == 2 && r.names[1] == std::string(40, 'y'));
    CHECK(r.wave.count == 9 && r.wave.tag == "w" && r.waves.size() == 1 && r.waves[0].count == 1 && r.waves[0].tag == "first");
    CHECK(std::string(r.prefab) == guid && warnings.empty());
    CHECK(!reflect::Equals(d, o.Get(), back.Get()));
    CHECK(reflect::Save(d, back.Get()) == j);  // written back the same
    // An enum name this version does not know: kept and written back.
    json::Value newer = j;
    newer.Set("mode", json::Value("Fly"));
    reflect::Object n(d);
    reflect::Unknowns u;
    reflect::Load(d, n.Get(), newer, &u, &warnings);
    CHECK(reflect::Save(d, n.Get(), &u).Get("mode").AsString() == "Fly");
    // The asset field takes the kinds of file its type says.
    CHECK(d.fields[5].value.type == reflect::FieldType::AssetRef && std::string(d.fields[5].assetKinds) == ".rmprefab");
    CHECK(d.fields[1].value.type == reflect::FieldType::Array && d.fields[1].array->element.type == reflect::FieldType::Int);
    CHECK(d.fields[0].value.enumType && d.fields[0].value.enumType->items.size() == 2 && std::string(d.fields[0].value.enumType->items[0].label) == "歩く");

    // Carried over by name to the scripts built again (a scene saved and read with another module).
    Scene s = SceneWith(m);
    const int e = s.Create("A");
    s.AddComponent(e, d);
    reflect::Load(d, s.Find(e)->FindComponent("test::Rich")->value.Get(), j, nullptr, nullptr);
    const json::Value saved = s.Save();
    ScriptModule again;
    again.Adopt(kModule);
    Scene s2 = SceneWith(again);
    s2.Load(saved);
    const Rich& r2 = *static_cast<const Rich*>(s2.Find(e)->FindComponent("test::Rich")->value.Get());
    CHECK(r2.mode == Mode::Run && r2.waves.size() == 1 && r2.names[1] == std::string(40, 'y') && std::string(r2.prefab) == guid);
}

RM_TEST(ScriptsMore_CheckModuleWantsTheDescriptions) {
    RmFieldDesc noEnum = kRichFields[0];
    noEnum.enumDesc = nullptr;
    const RmScriptDesc a{"A", "A", "A.h", sizeof(Rich), alignof(Rich), &noEnum, 1, &Construct<Rich>, &Destruct<Rich>, &Copy<Rich>};
    const RmModule ma{RM_ABI_VERSION, kFlags, 1, &a, &StringGet, &StringSet};
    CHECK(!CheckModule(&ma).empty());
    RmFieldDesc noList = kRichFields[1];
    noList.list = nullptr;
    const RmScriptDesc b{"B", "B", "B.h", sizeof(Rich), alignof(Rich), &noList, 1, &Construct<Rich>, &Destruct<Rich>, &Copy<Rich>};
    const RmModule mb{RM_ABI_VERSION, kFlags, 1, &b, &StringGet, &StringSet};
    CHECK(!CheckModule(&mb).empty());
    static const RmListDesc listOfLists{RmField_List, 0, &VecSize<std::vector<int>>, &VecResize<std::vector<int>>, &VecAt<std::vector<int>>};
    RmFieldDesc nested = kRichFields[1];
    nested.list = &listOfLists;
    const RmScriptDesc c{"C", "C", "C.h", sizeof(Rich), alignof(Rich), &nested, 1, &Construct<Rich>, &Destruct<Rich>, &Copy<Rich>};
    const RmModule mc{RM_ABI_VERSION, kFlags, 1, &c, &StringGet, &StringSet};
    CHECK(!CheckModule(&mc).empty());
    CHECK(CheckModule(&kModule).empty());
}

RM_TEST(ScriptsMore_SpawnAndDestroy_AtTheFrameBoundary) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int spawner = s.Create("Spawner");
    s.AddComponent(spawner, m.Find("Spawner")->desc);
    const int target = s.Create("Target");
    s.AddComponent(target, m.Find("test::Counter")->desc);
    const int child = s.Create("Child", target);
    s.AddComponent(child, m.Find("test::Counter")->desc);
    ScriptWorld w;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    const RmApi api = MakeScriptApi(w);
    g_api = &api;
    ScriptRunner r;
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    // Spawner (by name, first) asks; the target still ticks this frame; at its end EndPlay of the target
    // and its child, then they go. The bullet is there already, not started.
    CHECK(g_log == "B" + Id(target) + "B" + Id(child) + "ST" + Id(target) + "T" + Id(child) + "E" + Id(target) + "E" + Id(child));
    CHECK(!s.Find(target) && !s.Find(child) && s.Find(static_cast<int>(g_made)) && s.Find(static_cast<int>(g_made))->data.name == "Bullet");
    CHECK(std::none_of(s.Entities().begin(), s.Entities().end(), [](const SceneEntity& e) { return e.data.name == "Brief"; }));
    // An id of something gone: nothing happens (and nothing breaks).
    const RmCoreApi& c = *api.core;
    char name[8] = "x";
    CHECK(c.exists(api.ctx, target) == 0 && c.getName(api.ctx, target, name, sizeof(name)) == 0 && c.parent(api.ctx, child) == 0);
    c.setActive(api.ctx, target, 0);
    c.destroy(api.ctx, target);
    CHECK(c.addComponent(api.ctx, target, "test::Counter") == nullptr && c.countComponents(api.ctx, target, "test::Counter") == 0);
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "B" + std::to_string(g_made) + "T" + std::to_string(g_made));  // the bullet: BeginPlay before its first Tick
    // Stop: EndPlay of the bullet; nothing asked is left over.
    g_log.clear();
    c.destroy(api.ctx, g_made);
    r.Stop(s, w, m, {});
    CHECK(g_log == "E" + std::to_string(g_made) && w.destroyAsked.empty());
    // Spawning under a parent; a parent that is gone: at the top.
    w.scene = &s;
    const RmEntityId under = c.spawn(api.ctx, "Under", spawner);
    const RmEntityId top = c.spawn(api.ctx, "Top", target);
    CHECK(c.parent(api.ctx, under) == static_cast<RmEntityId>(spawner) && c.parent(api.ctx, top) == 0);
    w.scene = nullptr;
    CHECK(c.spawn(api.ctx, "Nowhere", 0) == 0);  // not playing: nothing
    g_api = nullptr;
}

RM_TEST(ScriptsMore_AddGetRemoveComponents) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int e = s.Create("E");
    ScriptWorld w;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    w.scene = &s;
    const RmApi api = MakeScriptApi(w);
    const RmCoreApi& c = *api.core;
    void* first = c.addComponent(api.ctx, e, "test::Counter");
    void* second = c.addComponent(api.ctx, e, "test::Counter");
    CHECK(first && second && first != second && c.countComponents(api.ctx, e, "test::Counter") == 2);
    CHECK(c.getScript(api.ctx, e, "test::Counter", 1) == second);
    // One that cannot be twice: the one there.
    void* rich = c.addComponent(api.ctx, e, "test::Rich");
    CHECK(rich && c.addComponent(api.ctx, e, "test::Rich") == rich && c.countComponents(api.ctx, e, "test::Rich") == 1);
    CHECK(c.addComponent(api.ctx, e, "NoSuchType") == nullptr && told.find("NoSuchType") != std::string::npos);
    ScriptRunner r;
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "B" + Id(e) + "B" + Id(e) + "T" + Id(e) + "T" + Id(e));
    // Removed: still there until the frame's end, EndPlay first.
    CHECK(c.removeComponent(api.ctx, e, "test::Counter", 0) == 1 && c.countComponents(api.ctx, e, "test::Counter") == 2);
    CHECK(c.removeComponent(api.ctx, e, "test::Counter", 5) == 0);
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "T" + Id(e) + "T" + Id(e) + "E" + Id(e));
    CHECK(c.countComponents(api.ctx, e, "test::Counter") == 1 && c.getScript(api.ctx, e, "test::Counter", 0) == second);
    CHECK(static_cast<Counter*>(second)->ticks == 2);
    r.Stop(s, w, m, {});
}

RM_TEST(ScriptsMore_SpawnAPrefab) {
    ScriptModule m;
    m.Adopt(kModule);
    // The prefab: an enemy with a gun that counts.
    Scene src = SceneWith(m);
    const int enemy = src.Create("Enemy");
    const int gun = src.Create("Gun", enemy);
    src.AddComponent(gun, m.Find("test::Counter")->desc);
    const json::Value prefab = MakePrefab(src, enemy);
    const std::string guid = "aaaabbbbccccddddaaaabbbbccccdddd";

    Scene s = SceneWith(m);
    ScriptWorld w;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    w.prefab = [&](const AssetRef& r) -> const json::Value* { return r.guid == guid ? &prefab : nullptr; };
    w.scene = &s;
    const RmApi api = MakeScriptApi(w);
    const RmEntityId made = api.core->spawnPrefab(api.ctx, guid.c_str(), 0);
    const SceneEntity* root = s.Find(static_cast<int>(made));
    CHECK(root && root->data.name == "Enemy" && root->prefab.Root() && root->prefab.asset.guid == guid);
    const std::vector<int> kids = s.Children(static_cast<int>(made));
    CHECK(kids.size() == 1 && s.Find(kids[0])->FindComponent("test::Counter") != nullptr);
    // Twice: two copies, other ids.
    CHECK(api.core->spawnPrefab(api.ctx, guid.c_str(), 0) != made);
    // Not a prefab of the game: nothing, told once.
    CHECK(api.core->spawnPrefab(api.ctx, "ffffffffffffffffffffffffffffffff", 0) == 0 && told.find("ffff") != std::string::npos);
}

RM_TEST(ScriptsMore_SwappedWhilePlaying_StartedStayStarted_OnReloaded) {
    ScriptModule m, m2;  // (before the scene: its objects go before the types they are made of)
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int a = s.Create("A");
    s.AddComponent(a, m.Find("test::Counter")->desc);
    const int b = s.Create("B");
    s.AddComponent(b, m.Find("test::Counter")->desc);
    s.Find(b)->data.active = false;  // not started
    ScriptWorld w;
    ScriptRunner r;
    r.Frame(s, w, m, {}, 0.1f);
    r.Frame(s, w, m, {}, 0.1f);
    // The scripts built again: which ones had started, the values out by name, the old objects gone, the
    // new module's read back - as the editor does it (GameSoftware::ReloadScripts while playing).
    const auto running = r.Remember(s);
    CHECK(running.size() == 1 && running[0].entity == a && running[0].started);
    const json::Value saved = s.Save();
    s.SetComponentTypes({}, {});
    s.Load(saved);
    m2.Adopt(kModule);
    s.SetComponentTypes(m2.Types(), m2.Types());
    s.Load(saved);
    g_log.clear();
    r.Resume(s, w, m2, {}, running);
    CHECK(g_log == "R" + Id(a));  // OnReloaded, no BeginPlay again
    CHECK(static_cast<const Counter*>(s.Find(a)->FindComponent("test::Counter")->value.Get())->ticks == 2);  // carried over
    g_log.clear();
    r.Frame(s, w, m2, {}, 0.1f);
    CHECK(g_log == "T" + Id(a));
    s.Find(b)->data.active = true;
    g_log.clear();
    r.Frame(s, w, m2, {}, 0.1f);
    CHECK(g_log == "B" + Id(b) + "T" + Id(a) + "T" + Id(b));
    g_log.clear();
    r.Stop(s, w, m2, {});
    CHECK(g_log == "E" + Id(a) + "E" + Id(b));  // pairs kept across the swap
}

RM_TEST(ScriptsMore_KeepThisValue_ReadFromThePlayCopy_WrittenBack) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene before = SceneWith(m);
    const int e = before.Create("Hero");
    before.AddComponent(e, m.Find("test::Rich")->desc);
    Scene playing = before;  // the copy that plays
    // In Play: the name, a nested struct's field, a list changed.
    playing.Find(e)->data.name = "Hero (played)";
    Rich& r = *static_cast<Rich*>(playing.Find(e)->FindComponent("test::Rich")->value.Get());
    r.wave.count = 42;
    r.numbers = {7, 8, 9};
    const SceneField name{e, "", 0, {2}};                // Entity::name
    const SceneField count{e, "test::Rich", 0, {3, 0}};  // wave.count
    const SceneField numbers{e, "test::Rich", 0, {1}};
    const auto vName = ReadSceneField(playing, name);
    const auto vCount = ReadSceneField(playing, count);
    const auto vNumbers = ReadSceneField(playing, numbers);
    CHECK(vName && vName->AsString() == "Hero (played)" && vCount && vCount->AsInt() == 42 && vNumbers && vNumbers->Size() == 3);
    // Written into the scene from before Play; what was not kept stays as it was.
    CHECK(WriteSceneField(before, name, *vName) && WriteSceneField(before, count, *vCount));
    const Rich& kept = *static_cast<const Rich*>(before.Find(e)->FindComponent("test::Rich")->value.Get());
    CHECK(before.Find(e)->data.name == "Hero (played)" && kept.wave.count == 42 && (kept.numbers == std::vector<int>{1, 2}));
    // Not there (an entity made while playing, a component gone, a path too long): nothing.
    CHECK(!ReadSceneField(before, SceneField{999, "", 0, {2}}) && !ReadSceneField(before, SceneField{e, "test::Counter", 0, {0}}));
    CHECK(!WriteSceneField(before, SceneField{999, "", 0, {2}}, *vName));
}
