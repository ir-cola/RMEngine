// アニメーション (plugins/rm_engine/animation/runtime) のテスト: 条件の小さな式 (Condition) と、ルールの並びを
// 上から調べる決まり (Rules: docs/ANIMATION.md 4-7・4-8)。

#include "Test.h"

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "rm_engine/animation/runtime/Blend.h"
#include "rm_engine/animation/runtime/Condition.h"
#include "rm_engine/animation/runtime/Controller.h"
#include "rm_engine/animation/runtime/Rules.h"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::animation;

namespace {

double Eval(std::string_view text, const std::map<std::string, double>& vars = {}) {
    const Condition c = Condition::Parse(text);
    return c.Evaluate([&](std::string_view n) -> std::optional<double> {
        const auto it = vars.find(std::string(n));
        if (it == vars.end()) return std::nullopt;
        return it->second;
    });
}

AnimController Sample() {
    AnimController c;
    c.params = {{"speed", ParamType::Float, 0}, {"grounded", ParamType::Bool, 1}, {"jump", ParamType::Trigger, 0}, {"hp", ParamType::Int, 10}};
    AnimState idle{"Idle", "Idle", true, 1, StateExit::Free};
    AnimState move{"Move", "Run", true, 1, StateExit::Free};
    AnimState jump{"Jump", "Jump", true, 1, StateExit::Own};
    AnimState land{"Land", "Land", false, 1, StateExit::OwnUntilEnd};
    AnimState die{"Die", "Die", false, 1, StateExit::Own};
    c.states = {idle, move, jump, land, die};
    c.rules = {
        {"", "hp <= 0", "Die", 0.1f, true, false},
        {"", "jump", "Jump", 0.1f, false, false},
        {"Jump", "grounded", "Land", 0.05f, false, false},
        {"Land", "ended", "Idle", 0.2f, false, false},
        {"", "speed > 0.1", "Move", 0.2f, false, false},
        {"", "", "Idle", 0.2f, false, false},
    };
    return c;
}

}  // namespace

RM_TEST(Animation_Condition) {
    // 強さ (C++ と同じ)・つなぐ・比べる・計算・関数。
    CHECK(Eval("1 + 2 * 3") == 7);
    CHECK(Eval("(1 + 2) * 3") == 9);
    CHECK(Eval("-2 + 5") == 3);
    CHECK(Eval("1 < 2 && 3 >= 3") == 1 && Eval("1 > 2 || 2 != 2") == 0 && Eval("!(1 == 1)") == 0);
    CHECK(Eval("true && !false") == 1);
    CHECK(Eval("abs(-3) + min(4, 2) + max(1, 5)") == 10);
    CHECK(Eval("1 / 0") == 0);  // 0 で割ると 0
    CHECK(Eval("speed > 0.1 && grounded", {{"speed", 0.5}, {"grounded", 1}}) == 1);
    CHECK(Eval("speed > 0.1 && grounded", {{"speed", 0.0}, {"grounded", 1}}) == 0);
    // 日本語の名前。
    CHECK(Eval("速さ > 1", {{"速さ", 2}}) == 1);
    // 空: いつも真。
    CHECK(Condition::Parse("").empty() && Eval("") == 1 && Eval("   ") == 1);
    // 名前を集める (重なりなし)。知らない名前は 0 として、教える。
    const Condition c = Condition::Parse("a > 1 || b && a");
    CHECK(c.ok() && c.names() == std::vector<std::string>({"a", "b"}));
    std::vector<std::string> unknown;
    CHECK(c.Evaluate([](std::string_view n) -> std::optional<double> { return n == "a" ? std::optional<double>(2) : std::nullopt; }, &unknown) == 1);
    CHECK(unknown == std::vector<std::string>({"b"}));
    // 間違い: 何文字目か (日本語も 1 文字は 1)。間違いのある式は当たらない。
    const Condition bad = Condition::Parse("速さ > ");
    CHECK(!bad.ok() && bad.column() == 6 && !bad.error().empty() && bad.Evaluate({}) == 0);
    const Condition paren = Condition::Parse("(a > 1");
    CHECK(!paren.ok() && paren.column() == 7);
    const Condition chr = Condition::Parse("a # b");
    CHECK(!chr.ok() && chr.column() == 3);
    const Condition fn = Condition::Parse("sqrt(4)");
    CHECK(!fn.ok() && fn.column() == 1);
    CHECK(!Condition::Parse("min(1)").ok() && !Condition::Parse("1 2").ok());
}

RM_TEST(Animation_Rules) {
    const CompiledController c = Compile(Sample());
    CHECK(c.states.size() == 5 && c.rules.size() == 6);
    for (const CompiledRule& r : c.rules) CHECK(r.problem.empty());
    ParamValues v = StartValues(c);
    CHECK(ParamOf(v, c, "grounded") == 1 && ParamOf(v, c, "hp") == 10 && ParamOf(v, c, "jump") == 0);
    // 待機: (いつも) → Idle は同じ状態なので移らない。
    std::vector<RuleCheck> checks;
    RuleStep s = StepRules(c, v, {0, 0, false}, 0.016f, &checks);
    CHECK(s.rule == 5 && s.to == -1);
    CHECK(checks[0] == RuleCheck::False && checks[4] == RuleCheck::False && checks[5] == RuleCheck::Chosen && checks[2] == RuleCheck::NotHere);
    // 速さ → Move (0.2 秒)。上の行が先。
    CHECK(SetParam(v, c, "speed", 2));
    s = StepRules(c, v, {0, 0, false}, 0.016f, &checks);
    CHECK(s.rule == 4 && s.to == 1 && s.fade == 0.2f && checks[5] == RuleCheck::Below);
    // トリガー: jump → Jump (当たったら消える)。
    CHECK(SetTrigger(v, c, "jump") && ParamOf(v, c, "jump") == 1);
    s = StepRules(c, v, {1, 0.3f, false}, 0.016f);
    CHECK(s.rule == 1 && s.to == 2 && ParamOf(v, c, "jump") == 0);
    // Jump の出口は「自分のルールだけ」: speed があっても Move に戻らない。地面に着いたら Land。
    SetParam(v, c, "grounded", 0);
    s = StepRules(c, v, {2, 0.5f, false}, 0.016f, &checks);
    CHECK(s.to == -1 && checks[4] == RuleCheck::Blocked && checks[2] == RuleCheck::False);
    SetParam(v, c, "grounded", 1);
    s = StepRules(c, v, {2, 0.5f, false}, 0.016f);
    CHECK(s.rule == 2 && s.to == 3 && s.fade == 0.05f);
    // Land: 終わるまで自分のルールだけ → 終わったら Idle (ended)。
    s = StepRules(c, v, {3, 0.5f, false}, 0.016f, &checks);
    CHECK(s.to == -1 && checks[4] == RuleCheck::Blocked);
    s = StepRules(c, v, {3, 1.f, true}, 0.016f);
    CHECK(s.rule == 3 && s.to == 0);
    // ⚡ は出口を越える: Jump の途中でも hp <= 0 → Die。
    SetParam(v, c, "hp", 0);
    s = StepRules(c, v, {2, 0.2f, false}, 0.016f);
    CHECK(s.rule == 0 && s.to == 4);
    // 使われなかったトリガーは 0.2 秒で消える。
    SetParam(v, c, "hp", 10);
    SetTrigger(v, c, "jump");
    StepRules(c, v, {2, 0.2f, false}, 0.1f);  // (Jump の出口: jump の行は調べない)
    CHECK(ParamOf(v, c, "jump") == 1);
    StepRules(c, v, {2, 0.2f, false}, 0.15f);
    CHECK(ParamOf(v, c, "jump") == 0);
    // 値の形: 整数は丸め、真偽は 0 / 1。無い名前は入らない。
    SetParam(v, c, "hp", 2.6f);
    SetParam(v, c, "grounded", 5);
    CHECK(ParamOf(v, c, "hp") == 3 && ParamOf(v, c, "grounded") == 1 && !SetParam(v, c, "nothing", 1));
}

RM_TEST(Animation_RulesProblems) {
    AnimController a = Sample();
    a.rules.push_back({"", "speed >", "Move", 0.1f, false, false});       // 式の間違い
    a.rules.push_back({"Nowhere", "speed > 0", "Move", 0.1f, false, false});  // 無い元
    a.rules.push_back({"", "speed > 0", "Fly", 0.1f, false, false});      // 無い先
    a.rules.push_back({"", "power > 1", "Move", 0.1f, false, false});     // 知らない名前 (0 として調べる)
    a.rules.insert(a.rules.begin(), {"", "restart_me", "Idle", 0.f, false, true});  // やり直す (知らない名前: 偽)
    const CompiledController c = Compile(a);
    CHECK(c.rules[7].problem.find("1") != std::string::npos || !c.rules[7].problem.empty());
    CHECK(!c.rules[7].problem.empty() && c.rules[7].to == -1);
    CHECK(!c.rules[8].problem.empty() && c.rules[8].to == -1);
    CHECK(!c.rules[9].problem.empty() && c.rules[9].to == -1);
    CHECK(!c.rules[10].problem.empty() && c.rules[10].to == 1);  // (当たりうる)
    ParamValues v = StartValues(c);
    std::vector<RuleCheck> checks;
    StepRules(c, v, {0, 0, false}, 0.016f, &checks);
    CHECK(checks[7] == RuleCheck::Broken && checks[8] == RuleCheck::Broken && checks[9] == RuleCheck::Broken);
    // やり直す: 同じ状態でも始めから。
    AnimController r = Sample();
    r.rules = {{"", "", "Idle", 0.1f, false, true}};
    const CompiledController rc = Compile(r);
    ParamValues rv = StartValues(rc);
    const RuleStep s = StepRules(rc, rv, {0, 0.5f, false}, 0.016f);
    CHECK(s.to == 0 && s.restart);
    // ファイル: 種類を確かめて読む (保存は型の表から)。
    const std::string text = json::Write(reflect::Save(Sample()));
    std::string err;
    const auto back = ReadController(text, &err);
    CHECK(back && back->rules.size() == 6 && back->states[2].exit == StateExit::Own && back->params[2].type == ParamType::Trigger);
    CHECK(!ReadController(R"({"kind": "other"})", &err) && !err.empty());
    CHECK(!ReadController("{", &err));
}

// 混ぜる重み: 1D は両隣、2D は三角形の重心座標・外は一番近い辺・一直線は 1D。
RM_TEST(Animation_BlendWeights) {
    auto same = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    std::vector<float> w;
    const std::vector<float> line{0.f, 4.f, 1.5f, 7.f};  // (並んでいなくてよい: Idle 0・Run 4・Walk 1.5・Sprint 7)
    animation::BlendWeights1D(line, 2.75f, w);
    CHECK(w.size() == 4 && same(w[2], 0.5f) && same(w[1], 0.5f) && w[0] == 0 && w[3] == 0);
    animation::BlendWeights1D(line, -3.f, w);
    CHECK(w[0] == 1 && w[1] == 0);
    animation::BlendWeights1D(line, 99.f, w);
    CHECK(w[3] == 1);
    animation::BlendWeights1D(line, 1.5f, w);
    CHECK(same(w[2], 1.f));
    animation::BlendWeights1D({}, 1.f, w);
    CHECK(w.empty());
    // 2D: 十字 (前・左・真ん中・右・後ろ)。
    const std::vector<Vec2> cross{{0, 1}, {-1, 0}, {0, 0}, {1, 0}, {0, -1}};
    const auto tris = animation::Triangulate(cross);
    CHECK(tris.size() == 4);
    animation::BlendWeights2D(cross, tris, {0, 0}, w);
    CHECK(same(w[2], 1.f));
    animation::BlendWeights2D(cross, tris, {0.5f, 0.25f}, w);
    float sum = 0;
    for (float x : w) sum += x;
    CHECK(same(sum, 1.f) && same(w[3], 0.5f) && same(w[0], 0.25f) && same(w[2], 0.25f) && w[1] == 0 && w[4] == 0);
    // 外: 一番近い辺 (前と右の間の辺の外 → その 2 点)。
    animation::BlendWeights2D(cross, tris, {1, 1}, w);
    CHECK(same(w[0], 0.5f) && same(w[3], 0.5f));
    // 一直線 (三角形が無い): 1D として。
    const std::vector<Vec2> row{{0, 0}, {2, 0}, {1, 0}};
    const auto none = animation::Triangulate(row);
    CHECK(none.empty());
    animation::BlendWeights2D(row, none, {1.5f, 3.f}, w);
    CHECK(same(w[2], 0.5f) && same(w[1], 0.5f) && w[0] == 0);
    // 点が 1 つ・2 つ。
    animation::BlendWeights2D(std::vector<Vec2>{{5, 5}}, {}, {0, 0}, w);
    CHECK(w.size() == 1 && w[0] == 1);
    animation::BlendWeights2D(std::vector<Vec2>{{0, 0}, {4, 0}}, {}, {1, 9}, w);
    CHECK(same(w[0], 0.75f) && same(w[1], 0.25f));
}
