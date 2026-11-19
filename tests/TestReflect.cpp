// Tests for reflection (core/reflect): the design's completion conditions T1-T3 plus equality,
// diff, copy, unknown enum names and the undo command. The types are in TestReflectTypes.h and
// their tables are written by rmgen during the build of RMTests.

#include "Test.h"

#include "TestReflectTypes.h"
#include "core/UndoHistory.h"
#include "core/reflect/Data.h"
#include "core/reflect/FieldCommand.h"
#include "core/reflect/Object.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

using namespace rm;
using namespace rm::reflect;
using rmtest::Quality;
using rmtest::Settings;
using rmtest::Window;

namespace {

const FieldDesc& FieldNamed(const TypeDesc& t, std::string_view name) {
    for (const auto& f : t.fields) {
        if (name == f.name) return f;
    }
    static FieldDesc none{};
    return none;
}

size_t IndexOf(const TypeDesc& t, std::string_view name) {
    for (size_t i = 0; i < t.fields.size(); ++i) {
        if (name == t.fields[i].name) return i;
    }
    return ~size_t{0};
}

}  // namespace

RM_TEST(Reflect_TableFromHeader) {
    const TypeDesc& t = TypeOf<Settings>();
    CHECK(std::string_view(t.label) == "テスト用の設定");
    CHECK(t.kind == TypeKind::Struct);
    CHECK(t.fields.size() == 13);  // transient fields are not in the table
    CHECK(FieldCount<Settings>() == 15);
    const FieldDesc& speed = FieldNamed(t, "speed");
    CHECK(speed.value.type == FieldType::Float);
    CHECK(speed.hasMin && speed.min == 0 && speed.hasMax && speed.max == 10 && speed.hasStep && speed.step == 0.5);
    CHECK(std::string_view(speed.Label()) == "速さ");
    CHECK(std::string_view(FieldNamed(t, "title").tooltip) == "ゲームの名前");
    CHECK(FieldNamed(t, "build").readOnly && !FieldNamed(t, "build").edit);
    CHECK(FieldNamed(t, "window").value.structType == &TypeOf<Window>());
    CHECK(FieldNamed(t, "monitors").array->element.structType == &TypeOf<Window>());
    const EnumDesc* q = FieldNamed(t, "quality").value.enumType;
    CHECK(q && q->items.size() == 3);
    CHECK(std::string_view(q->items[0].label) == "低" && std::string_view(q->items[1].label) == "Medium");
    CHECK(q->items[2].value == 7);
    CHECK(FindEnum(typeid(Quality)) == q);
    // Every reflected type of the program is listed (the tests also build the plugins' types).
    auto listed = [](const TypeDesc* d) { return std::find(AllTypes().begin(), AllTypes().end(), d) != AllTypes().end(); };
    CHECK(listed(&TypeOf<Settings>()) && listed(&TypeOf<Window>()));
    CHECK(std::find(AllEnums().begin(), AllEnums().end(), q) != AllEnums().end());
}

RM_TEST(Reflect_T1_RoundTrip) {
    // 10,000 random objects: save -> load -> save gives the same bytes, and the objects are equal.
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    const TypeDesc& t = TypeOf<Settings>();
    for (int n = 0; n < 10000; ++n) {
        Settings a;
        a.title = "名前 " + std::to_string(rng());
        a.speed = unit(rng) * 10.f;
        a.level = static_cast<int>(rng() % 7) - 3;
        a.vsync = rng() % 2 == 0;
        a.clear = {unit(rng), unit(rng), unit(rng), unit(rng)};
        a.gravity2d = {unit(rng) * 100 - 50, unit(rng) * -20};
        a.gravity = {unit(rng), -unit(rng) * 30, unit(rng) * 1e-6f};
        a.quality = std::array{Quality::Low, Quality::Medium, Quality::High}[rng() % 3];
        a.window.width = 320 + static_cast<int>(rng() % 7000);
        a.window.fullscreen = rng() % 2 == 0;
        for (size_t i = rng() % 5; i > 0; --i) a.scores.push_back(static_cast<int>(rng()));
        for (size_t i = rng() % 3; i > 0; --i) a.monitors.push_back({640 + static_cast<int>(rng() % 100), 480, true});
        for (size_t i = rng() % 3; i > 0; --i) a.history.push_back(Quality::High);
        const std::string text = json::Write(Save(a));
        Settings b;
        Load(b, *json::Parse(text));
        if (!Equals(t, &a, &b) || json::Write(Save(b)) != text) {
            CHECK(false);
            break;
        }
    }
}

RM_TEST(Reflect_T2_UnknownKeysAreKept) {
    Settings a;
    a.speed = 2.5f;
    json::Value v = Save(a);
    v.Set("addedByANewerVersion", "keep me");
    json::Value nested = json::Value::MakeObject();
    nested.Set("x", 1);
    v.Set("pluginData", nested);
    json::Value w = v.Get("window");
    w.Set("refreshRate", 144);
    v.Set("window", w);
    v.Set("quality", "Ultra");  // an enum value this version does not know

    Settings b;
    Unknowns u;
    Warnings warn;
    Load(b, v, &u, &warn);
    CHECK(b.speed == 2.5f);
    CHECK(b.quality == Quality::Medium);  // behaves with the default ...
    CHECK(!warn.empty());                 // ... says so ...
    const json::Value back = Save(b, &u);  // ... and writes everything back
    CHECK(back.Get("addedByANewerVersion").AsString() == "keep me");
    CHECK(back.Get("pluginData").Get("x").AsInt() == 1);
    CHECK(back.Get("window").Get("refreshRate").AsInt() == 144);
    CHECK(back.Get("quality").AsString() == "Ultra");
    CHECK(back.Get("speed").AsDouble() == 2.5);
    // without an Unknowns the file is read the same way, and only known data is written
    Settings c;
    Load(c, v);
    CHECK(Equals(TypeOf<Settings>(), &b, &c));
    CHECK(Save(c).Find("addedByANewerVersion") == nullptr);
}

RM_TEST(Reflect_T3_ClampIsTheSameOnBothRoads) {
    // speed: Min=0 Max=10, level: Min=-3 Max=3 (int)
    const TypeDesc& t = TypeOf<Settings>();
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    struct Case {
        const char* field;
        double in;
        const char* typed;
    };
    const Case cases[] = {{"speed", -5, "-5"},   {"speed", 15, "15"},   {"speed", nan, "nan"},  {"speed", inf, "inf"},
                          {"speed", -inf, "-inf"}, {"speed", 3.25, "3.25"}, {"level", 2.6, "2.6"}, {"level", -100, "-100"},
                          {"level", inf, "inf"},   {"level", 1e300, "1e300"}};
    for (const Case& c : cases) {
        const FieldDesc& f = FieldNamed(t, c.field);
        // the loader's road
        Settings loaded;
        json::Value v = Save(loaded);
        v.Set(c.field, c.in);
        Load(loaded, v);
        const double fromFile = std::string_view(c.field) == "speed" ? loaded.speed : loaded.level;
        // the inspector's road (typed text, the current value being the default)
        const Settings fresh;
        const double current = std::string_view(c.field) == "speed" ? fresh.speed : fresh.level;
        const auto fromInput = NumberFromInput(f, c.typed, current);
        CHECK(fromInput.has_value());
        CHECK(fromInput && static_cast<float>(*fromInput) == static_cast<float>(fromFile));
    }
    // the results themselves
    const FieldDesc& speed = FieldNamed(t, "speed");
    CHECK(SanitizeNumber(speed, -5, 5) == 0 && SanitizeNumber(speed, 15, 5) == 10 && SanitizeNumber(speed, nan, 5) == 5);
    CHECK(SanitizeNumber(FieldNamed(t, "level"), 2.6, 1) == 3);
    CHECK(!NumberFromInput(speed, "abc", 5).has_value());
    CHECK(!NumberFromInput(speed, "", 5).has_value());
    CHECK(NumberFromInput(speed, " +4 ", 5) == 4.0);
    // nested struct fields are clamped by their own attributes
    Settings s;
    json::Value v = Save(s);
    json::Value w = v.Get("window");
    w.Set("width", 10);
    v.Set("window", w);
    Load(s, v);
    CHECK(s.window.width == 320);
}

RM_TEST(Reflect_WrongTypesKeepTheValue) {
    Settings s;
    json::Value v = json::Value::MakeObject();
    v.Set("speed", "fast");
    v.Set("gravity", json::Value::MakeArray());
    v.Set("window", 3);
    Warnings w;
    Load(s, v, nullptr, &w);
    CHECK(s.speed == 5.0f);
    CHECK(s.gravity.y == -9.8f);
    CHECK(s.window.width == 1280);
    CHECK(w.size() == 3);
    // Kept and written back as they were (a newer version's value of another kind: design R1 4-f) -
    // until the field is set (2026-11-19: before, the next save dropped them).
    Settings k;
    Unknowns u;
    Load(k, v, &u, nullptr);
    json::Value back = Save(k, &u);
    CHECK(back.Get("speed").AsString() == "fast" && back.Get("gravity").IsArray() && back.Get("gravity").Size() == 0 && back.Get("window").AsInt() == 3);
    const TypeDesc& t = TypeOf<Settings>();
    for (size_t i = 0; i < t.fields.size(); ++i) {
        if (std::string_view(t.fields[i].name) != "speed") continue;
        SetFieldCommand set(t, FieldPath{i}, SaveField(t.fields[i], &k, &u), json::Value(7), [&] { return EditTarget{&k, &u}; }, "速さ", false);
        set.Do();
    }
    back = Save(k, &u);
    CHECK(back.Get("speed").AsDouble() == 7 && back.Get("window").AsInt() == 3);  // (chosen: the choice; the rest as they were)
    // A number beyond what a float holds: as one that is not finite (it would become null, then the default).
    Settings big;
    json::Value bv = Save(big);
    bv.Set("gravity", json::Value::MakeArray());
    json::Value g = json::Value::MakeArray();
    g.Push(1e39);
    g.Push(2.0);
    g.Push(0.0);
    bv.Set("gravity", g);
    Load(big, bv);
    CHECK(big.gravity.x == -0.f || big.gravity.x == 0.f);
    CHECK(big.gravity.y == 2.f);
    CHECK(SanitizeNumber(FieldNamed(t, "speed"), 1e39, 4) == 4);
    // An array's elements' unknowns stay with their places - not put on others when the count changed.
    Settings arr;
    json::Value av = Save(arr);
    json::Value mons = json::Value::MakeArray();
    for (int m = 0; m < 2; ++m) {
        json::Value one = json::Value::MakeObject();
        one.Set("width", 800 + m);
        one.Set("hz", 60 + m);  // (a newer version's key)
        mons.Push(one);
    }
    av.Set("monitors", mons);
    Unknowns au;
    Load(arr, av, &au, nullptr);
    CHECK(Save(arr, &au).Get("monitors").At(1).Get("hz").AsInt() == 61);
    arr.monitors.erase(arr.monitors.begin());  // (a tool takes the first out directly)
    const json::Value after = Save(arr, &au);
    CHECK(after.Get("monitors").Size() == 1 && after.Get("monitors").At(0).Find("hz") == nullptr && after.Get("monitors").At(0).Get("width").AsInt() == 801);
}

RM_TEST(Reflect_EqualsDiffCopy) {
    const TypeDesc& t = TypeOf<Settings>();
    Settings a, b;
    CHECK(Equals(t, &a, &b) && Diff(t, &a, &b) == 0);
    b.timer = 99;  // transient: not part of the data
    CHECK(Equals(t, &a, &b));
    b.speed = 1;
    b.window.height = 999;
    const uint64_t d = Diff(t, &a, &b);
    CHECK(d == ((uint64_t{1} << IndexOf(t, "speed")) | (uint64_t{1} << IndexOf(t, "window"))));
    b.scores = {1, 2};
    CHECK(Diff(t, &a, &b) & (uint64_t{1} << IndexOf(t, "scores")));
    Settings c;
    t.copy(&c, &b);
    CHECK(Equals(t, &b, &c));
    // construct / destruct by the table
    alignas(Settings) unsigned char raw[sizeof(Settings)];
    t.construct(raw);
    CHECK(reinterpret_cast<Settings*>(raw)->title == "My Game");
    t.destruct(raw);
}

RM_TEST(Reflect_SetFieldCommandUndo) {
    Settings s;
    Unknowns u;
    json::Value v = Save(s);
    v.Set("quality", "Ultra");
    Load(s, v, &u);
    const TypeDesc& t = TypeOf<Settings>();
    UndoHistory h;
    int modified = 0;
    h.SetOnModified([&] { ++modified; });
    auto resolve = [&] { return EditTarget{&s, &u}; };

    // a nested field: window.width (clamped by its Min when applied)
    const FieldPath widthPath{IndexOf(t, "window"), IndexOf(TypeOf<Window>(), "width")};
    const PathTarget pt = Resolve(t, &s, &u, widthPath);
    CHECK(pt.field && std::string_view(pt.field->name) == "width");
    CHECK(PathText(t, widthPath) == "window.width");
    h.Push(std::make_unique<SetFieldCommand>(t, widthPath, SaveField(*pt.field, pt.owner, pt.unknowns), json::Value(100), resolve,
                                             "幅"),
           false);
    CHECK(s.window.width == 320);
    CHECK(modified == 1);
    h.Undo();
    CHECK(s.window.width == 1280);
    h.Redo();
    CHECK(s.window.width == 320);

    // choosing a value for an enum that held an unknown name replaces it; undo brings the name back
    const FieldPath qp{IndexOf(t, "quality")};
    const PathTarget qt = Resolve(t, &s, &u, qp);
    const json::Value before = SaveField(*qt.field, qt.owner, qt.unknowns);
    CHECK(before.AsString() == "Ultra");
    h.Push(std::make_unique<SetFieldCommand>(t, qp, before, json::Value("High"), resolve, "品質"), false);
    CHECK(s.quality == Quality::High);
    CHECK(Save(s, &u).Get("quality").AsString() == "High");
    h.Undo();
    CHECK(Save(s, &u).Get("quality").AsString() == "Ultra");
}

RM_TEST(Reflect_References) {
    using rmtest::Links;
    const TypeDesc& t = TypeOf<Links>();
    CHECK(FieldNamed(t, "model").value.type == FieldType::AssetRef);
    CHECK(std::string_view(FieldNamed(t, "model").assetKinds) == "model");
    CHECK(FieldNamed(t, "target").value.type == FieldType::EntityRef && FieldNamed(t, "target").assetKinds == nullptr);
    CHECK(FieldNamed(t, "textures").array->element.type == FieldType::AssetRef);
    CHECK(std::string_view(FieldNamed(t, "textures").assetKinds) == "image,model");

    // Saved as the ids; read back the same.
    Links a;
    a.model.guid = "0123456789abcdef0123456789abcdef";
    a.target.id = 42;
    a.textures = {{"00000000000000000000000000000001"}, {}};
    a.followers = {{7}, {9}};
    const json::Value v = Save(a);
    CHECK(v.Get("model").AsString() == a.model.guid);
    CHECK(v.Get("target").AsInt() == 42);
    CHECK(v.Get("textures").Items().size() == 2 && v.Get("textures").At(1).AsString().empty());
    Links b;
    Load(b, *json::Parse(json::Write(v)));
    CHECK(Equals(t, &a, &b));
    CHECK(json::Write(Save(b)) == json::Write(v));
    const Links none;
    CHECK(Diff(t, &a, &none) == 0b1111);

    // An id this version does not know is kept as it is (written back unchanged); null = none;
    // a wrong type keeps the value and warns.
    json::Value odd = v;
    odd.Set("model", "made-by-a-newer-version");
    odd.Set("target", nullptr);
    Links c = a;
    Load(c, odd);
    CHECK(c.model.guid == "made-by-a-newer-version" && c.target.Empty());
    CHECK(Save(c).Get("model").AsString() == "made-by-a-newer-version");
    json::Value wrong = v;
    wrong.Set("model", 5);
    wrong.Set("target", -1);
    Links d = a;
    Warnings warn;
    Load(d, wrong, nullptr, &warn);
    CHECK(d.model == a.model && d.target == a.target && warn.size() == 2);

    // Changed through the one undo command, like any field.
    UndoHistory h;
    Links s;
    auto resolve = [&] { return EditTarget{&s, nullptr}; };
    const FieldPath mp{IndexOf(t, "model")};
    h.Push(std::make_unique<SetFieldCommand>(t, mp, json::Value(""), json::Value(a.model.guid), resolve, "モデル"), false);
    CHECK(s.model == a.model);
    h.Undo();
    CHECK(s.model.Empty());
    const FieldPath tp{IndexOf(t, "target")};
    h.Push(std::make_unique<SetFieldCommand>(t, tp, json::Value(uint64_t{0}), json::Value(uint64_t{3}), resolve, "相手"), false);
    CHECK(s.target.id == 3);
    h.Undo();
    CHECK(s.target.Empty());
}

RM_TEST(Reflect_SetFieldCommand_TypingIsOneEntry) {
    Settings s;
    Unknowns u;
    const TypeDesc& t = TypeOf<Settings>();
    UndoHistory h;
    auto resolve = [&] { return EditTarget{&s, &u}; };
    const FieldPath wp{IndexOf(t, "window"), IndexOf(TypeOf<Window>(), "width")};
    const FieldPath hp{IndexOf(t, "window"), IndexOf(TypeOf<Window>(), "height")};
    const int startW = s.window.width;
    // "6", "64", "640" typed into the width field: one entry, undone at once.
    for (int v : {600, 640, 1640}) {
        const PathTarget pt = Resolve(t, &s, &u, wp);
        h.Push(std::make_unique<SetFieldCommand>(t, wp, SaveField(*pt.field, pt.owner, pt.unknowns), json::Value(v), resolve, "幅", 42),
               false);
    }
    CHECK(s.window.width == 1640 && h.UndoCount() == 1);
    // Another field with the same key does not join; no key never joins.
    h.Push(std::make_unique<SetFieldCommand>(t, hp, json::Value(s.window.height), json::Value(500), resolve, "高さ", 42), false);
    h.Push(std::make_unique<SetFieldCommand>(t, hp, json::Value(500), json::Value(510), resolve, "高さ"), false);
    CHECK(h.UndoCount() == 3);
    h.Undo();
    h.Undo();
    h.Undo();
    CHECK(s.window.width == startW);
}

RM_TEST(Reflect_SetFieldCommand_OnlyTheSameObjectJoins) {
    const TypeDesc& t = TypeOf<Settings>();
    const FieldPath wp{IndexOf(t, "window"), IndexOf(TypeOf<Window>(), "width")};
    Settings a, b, c;
    Unknowns ua, ub, uc;
    auto at = [&](Settings& s, Unknowns& u) { return [&s, &u] { return EditTarget{&s, &u}; }; };
    auto set = [&](Settings& s, Unknowns& u, int from, int to, uint64_t key) {
        return std::make_unique<SetFieldCommand>(t, wp, json::Value(from), json::Value(to), at(s, u), "幅", key);
    };
    UndoHistory h;
    // The same key and field, another object (two inspectors, a key used again): two entries.
    h.Push(set(a, ua, a.window.width, 700, 9), false);
    h.Push(set(b, ub, b.window.width, 800, 9), false);
    CHECK(h.UndoCount() == 2 && a.window.width == 700 && b.window.width == 800);

    // Groups join all or nothing: the second group's last target is another object, so nothing of
    // the first one may change (a redo must give 710, not the second group's 720).
    h.Clear();
    auto g1 = std::make_unique<CommandGroup>("幅", 5);
    g1->Add(set(a, ua, 700, 710, 11));
    g1->Add(set(b, ub, 800, 810, 11));
    h.Push(std::move(g1), false);
    auto g2 = std::make_unique<CommandGroup>("幅", 5);
    g2->Add(set(a, ua, 710, 720, 11));
    g2->Add(set(c, uc, c.window.width, 820, 11));
    h.Push(std::move(g2), false);
    CHECK(h.UndoCount() == 2 && a.window.width == 720 && c.window.width == 820);
    CHECK(h.Undo() && h.Undo());
    CHECK(a.window.width == 700 && b.window.width == 800);
    CHECK(h.Redo());
    CHECK(a.window.width == 710 && b.window.width == 810);
    // The same targets in the same order still join (typing into a field of several things).
    auto g3 = std::make_unique<CommandGroup>("幅", 6);
    g3->Add(set(a, ua, 710, 711, 12));
    g3->Add(set(b, ub, 810, 811, 12));
    h.Push(std::move(g3), false);
    auto g4 = std::make_unique<CommandGroup>("幅", 6);
    g4->Add(set(a, ua, 711, 712, 12));
    g4->Add(set(b, ub, 811, 812, 12));
    h.Push(std::move(g4), false);
    CHECK(h.UndoCount() == 2);
}

RM_TEST(Reflect_Object_OwnsAValueOfAnyType) {
    const TypeDesc& t = TypeOf<Settings>();
    Object a(t);
    CHECK(!a.Empty() && a.Type() == &t);
    auto* s = static_cast<Settings*>(a.Get());
    CHECK(s->title == "My Game" && s->speed == 5.0f);  // the default member values
    s->title = "changed";
    s->scores = {1, 2, 3};
    Object b = a;  // a deep copy
    static_cast<Settings*>(b.Get())->scores.push_back(4);
    CHECK(s->scores.size() == 3);
    CHECK(static_cast<Settings*>(b.Get())->title == "changed");
    Object c = std::move(b);
    CHECK(b.Empty() && c.Type() == &t);
    c = a;
    CHECK(static_cast<Settings*>(c.Get())->scores.size() == 3);
    c = Object();
    CHECK(c.Empty());
    CHECK(FindTypeNamed(AllTypes(), t.name) == &t);
    CHECK(FindTypeNamed(AllTypes(), "rmtest::Nothing") == nullptr);
}

RM_TEST(Reflect_ShowIf_EveryTable) {
    // Every ShowIf of every reflected type names a field of its struct and items its enum has (rmgen
    // checks only the names of the fields).
    for (const TypeDesc* t : AllTypes()) {
        for (const FieldDesc& f : t->fields) {
            const std::string problem = ShowIfProblem(*t, f);
            if (!problem.empty()) CHECK(!"ShowIf names something that is not there");
        }
    }
}
