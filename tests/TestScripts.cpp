// Tests for C++ scripts (game production's runtime/Scripts + rmgen --scripts): the run order, the
// ABI check, scripts as reflected types, the hooks (BeginPlay before Tick, a crash stops only that
// script), the values carried over when the scripts are built again, and rmgen's registration.
// A module is built here by hand (the same tables rmgen writes into a script DLL).

#include "Test.h"

#include "../tools/rmgen/Scanner.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <new>

using namespace rm;
using namespace rm::game_production;

namespace {

std::string g_log;  // what the hooks did

struct Counter {
    int ticks = 0;
    float speed = 2.5f;
    std::string note = "hello";
};
void* CounterTicks(void* o) { return &static_cast<Counter*>(o)->ticks; }
void* CounterSpeed(void* o) { return &static_cast<Counter*>(o)->speed; }
void* CounterNote(void* o) { return &static_cast<Counter*>(o)->note; }
void CounterBegin(void* s, RmEntityId e) { g_log += "B" + std::to_string(e) + (static_cast<Counter*>(s)->ticks == 0 ? "" : "!"); }
void CounterTick(void* s, RmEntityId e, float) {
    ++static_cast<Counter*>(s)->ticks;
    g_log += "T" + std::to_string(e);
}
void CounterEnd(void*, RmEntityId e) { g_log += "E" + std::to_string(e); }

struct Crasher {
    int x = 0;
};
void* CrasherX(void* o) { return &static_cast<Crasher*>(o)->x; }
void CrasherTick(void*, RmEntityId, float) {
    g_log += "C";
    volatile int* p = nullptr;
    *p = 1;  // an access violation: caught, this script stopped
}

template <class T>
void Construct(void* p) { new (p) T(); }
template <class T>
void Destruct(void* p) { static_cast<T*>(p)->~T(); }
template <class T>
void Copy(void* d, const void* s) { *static_cast<T*>(d) = *static_cast<const T*>(s); }

const RmFieldDesc kCounterFields[] = {
    {"ticks", RmField_Int, RmFieldFlag_Edit, &CounterTicks, "回数", nullptr, 0, 0, 0, 1},
    {"speed", RmField_Float, RmFieldFlag_Edit | RmFieldFlag_HasMin, &CounterSpeed, "速さ", nullptr, 0, 0, 0, 2},
    {"note", RmField_String, RmFieldFlag_Edit, &CounterNote, nullptr, nullptr, 0, 0, 0, 3},
};
const RmFieldDesc kCrasherFields[] = {{"x", RmField_Int, RmFieldFlag_Edit, &CrasherX, nullptr, nullptr, 0, 0, 0, 1}};
const RmScriptDesc kScripts[] = {
    {"test::Counter", "数える", "Counter.h", sizeof(Counter), alignof(Counter), kCounterFields, 3, &Construct<Counter>, &Destruct<Counter>,
     &Copy<Counter>, &CounterBegin, &CounterTick, &CounterEnd},
    {"Crasher", "落ちる", "Crasher.h", sizeof(Crasher), alignof(Crasher), kCrasherFields, 1, &Construct<Crasher>, &Destruct<Crasher>,
     &Copy<Crasher>, nullptr, &CrasherTick, nullptr},
};
// The module's string functions (in a real DLL: rmgen writes them, with the DLL's C runtime). Counted:
// the engine must go through them for every string field.
int g_stringCalls = 0;
uint64_t StringGet(const void* field, char* buf, uint64_t cap) {
    ++g_stringCalls;
    const std::string& s = *static_cast<const std::string*>(field);
    if (buf && cap > 0) {
        const size_t n = std::min<size_t>(s.size(), static_cast<size_t>(cap - 1));
        std::memcpy(buf, s.data(), n);
        buf[n] = '\0';
    }
    return s.size();
}
void StringSet(void* field, const char* utf8, uint64_t len) {
    ++g_stringCalls;
    static_cast<std::string*>(field)->assign(utf8, static_cast<size_t>(len));
}
constexpr uint32_t kFlags = kEngineDebug ? RmModuleFlag_Debug : 0;
const RmModule kModule{RM_ABI_VERSION, kFlags, 2, kScripts, &StringGet, &StringSet};

Scene SceneWith(const ScriptModule& m) {
    Scene s;
    const auto types = m.Types();
    s.SetComponentTypes(types, types);
    return s;
}

}  // namespace

RM_TEST(Scripts_OrderByListThenName) {
    const std::vector<ScriptOrderEntry> order{{"Camera", 10}, {"Input", -5}};
    const auto sorted = SortScriptTypes({"Player", "Camera", "Enemy", "Input", "Player"}, order);
    CHECK((sorted == std::vector<std::string>{"Input", "Enemy", "Player", "Camera"}));  // -5, (0: by name), 10
    CHECK(ScriptOrderOf("Camera", order) == 10 && ScriptOrderOf("Nobody", order) == 0);
}

RM_TEST(Scripts_CheckModule) {
    CHECK(!CheckModule(nullptr).empty());
    CHECK(CheckModule(&kModule).empty());
    const RmModule other{RM_ABI_VERSION + 1, kFlags, 2, kScripts, &StringGet, &StringSet};
    CHECK(!CheckModule(&other).empty());  // built with another RM.h
    RmScriptDesc twice[] = {kScripts[0], kScripts[0]};
    const RmModule dup{RM_ABI_VERSION, kFlags, 2, twice, &StringGet, &StringSet};
    CHECK(!CheckModule(&dup).empty());
    RmScriptDesc aligned[] = {kScripts[0]};
    aligned[0].align = 64;
    const RmModule big{RM_ABI_VERSION, kFlags, 1, aligned, &StringGet, &StringSet};
    CHECK(!CheckModule(&big).empty());
    RmFieldDesc unknownKind[] = {kCounterFields[0]};
    unknownKind[0].type = 99;
    RmScriptDesc strange[] = {kScripts[0]};
    strange[0].fields = unknownKind;
    strange[0].fieldCount = 1;
    const RmModule odd{RM_ABI_VERSION, kFlags, 1, strange, &StringGet, &StringSet};
    CHECK(!CheckModule(&odd).empty());
    // Built as Debug for a Release engine (or the other way): never read (another C runtime).
    const RmModule otherKind{RM_ABI_VERSION, kFlags ^ RmModuleFlag_Debug, 2, kScripts, &StringGet, &StringSet};
    CHECK(!CheckModule(&otherKind).empty());
    const RmModule noStrings{RM_ABI_VERSION, kFlags, 2, kScripts, nullptr, nullptr};
    CHECK(!CheckModule(&noStrings).empty());
    CHECK(ScriptDllNumber(L"Scripts_0007.dll") == 7 && ScriptDllNumber(L"Scripts_.dll") == -1 && ScriptDllNumber(L"Other_1.dll") == -1);
}

RM_TEST(Scripts_AreReflectedTypes) {
    ScriptModule m;
    m.Adopt(kModule);
    const ScriptType* c = m.Find("test::Counter");
    CHECK(c && c->desc.kind == reflect::TypeKind::Script && std::string(c->desc.label) == "数える" && c->desc.fields.size() == 3);
    CHECK(c->desc.fields[1].hasMin && c->desc.fields[2].value.type == reflect::FieldType::String);
    // Saved and read like any data (the inspector and undo use the same tables).
    reflect::Object o(c->desc);
    static_cast<Counter*>(o.Get())->speed = 7;
    const json::Value saved = reflect::Save(c->desc, o.Get());
    CHECK(saved.Get("speed").AsDouble() == 7 && saved.Get("note").AsString() == "hello");
}

RM_TEST(Scripts_BeginPlayBeforeTick_TypeByType_CrashStopsOnlyIt) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int a = s.Create("a");
    const int b = s.Create("b");
    s.AddComponent(b, m.Find("test::Counter")->desc);
    s.AddComponent(a, m.Find("test::Counter")->desc);
    s.AddComponent(a, m.Find("Crasher")->desc);
    ScriptWorld w;
    std::string told;
    w.log = [&](int level, std::string_view text) {
        if (level == 2) told += text;
    };
    ScriptRunner r;
    const std::vector<ScriptOrderEntry> order{{"Crasher", -1}};
    g_log.clear();
    r.Frame(s, w, m, order, 0.1f);
    // BeginPlay of every new one first (by entity), then Tick type by type: Crasher (-1) before Counter.
    CHECK(g_log == "B1B2CT1T2");
    CHECK(r.Crashed() == 1 && told.find("Crasher") == std::string::npos && told.find("落ちる") != std::string::npos);
    g_log.clear();
    // A new one during the game: its BeginPlay before its Tick, in the next frame. The crashed one rests.
    const int c = s.Create("c");
    s.AddComponent(c, m.Find("test::Counter")->desc);
    r.Frame(s, w, m, order, 0.1f);
    CHECK(g_log == "B3T1T2T3");
    CHECK(static_cast<Counter*>(s.Find(a)->FindComponent("test::Counter")->value.Get())->ticks == 2);
    CHECK(w.time > 0.19 && w.time < 0.21);
    g_log.clear();
    r.Stop(s, w, m, order);
    CHECK(g_log == "E1E2E3");
}

RM_TEST(Scripts_ValuesCarriedOverWhenBuiltAgain) {
    // Before: Counter {ticks, speed, note}. After: speed is an int now, note is gone, `extra` is new,
    // and Crasher is gone (renamed or deleted).
    struct Counter2 {
        int speed = 0;
        int extra = 42;
        int ticks = 0;
    };
    static const RmFieldDesc fields2[] = {
        {"speed", RmField_Int, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Counter2*>(o)->speed; }, nullptr, nullptr, 0, 0, 0, 1},
        {"extra", RmField_Int, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Counter2*>(o)->extra; }, nullptr, nullptr, 0, 0, 0, 2},
        {"ticks", RmField_Int, RmFieldFlag_Edit, [](void* o) -> void* { return &static_cast<Counter2*>(o)->ticks; }, nullptr, nullptr, 0, 0, 0, 3},
    };
    static const RmScriptDesc scripts2[] = {{"test::Counter", "数える", "Counter.h", sizeof(Counter2), alignof(Counter2), fields2, 3,
                                             &Construct<Counter2>, &Destruct<Counter2>, &Copy<Counter2>, nullptr, nullptr, nullptr}};
    static const RmModule module2{RM_ABI_VERSION, kFlags, 1, scripts2, &StringGet, &StringSet};

    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int a = s.Create("a");
    s.AddComponent(a, m.Find("test::Counter")->desc);
    s.AddComponent(a, m.Find("Crasher")->desc);
    auto* before = static_cast<Counter*>(s.Find(a)->FindComponent("test::Counter")->value.Get());
    before->speed = 3.0f;
    before->ticks = 5;
    before->note = "keep me";
    // What a hot reload does: the values written out by name, the old types gone, the new ones read.
    const json::Value saved = s.Save();
    ScriptModule next;
    next.Adopt(module2);
    Scene t = SceneWith(next);
    reflect::Warnings warnings;
    t.Load(saved, &warnings);
    const Component* c = t.Find(a)->FindComponent("test::Counter");
    CHECK(c && c->Known());
    const auto* after = static_cast<const Counter2*>(c->value.Get());
    CHECK(after->ticks == 5);   // same name, same type: kept
    CHECK(after->speed == 3);   // float -> int: converted
    CHECK(after->extra == 42);  // new: its default
    // The field that went is kept (written back - it comes back if the field does), and the script
    // that went is kept as a missing one, with its values.
    const json::Value again = t.Save();
    const json::Value& comps = again.Get("entities").At(0).Get("components");
    CHECK(comps.At(0).Get("data").Get("note").AsString() == "keep me");
    const Component* gone = t.Find(a)->FindComponent("Crasher");
    CHECK(gone && !gone->Known());
    CHECK(!warnings.empty());  // the missing script is told
}

RM_TEST(Scripts_CoreApiOnTheWorld) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int a = s.Create("Player");
    const int child = s.Create("Hand", a);
    s.AddComponent(child, m.Find("test::Counter")->desc);
    ScriptWorld w;
    w.scene = &s;
    bool leftDown = false;
    w.readInput = [&] {
        InputFrame f;
        f.keys[0x25] = leftDown ? 1 : 0;
        return f;
    };
    static const int partTable = 7;
    w.parts.push_back({"test.part", 2, &partTable});
    const RmApi api = MakeScriptApi(w);
    const RmCoreApi& core = *api.core;
    CHECK(core.findByName(api.ctx, "Hand") == static_cast<RmEntityId>(child) && core.findByName(api.ctx, "nobody") == 0);
    CHECK(core.parent(api.ctx, child) == static_cast<RmEntityId>(a));
    char name[4];
    CHECK(core.getName(api.ctx, a, name, sizeof(name)) == 6 && std::string(name) == "Pla");  // cut, the full length told
    CHECK(core.isActive(api.ctx, child) == 1);
    core.setActive(api.ctx, a, 0);
    CHECK(core.isActive(api.ctx, child) == 0);  // through its parent
    CHECK(core.getScript(api.ctx, child, "test::Counter", 0) != nullptr && core.getScript(api.ctx, child, "test::Counter", 1) == nullptr);
    w.ReadInput();
    leftDown = true;
    w.ReadInput();
    CHECK(core.keyDown(api.ctx, 0x25) == 1 && core.keyPressed(api.ctx, 0x25) == 1);
    w.ReadInput();
    CHECK(core.keyPressed(api.ctx, 0x25) == 0);
    core.setCamera(api.ctx, 9);
    CHECK(w.camera == 9 && core.camera(api.ctx) == 9);
    CHECK(api.part(api.ctx, "test.part", 2) == &partTable && api.part(api.ctx, "test.part", 1) == nullptr);
}

RM_TEST(Scripts_RmgenWritesTheRegistration) {
    const char* text = R"(#pragma once
#include "RM.h"
RM_SCRIPT(Label="動く")
struct Mover {
    RM_PROPERTY(Edit, Label="速さ", Min=0) float speed = 3.0f;
    RM_PROPERTY(Edit) rm::Vec3 dir;
    RM_TRANSIENT() float time = 0;
    void Tick(rm::Entity self, float dt);
};
)";
    std::vector<rmgen::Diagnostic> diags;
    std::vector<rmgen::Header> headers{rmgen::Scan(text, "Mover.h", "Mover.h", diags)};
    rmgen::Validate(headers, diags);
    rmgen::ValidateScripts(headers, diags);
    CHECK(diags.empty());
    const std::string cpp = rmgen::EmitScripts(headers);
    CHECK(cpp.find("#include \"Mover.h\"") != std::string::npos);
    CHECK(cpp.find("RmModuleInit") != std::string::npos && cpp.find("RM_ABI_VERSION") != std::string::npos);
    CHECK(cpp.find("{\"speed\", RmField_Float") != std::string::npos && cpp.find("RmFieldFlag_HasMin") != std::string::npos);
    CHECK(cpp.find("{\"dir\", RmField_Vec3") != std::string::npos && cpp.find("\"time\"") == std::string::npos);
    CHECK(cpp.find("\"動く\"") != std::string::npos);
    // ABI 4: lists, enums, nested structs, asset references, OnReloaded.
    const char* more = R"(#include "RM.h"
RM_ENUM() enum class Mode { RM_ITEM(Label="歩く") Walk, Run };
RM_STRUCT(Label="波") struct Wave { RM_PROPERTY(Edit) int count = 3; RM_PROPERTY(Edit) std::vector<std::string> tags; };
RM_SCRIPT()
struct Spawner {
    RM_PROPERTY(Edit) std::vector<int> list;
    RM_PROPERTY(Edit) Mode mode = Mode::Run;
    RM_PROPERTY(Edit) Wave wave;
    RM_PROPERTY(Edit) std::vector<Wave> waves;
    RM_PROPERTY(Edit) rm::PrefabRef enemy;
    RM_PROPERTY(Edit) std::vector<rm::ImageRef> pictures;
    void OnReloaded(rm::Entity self);
};
)";
    std::vector<rmgen::Diagnostic> d1;
    std::vector<rmgen::Header> h1{rmgen::Scan(more, "Spawner.h", "Spawner.h", d1)};
    rmgen::Validate(h1, d1);
    rmgen::ValidateScripts(h1, d1);
    CHECK(d1.empty());
    const std::string c1 = rmgen::EmitScripts(h1);
    CHECK(c1.find("{\"list\", RmField_List") != std::string::npos && c1.find("RmVecSize<std::vector<int>>") != std::string::npos);
    CHECK(c1.find("{\"mode\", RmField_Enum") != std::string::npos && c1.find("rmgen_scripts::E_Mode") != std::string::npos);
    CHECK(c1.find("{\"wave\", RmField_Struct") != std::string::npos && c1.find("const RmStructDesc rmgen_scripts::S_Wave") != std::string::npos);
    CHECK(c1.find("\"歩く\"") != std::string::npos && c1.find("\"波\"") != std::string::npos);
    CHECK(c1.find("{\"enemy\", RmField_Asset") != std::string::npos && c1.find("\".rmprefab\"") != std::string::npos);
    CHECK(c1.find("RmVecSize<std::vector<::rm::ImageRef>>") != std::string::npos && c1.find("\"image\"") != std::string::npos);
    CHECK(c1.find("RmReloadOf<::Spawner>()") != std::string::npos);
    // What a script cannot have: told, not skipped.
    const char* bad = R"(#include "RM.h"
RM_COMPONENT()
struct Engine {
    RM_PROPERTY() int x = 0;
};
RM_SCRIPT()
struct Other {
    RM_PROPERTY() int y = 0;
};
RM_SCRIPT()
struct Bad {
    RM_PROPERTY() rm::AssetRef any;
    RM_PROPERTY() Other inside;
};
)";
    std::vector<rmgen::Diagnostic> d2;
    std::vector<rmgen::Header> h2{rmgen::Scan(bad, "Bad.h", "Bad.h", d2)};
    rmgen::Validate(h2, d2);
    rmgen::ValidateScripts(h2, d2);
    CHECK(d2.size() == 3 && std::all_of(d2.begin(), d2.end(), [](const rmgen::Diagnostic& d) { return d.code == "RM0013"; }));
    // No scripts at all: still a module (empty).
    CHECK(rmgen::EmitScripts({}).find("kRmScripts = nullptr") != std::string::npos);
}

RM_TEST(Scripts_StringsOnlyThroughTheModule) {
    ScriptModule m;
    m.Adopt(kModule);
    const ScriptType* c = m.Find("test::Counter");
    reflect::Object o(c->desc);
    g_stringCalls = 0;
    // Read, written, compared: always through the DLL's own functions (its C runtime owns the memory).
    json::Value data = reflect::Save(c->desc, o.Get());
    CHECK(data.Get("note").AsString() == "hello" && g_stringCalls > 0);
    const int afterSave = g_stringCalls;
    data.Set("note", std::string(100, 'x'));  // longer than the small-string buffer: a heap allocation
    reflect::Load(c->desc, o.Get(), data);
    CHECK(g_stringCalls > afterSave && static_cast<Counter*>(o.Get())->note == std::string(100, 'x'));
    reflect::Object copy(c->desc);
    CHECK(!reflect::Equals(c->desc, o.Get(), copy.Get()));
    CHECK(reflect::ReadString(c->desc.fields[2], &static_cast<Counter*>(o.Get())->note) == std::string(100, 'x'));
}

namespace {
int g_deep = 0;
#pragma optimize("", off)
#pragma warning(push)
#pragma warning(disable : 4717)  // (recursing for ever is what this is for)
int Deep(int n) {
    volatile char buf[4096];
    buf[0] = static_cast<char>(n);
    ++g_deep;
    return Deep(n + 1) + buf[0];  // never returns: the stack runs out
}
#pragma warning(pop)
#pragma optimize("", on)
void OverflowTick(void*, RmEntityId, float) { Deep(0); }
}  // namespace

RM_TEST(Scripts_StackOverflowTwice_TheEditorGoesOn) {
    RmScriptDesc d = kScripts[1];
    d.tick = &OverflowTick;
    // Twice: after the first, the stack's guard page must be back (or the second ends the program).
    CHECK(CallScriptHook(d, 1, nullptr, 1, 0) == EXCEPTION_STACK_OVERFLOW);
    CHECK(CallScriptHook(d, 1, nullptr, 1, 0) == EXCEPTION_STACK_OVERFLOW);
    CHECK(g_deep > 10);
}

RM_TEST(Scripts_BeginAndEndInPairs_WhenSwitchedOffAndOn) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene s = SceneWith(m);
    const int a = s.Create("a");
    s.AddComponent(a, m.Find("test::Counter")->desc);
    ScriptWorld w;
    ScriptRunner r;
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "B1T1");
    g_log.clear();
    s.Find(a)->data.active = false;  // switched off: EndPlay, no Tick
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "E1");
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log.empty());
    s.Find(a)->data.active = true;  // on again: BeginPlay again, then Tick
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "B1!T1");  // (the same object: its ticks kept)
    g_log.clear();
    // Stop: EndPlay of every started one, also one switched off just now; the world lets the scene go.
    s.Find(a)->data.active = false;
    r.Stop(s, w, m, {});
    CHECK(g_log == "E1" && w.scene == nullptr);
    g_log.clear();
    r.Stop(s, w, m, {});
    CHECK(g_log.empty());  // nothing started: nothing ended
}

// Game::LoadScene: asked during a frame, switched between frames - EndPlay of the scene playing, the
// new one read, its BeginPlay at the next frame. The scene being edited (the copy plays) never changes.
RM_TEST(Scripts_LoadSceneBetweenFrames) {
    ScriptModule m;
    m.Adopt(kModule);
    Scene edited = SceneWith(m);
    const int a = edited.Create("タイトル");
    edited.AddComponent(a, m.Find("test::Counter")->desc);
    const json::Value editedBefore = edited.Save();
    Scene stage = SceneWith(m);
    stage.Create("空");
    const int b = stage.Create("ステージ");
    stage.AddComponent(b, m.Find("test::Counter")->desc);
    const json::Value stageFile = stage.Save();

    Scene playing = edited;  // (Play: the copy plays)
    ScriptWorld w;
    std::string warned;
    w.log = [&](int level, std::string_view text) {
        if (level == 1) warned += text;
    };
    w.sceneNames = {"Title", "Stage"};
    w.sceneIndex = 0;
    const RmApi api = MakeScriptApi(w);
    const RmCoreApi& core = *api.core;
    ScriptRunner r;
    const std::vector<ScriptOrderEntry> order;
    std::vector<size_t> loaded;
    auto load = [&](size_t index, Scene& into, std::string* error) {
        loaded.push_back(index);
        if (index != 1) {
            if (error) *error = "読めません";
            return false;
        }
        into.Load(stageFile);
        return true;
    };
    std::string err;
    g_log.clear();
    r.Frame(playing, w, m, order, 0.1f);
    CHECK(g_log == "B1T1");
    // Nothing asked: nothing switched.
    CHECK(SwitchAskedScene(playing, w, r, m, order, load, &err) == SceneSwitch::None && loaded.empty());
    // A name not in the list: refused (told), nothing asked.
    CHECK(core.loadScene(api.ctx, "Nowhere") == 0 && warned.find("Nowhere") != std::string::npos && w.sceneAsked == -1);
    CHECK(core.loadSceneAt(api.ctx, 5) == 0 && w.sceneAsked == -1);
    // Asked during the frame (as a script's Tick would): the frame ends, then the switch.
    char name[16];
    CHECK(core.sceneName(api.ctx, name, sizeof(name)) == 5 && std::string(name) == "Title" && core.sceneIndex(api.ctx) == 0 && core.sceneCount(api.ctx) == 2);
    CHECK(core.loadScene(api.ctx, "Stage") == 1 && w.sceneAsked == 1);
    g_log.clear();
    CHECK(SwitchAskedScene(playing, w, r, m, order, load, &err) == SceneSwitch::Switched);
    CHECK(g_log == "E1");  // the old scene's EndPlay; the new one's scripts not started yet
    CHECK(w.sceneIndex == 1 && w.sceneAsked == -1 && w.scene == &playing && playing.Find(b) && playing.Find(b)->data.name == "ステージ");
    CHECK(core.sceneName(api.ctx, name, sizeof(name)) == 5 && std::string(name) == "Stage");
    g_log.clear();
    r.Frame(playing, w, m, order, 0.1f);
    CHECK(g_log == "B2T2");  // BeginPlay before the first Tick, in the new scene
    // A scene that cannot be read: the one playing goes on (no EndPlay).
    CHECK(core.loadSceneAt(api.ctx, 0) == 1);
    g_log.clear();
    CHECK(SwitchAskedScene(playing, w, r, m, order, load, &err) == SceneSwitch::Failed && err == "読めません");
    CHECK(g_log.empty() && w.sceneIndex == 1 && w.sceneAsked == -1 && playing.Find(b));
    // Stopped (■): the copy goes; the scene being edited is as it was.
    r.Stop(playing, w, m, order);
    CHECK(g_log == "E2");
    CHECK(edited.Save() == editedBefore);
}
