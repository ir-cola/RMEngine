#include "rm_engine/animation/runtime/Rules.h"

#include "base/Json.h"
#include "core/reflect/Data.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

namespace rm::animation {

std::optional<AnimController> ReadController(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns) {
    json::ParseError pe;
    const auto v = json::Parse(bytes, &pe);
    if (!v) {
        if (error) *error = std::format("JSON として読めません ({} 行目: {})", pe.line, pe.message);
        return std::nullopt;
    }
    if (!v->IsObject() || v->Get("kind").AsString() != kControllerKind) {
        if (error) *error = "アニメーター (状態とルール) のファイルではありません";
        return std::nullopt;
    }
    AnimController c;
    reflect::Load(c, *v, unknowns);
    return c;
}

std::string WriteController(const AnimController& c, const reflect::Unknowns* unknowns) {
    AnimController out = c;
    out.kind = kControllerKind;
    return json::Write(reflect::Save(out, unknowns));
}

int CompiledController::FindState(std::string_view name) const {
    for (size_t i = 0; i < states.size(); ++i) {
        if (states[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int CompiledController::FindParam(std::string_view name) const {
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

namespace {

// 状態とルール (体か、1 つのレイヤー) を解く。パラメーターは全体のもの。
CompiledController CompileRules(const std::vector<AnimParam>& params, const std::vector<AnimState>& states, const std::vector<AnimRule>& rules);

}  // namespace

CompiledController Compile(const AnimController& src) {
    CompiledController c = CompileRules(src.params, src.states, src.rules);
    for (const AnimLayer& l : src.layers) {
        CompiledLayer cl;
        cl.name = l.name;
        cl.weight = std::isfinite(l.weight) ? std::clamp(l.weight, 0.f, 1.f) : 1.f;
        cl.weightParam = l.weightParam.empty() ? -1 : c.FindParam(l.weightParam);
        cl.mode = l.mode;
        cl.additiveBase = l.additiveBase;
        cl.mask = l.mask;
        cl.rules = CompileRules(src.params, l.states, l.rules);
        c.layers.push_back(std::move(cl));
    }
    return c;
}

void StateBlend(const CompiledController& c, const ParamValues& v, int state, std::vector<float>& out) {
    out.clear();
    if (state < 0 || static_cast<size_t>(state) >= c.states.size() || static_cast<size_t>(state) >= c.blends.size()) return;
    const AnimState& s = c.states[static_cast<size_t>(state)];
    const CompiledBlend& b = c.blends[static_cast<size_t>(state)];
    auto param = [&](const std::string& name) {
        const int i = c.FindParam(name);
        return i >= 0 && static_cast<size_t>(i) < v.values.size() ? v.values[static_cast<size_t>(i)] : 0.f;
    };
    if (s.kind == StateKind::Blend1D) BlendWeights1D(b.xs, param(s.paramX), out);
    if (s.kind == StateKind::Blend2D) BlendWeights2D(b.points, b.triangles, {param(s.paramX), param(s.paramY)}, out);
}

float LayerWeight(const CompiledLayer& layer, const ParamValues& v) {
    if (layer.weightParam >= 0 && static_cast<size_t>(layer.weightParam) < v.values.size()) {
        const float w = v.values[static_cast<size_t>(layer.weightParam)];
        return std::isfinite(w) ? std::clamp(w, 0.f, 1.f) : 0.f;
    }
    return layer.weight;
}

namespace {

CompiledController CompileRules(const std::vector<AnimParam>& params, const std::vector<AnimState>& states, const std::vector<AnimRule>& rules) {
    CompiledController c;
    c.params = params;
    c.states = states;
    for (const AnimState& s : c.states) {
        CompiledBlend b;
        if (s.kind != StateKind::Clip) {
            for (const BlendMotion& m : s.motions) {
                b.xs.push_back(std::isfinite(m.x) ? m.x : 0.f);
                b.points.push_back({std::isfinite(m.x) ? m.x : 0.f, std::isfinite(m.y) ? m.y : 0.f});
            }
            if (s.kind == StateKind::Blend2D) b.triangles = Triangulate(b.points);
        }
        c.blends.push_back(std::move(b));
    }
    c.rules.reserve(rules.size());
    for (const AnimRule& r : rules) {
        CompiledRule cr;
        cr.from = r.from.empty() ? -1 : c.FindState(r.from);
        cr.to = c.FindState(r.to);
        cr.fade = std::isfinite(r.fade) ? std::clamp(r.fade, 0.f, 10.f) : 0.f;
        cr.interrupt = r.interrupt;
        cr.restart = r.restart;
        cr.condition = Condition::Parse(r.condition);
        if (!cr.condition.ok()) {
            cr.problem = std::format("条件の {} 文字目: {}", cr.condition.column(), cr.condition.error());
        } else if (!r.from.empty() && cr.from < 0) {
            cr.problem = std::format("元の状態「{}」がありません", r.from);
        } else if (cr.to < 0) {
            cr.problem = r.to.empty() ? std::string("先の状態がありません") : std::format("先の状態「{}」がありません", r.to);
        } else {
            for (const std::string& name : cr.condition.names()) {
                if (name != "t" && name != "ended" && c.FindParam(name) < 0) {
                    cr.problem = std::format("「{}」というパラメーターはありません (0 として調べます)", name);
                    break;
                }
            }
        }
        // (知らない名前だけなら当たることはある: 0 として調べる。式の間違い・無い状態は当たらない)
        if (!cr.condition.ok() || (!r.from.empty() && cr.from < 0)) cr.to = -1;
        c.rules.push_back(std::move(cr));
    }
    return c;
}

}  // namespace

ParamValues StartValues(const CompiledController& c) {
    ParamValues v;
    v.values.resize(c.params.size(), 0.f);
    v.age.assign(c.params.size(), -1.f);
    for (size_t i = 0; i < c.params.size(); ++i) {
        const AnimParam& p = c.params[i];
        const float x = std::isfinite(p.value) ? p.value : 0.f;
        switch (p.type) {
            case ParamType::Float: v.values[i] = x; break;
            case ParamType::Int: v.values[i] = std::round(x); break;
            case ParamType::Bool: v.values[i] = x != 0.f ? 1.f : 0.f; break;
            case ParamType::Trigger: v.values[i] = 0.f; break;
        }
    }
    return v;
}

bool SetParam(ParamValues& v, const CompiledController& c, std::string_view name, float value) {
    const int i = c.FindParam(name);
    if (i < 0 || static_cast<size_t>(i) >= v.values.size()) return false;
    const size_t k = static_cast<size_t>(i);
    const float x = std::isfinite(value) ? value : 0.f;
    switch (c.params[k].type) {
        case ParamType::Float: v.values[k] = x; break;
        case ParamType::Int: v.values[k] = std::round(x); break;
        case ParamType::Bool: v.values[k] = x != 0.f ? 1.f : 0.f; break;
        case ParamType::Trigger:
            v.values[k] = x != 0.f ? 1.f : 0.f;
            v.age[k] = x != 0.f ? 0.f : -1.f;
            break;
    }
    return true;
}

bool SetTrigger(ParamValues& v, const CompiledController& c, std::string_view name) { return SetParam(v, c, name, 1.f); }

float ParamOf(const ParamValues& v, const CompiledController& c, std::string_view name) {
    const int i = c.FindParam(name);
    return i >= 0 && static_cast<size_t>(i) < v.values.size() ? v.values[static_cast<size_t>(i)] : 0.f;
}

RuleStep StepRules(const CompiledController& c, ParamValues& v, const StateNow& now, float dt, std::vector<RuleCheck>* checks) {
    RuleStep out;
    if (checks) checks->assign(c.rules.size(), RuleCheck::Below);
    // 1. 出口: 自分のルールだけを調べるか。
    bool ownOnly = false;
    if (now.state >= 0 && static_cast<size_t>(now.state) < c.states.size()) {
        const StateExit exit = c.states[static_cast<size_t>(now.state)].exit;
        ownOnly = exit == StateExit::Own || (exit == StateExit::OwnUntilEnd && !now.ended);
    }
    auto lookup = [&](std::string_view name) -> std::optional<double> {
        if (name == "t") return now.t;
        if (name == "ended") return now.ended ? 1.0 : 0.0;
        const int i = c.FindParam(name);
        if (i < 0 || static_cast<size_t>(i) >= v.values.size()) return std::nullopt;
        return v.values[static_cast<size_t>(i)];
    };
    // 2. 上から調べて、最初に当たった 1 行。
    for (size_t i = 0; i < c.rules.size(); ++i) {
        const CompiledRule& r = c.rules[i];
        RuleCheck result;
        if (r.to < 0) {
            result = RuleCheck::Broken;
        } else if (r.from >= 0 && r.from != now.state) {
            result = RuleCheck::NotHere;
        } else if (ownOnly && !r.interrupt && r.from != now.state) {
            result = RuleCheck::Blocked;
        } else if (out.rule >= 0) {
            result = RuleCheck::Below;
        } else if (r.condition.Test(lookup)) {
            result = RuleCheck::Chosen;
            out.rule = static_cast<int>(i);
        } else {
            result = RuleCheck::False;
        }
        if (checks) (*checks)[i] = result;
        if (result == RuleCheck::Chosen && !checks) break;
    }
    // 3. 先: 今と同じなら、やり直すときだけ。
    if (out.rule >= 0) {
        const CompiledRule& r = c.rules[static_cast<size_t>(out.rule)];
        if (r.to != now.state || r.restart) {
            out.to = r.to;
            out.fade = r.fade;
            out.restart = r.to == now.state;
        }
        // 4. 当たった行が使ったトリガーを消す。
        for (const std::string& name : r.condition.names()) {
            const int p = c.FindParam(name);
            if (p >= 0 && c.params[static_cast<size_t>(p)].type == ParamType::Trigger && static_cast<size_t>(p) < v.values.size()) {
                v.values[static_cast<size_t>(p)] = 0.f;
                v.age[static_cast<size_t>(p)] = -1.f;
            }
        }
    }
    // 使われなかったトリガーは古くなって消える。
    const float step = std::isfinite(dt) && dt > 0 ? dt : 0.f;
    for (size_t i = 0; i < v.values.size() && i < v.age.size(); ++i) {
        if (v.age[i] < 0) continue;
        v.age[i] += step;
        if (v.age[i] > kTriggerHold) {
            v.values[i] = 0.f;
            v.age[i] = -1.f;
        }
    }
    return out;
}

}  // namespace rm::animation
