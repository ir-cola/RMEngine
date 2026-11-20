// C++ スクリプトのテスト (ゲーム制作のランタイム/Scripts + rmgen --scripts): 実行順、
// ABI の確認、リフレクションの型としてのスクリプト、フック (BeginPlay は Tick より先、
// クラッシュは そのスクリプトだけ止める)、スクリプトを作り直したときの値の引き継ぎ、
// rmgen の登録。モジュールはここで手作りする (rmgen がスクリプト DLL に書くのと同じ表)。

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

std::string g_log;  // フックがしたこと

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
    *p = 1;  // アクセス違反: 捕まえて、このスクリプトを止める
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
// モジュールの文字列関数 (本物の DLL では rmgen が書き、DLL の C ランタイムを使う)。数える:
// エンジンはどの文字列フィールドでも、これを通さなければならない。
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
    CHECK((sorted == std::vector<std::string>{"Input", "Enemy", "Player", "Camera"}));  // -5、(0: 名前順)、10
    CHECK(ScriptOrderOf("Camera", order) == 10 && ScriptOrderOf("Nobody", order) == 0);
}

RM_TEST(Scripts_CheckModule) {
    CHECK(!CheckModule(nullptr).empty());
    CHECK(CheckModule(&kModule).empty());
    const RmModule other{RM_ABI_VERSION + 1, kFlags, 2, kScripts, &StringGet, &StringSet};
    CHECK(!CheckModule(&other).empty());  // 別の RM.h で作った
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
    // Release のエンジンに Debug で作ったもの (その逆も): 読まない (C ランタイムが違う)。
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
    // ほかのデータと同じように保存して読む (インスペクターと Undo も同じ表を使う)。
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
    // 新しいもの全部の BeginPlay が先 (エンティティ順)、
    // そのあと型ごとに Tick: Crasher (-1) が Counter より先。
    CHECK(g_log == "B1B2CT1T2");
    CHECK(r.Crashed() == 1 && told.find("Crasher") == std::string::npos && told.find("落ちる") != std::string::npos);
    g_log.clear();
    // ゲーム中に増えたもの: 次のフレームで、BeginPlay が Tick より先。クラッシュしたものは休む。
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
    // 前: Counter {ticks, speed, note}。後: speed は int になり、note は無くなり、`extra` が増え、
    // Crasher も無くなる (名前の変更か削除)。
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
    // ホットリロードがすること: 値を名前で書き出し、古い型を捨て、新しい型へ読み込む。
    const json::Value saved = s.Save();
    ScriptModule next;
    next.Adopt(module2);
    Scene t = SceneWith(next);
    reflect::Warnings warnings;
    t.Load(saved, &warnings);
    const Component* c = t.Find(a)->FindComponent("test::Counter");
    CHECK(c && c->Known());
    const auto* after = static_cast<const Counter2*>(c->value.Get());
    CHECK(after->ticks == 5);   // 同じ名前で同じ型: 残る
    CHECK(after->speed == 3);   // float -> int: 変換される
    CHECK(after->extra == 42);  // 新しい: その初期値
    // 無くなったフィールドは残す (書き戻す - フィールドが戻れば値も戻る)。無くなったスクリプトは、
    // 値ごと「無いスクリプト」として残す。
    const json::Value again = t.Save();
    const json::Value& comps = again.Get("entities").At(0).Get("components");
    CHECK(comps.At(0).Get("data").Get("note").AsString() == "keep me");
    const Component* gone = t.Find(a)->FindComponent("Crasher");
    CHECK(gone && !gone->Known());
    CHECK(!warnings.empty());  // 無いスクリプトを知らせる
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
    CHECK(core.getName(api.ctx, a, name, sizeof(name)) == 6 && std::string(name) == "Pla");  // 切って、全体の長さを返す
    CHECK(core.isActive(api.ctx, child) == 1);
    core.setActive(api.ctx, a, 0);
    CHECK(core.isActive(api.ctx, child) == 0);  // 親を通して
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
    // ABI 4: リスト、列挙型、入れ子の構造体、アセット参照、OnReloaded。
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
    // スクリプトが持てないもの: 黙って飛ばさず、知らせる。
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
    // スクリプトが 1 つもない: それでもモジュールにはなる (空)。
    CHECK(rmgen::EmitScripts({}).find("kRmScripts = nullptr") != std::string::npos);
}

RM_TEST(Scripts_StringsOnlyThroughTheModule) {
    ScriptModule m;
    m.Adopt(kModule);
    const ScriptType* c = m.Find("test::Counter");
    reflect::Object o(c->desc);
    g_stringCalls = 0;
    // 読む、書く、比べる: いつも DLL 自身の関数を通す (メモリは DLL の C ランタイムのもの)。
    json::Value data = reflect::Save(c->desc, o.Get());
    CHECK(data.Get("note").AsString() == "hello" && g_stringCalls > 0);
    const int afterSave = g_stringCalls;
    data.Set("note", std::string(100, 'x'));  // 小さい文字列用バッファより長い: ヒープに確保される
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
#pragma warning(disable : 4717)  // (無限に再帰するのがこの目的)
int Deep(int n) {
    volatile char buf[4096];
    buf[0] = static_cast<char>(n);
    ++g_deep;
    return Deep(n + 1) + buf[0];  // 戻らない: スタックを使い切る
}
#pragma warning(pop)
#pragma optimize("", on)
void OverflowTick(void*, RmEntityId, float) { Deep(0); }
}  // namespace

RM_TEST(Scripts_StackOverflowTwice_TheEditorGoesOn) {
    RmScriptDesc d = kScripts[1];
    d.tick = &OverflowTick;
    // 2 回: 1 回目のあと、スタックのガードページが元に戻っていなければ
    // ならない (戻らないと 2 回目でプログラムが終わる)。
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
    s.Find(a)->data.active = false;  // 切った: EndPlay は呼ぶ、Tick は呼ばない
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "E1");
    g_log.clear();
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log.empty());
    s.Find(a)->data.active = true;  // また入れた: また BeginPlay、そのあと Tick
    r.Frame(s, w, m, {}, 0.1f);
    CHECK(g_log == "B1!T1");  // (同じオブジェクト: Tick の数は残る)
    g_log.clear();
    // Stop: 始まっていたもの全部の EndPlay (今切ったものも)。ワールドはシーンを手放す。
    s.Find(a)->data.active = false;
    r.Stop(s, w, m, {});
    CHECK(g_log == "E1" && w.scene == nullptr);
    g_log.clear();
    r.Stop(s, w, m, {});
    CHECK(g_log.empty());  // 何も始まっていない: 何も終わらない
}

// Game::LoadScene: フレーム中に頼み、フレームの間で切り替える - 今のシーンの EndPlay、
// 新しいシーンを読み、その BeginPlay は次のフレーム。編集中のシーン (動くのはコピー) は変わらない。
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

    Scene playing = edited;  // (Play: 動くのはコピー)
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
    // 何も頼んでいない: 何も切り替わらない。
    CHECK(SwitchAskedScene(playing, w, r, m, order, load, &err) == SceneSwitch::None && loaded.empty());
    // 一覧にない名前: 断る (知らせる)、何も頼まれない。
    CHECK(core.loadScene(api.ctx, "Nowhere") == 0 && warned.find("Nowhere") != std::string::npos && w.sceneAsked == -1);
    CHECK(core.loadSceneAt(api.ctx, 5) == 0 && w.sceneAsked == -1);
    // フレーム中に頼む (スクリプトの Tick がするように): フレームが終わってから切り替わる。
    char name[16];
    CHECK(core.sceneName(api.ctx, name, sizeof(name)) == 5 && std::string(name) == "Title" && core.sceneIndex(api.ctx) == 0 && core.sceneCount(api.ctx) == 2);
    CHECK(core.loadScene(api.ctx, "Stage") == 1 && w.sceneAsked == 1);
    g_log.clear();
    CHECK(SwitchAskedScene(playing, w, r, m, order, load, &err) == SceneSwitch::Switched);
    CHECK(g_log == "E1");  // 古いシーンの EndPlay。新しいシーンのスクリプトはまだ始まっていない
    CHECK(w.sceneIndex == 1 && w.sceneAsked == -1 && w.scene == &playing && playing.Find(b) && playing.Find(b)->data.name == "ステージ");
    CHECK(core.sceneName(api.ctx, name, sizeof(name)) == 5 && std::string(name) == "Stage");
    g_log.clear();
    r.Frame(playing, w, m, order, 0.1f);
    CHECK(g_log == "B2T2");  // 新しいシーンで、最初の Tick より先に BeginPlay
    // 読めないシーン: 動いているシーンは続く (EndPlay はない)。
    CHECK(core.loadSceneAt(api.ctx, 0) == 1);
    g_log.clear();
    CHECK(SwitchAskedScene(playing, w, r, m, order, load, &err) == SceneSwitch::Failed && err == "読めません");
    CHECK(g_log.empty() && w.sceneIndex == 1 && w.sceneAsked == -1 && playing.Find(b));
    // 停止 (■): コピーは消え、編集中のシーンはそのまま。
    r.Stop(playing, w, m, order);
    CHECK(g_log == "E2");
    CHECK(edited.Save() == editedBefore);
}
