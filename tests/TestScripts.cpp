// Tests for C++ scripts (game production's runtime/Scripts + rmgen --scripts): the run order, the
// ABI check, scripts as reflected types, the hooks (BeginPlay before Tick, a crash stops only that
// script), the values carried over when the scripts are built again, and rmgen's registration.
// A module is built here by hand (the same tables rmgen writes into a script DLL).

#include "Test.h"

#include "../tools/rmgen/Scanner.h"
#include "rm_engine/game_production/runtime/Scripts.h"

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
const RmModule kModule{RM_ABI_VERSION, 2, kScripts};

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
    const RmModule other{RM_ABI_VERSION + 1, 2, kScripts};
    CHECK(!CheckModule(&other).empty());  // built with another RM.h
    RmScriptDesc twice[] = {kScripts[0], kScripts[0]};
    const RmModule dup{RM_ABI_VERSION, 2, twice};
    CHECK(!CheckModule(&dup).empty());
    RmScriptDesc aligned[] = {kScripts[0]};
    aligned[0].align = 64;
    const RmModule big{RM_ABI_VERSION, 1, aligned};
    CHECK(!CheckModule(&big).empty());
    RmFieldDesc unknownKind[] = {kCounterFields[0]};
    unknownKind[0].type = 99;
    RmScriptDesc strange[] = {kScripts[0]};
    strange[0].fields = unknownKind;
    strange[0].fieldCount = 1;
    const RmModule odd{RM_ABI_VERSION, 1, strange};
    CHECK(!CheckModule(&odd).empty());
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
    static const RmModule module2{RM_ABI_VERSION, 1, scripts2};

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
    w.keyDown = [&](int vk) { return vk == 0x25 && leftDown; };
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
    w.ReadKeys();
    leftDown = true;
    w.ReadKeys();
    CHECK(core.keyDown(api.ctx, 0x25) == 1 && core.keyPressed(api.ctx, 0x25) == 1);
    w.ReadKeys();
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
    // What a script cannot have yet: told, not skipped.
    const char* bad = R"(#include "RM.h"
RM_SCRIPT()
struct Bad {
    RM_PROPERTY() std::vector<int> list;
};
RM_STRUCT()
struct Data {
    RM_PROPERTY() int x = 0;
};
)";
    std::vector<rmgen::Diagnostic> d2;
    std::vector<rmgen::Header> h2{rmgen::Scan(bad, "Bad.h", "Bad.h", d2)};
    rmgen::Validate(h2, d2);
    rmgen::ValidateScripts(h2, d2);
    CHECK(d2.size() == 2 && d2[0].code == "RM0013" && d2[1].code == "RM0013");
    // No scripts at all: still a module (empty).
    CHECK(rmgen::EmitScripts({}).find("kRmScripts = nullptr") != std::string::npos);
}
