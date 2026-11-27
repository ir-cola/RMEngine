#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"

#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rm::game_x_animation {

namespace {

const space3d::Clip* CurrentClip(const AnimatorRun& run) {
    const int i = run.player.ClipIndex();
    if (!run.clips || i < 0 || static_cast<size_t>(i) >= run.clips->size()) return nullptr;
    return &(*run.clips)[static_cast<size_t>(i)];
}

}  // namespace

AnimatorRun& RunOf(Animator& a, int entity) {
    if (!a.run || a.run->owner != entity) {
        a.run = std::make_shared<AnimatorRun>();
        a.run->owner = entity;
    }
    return *a.run;
}

namespace {

// レイヤーの再生をモデルにつなぎ、マスクと休みの姿勢を作る。
void BindLayers(AnimatorRun& run) {
    run.rest.clear();
    if (run.skin) run.rest = animation3d::RestPose(run.skin->skeleton);
    if (!run.rules) {
        run.layers.clear();
        return;
    }
    run.layers.resize(run.rules->layers.size());
    for (size_t i = 0; i < run.layers.size(); ++i) {
        AnimatorRun::Layer& l = run.layers[i];
        l.state = -1;
        l.rule = -1;
        l.checks.clear();
        if (run.skin && run.clips) {
            l.player.Bind(run.skin->skeleton, *run.clips);
            l.player.BindMorphs(run.skin->morphs, run.skin->morphDefaults);
            l.mask = animation3d::BoneMask(run.skin->skeleton, run.rules->layers[i].mask);
        }
    }
}

}  // namespace

void BindModel(AnimatorRun& run, std::shared_ptr<const space3d::SkinData> skin, std::shared_ptr<const std::vector<space3d::Clip>> clips) {
    run.skin = std::move(skin);
    run.clips = std::move(clips);
    if (run.skin && run.clips) {
        run.player.Bind(run.skin->skeleton, *run.clips);
        run.player.BindMorphs(run.skin->morphs, run.skin->morphDefaults);
    }
    run.morphs = run.skin ? run.skin->morphDefaults : std::vector<float>{};
    BindLayers(run);
    run.started = false;
    run.playing.clear();
    run.bones.clear();
    run.version = 0;
    run.moving = false;
    run.dirty = false;
}

void StepAnimator(AnimatorRun& run, const Animator& a, float dt, std::vector<const space3d::ClipEvent*>& fired, uint64_t (*next)(),
                  const PoseHook* post) {
    run.moving = false;
    run.root = {};
    run.rootActive = false;
    if (!run.skin || !run.clips) return;
    animation3d::Player& p = run.player;
    p.speed = std::isfinite(a.speed) ? a.speed : 1.f;
    p.loop = a.loop;
    bool changed = std::exchange(run.dirty, false);
    if (!run.started || a.clip != run.playing) {
        // 始め・欄のクリップが変わった (インスペクター・スクリプトの PlayClip がまだ読む前に頼んだもの)。
        const float fade = run.started ? std::exchange(run.askedFade, 0.f) : 0.f;
        p.Play(FindClip(*run.clips, a.clip), fade);  // (見つからない: 休みの姿勢)
        if (!run.started) run.paused = !(a.playOnStart || run.asked);
        run.started = true;
        run.playing = a.clip;
        changed = true;
    }
    if (!run.paused) {
        const float before = p.Time();
        const bool fading = p.FadeWeight() < 1.f;
        p.Tick(std::isfinite(dt) && dt > 0 ? dt : 0.f);
        // 根が動いた分 (イベントと同じ区間。体だけ: docs/ANIMATION.md 6 章の 10)。
        run.root = p.RootStep();
        run.rootActive = p.RootActive();
        if (const space3d::Clip* c = CurrentClip(run); c && !c->events.empty()) {
            std::vector<float> times;
            times.reserve(c->events.size());
            for (const space3d::ClipEvent& e : c->events) times.push_back(e.time);
            std::vector<int> hit;
            p.PassedEvents(times, hit);
            for (int i : hit) fired.push_back(&c->events[static_cast<size_t>(i)]);
        }
        changed = changed || fading || p.Time() != before;
    }
    // 重ねるレイヤー: それぞれ進めて、体の姿勢の上に重ねる (重ねている間は毎フレーム作り直す)。
    bool layered = false;
    for (size_t i = 0; i < run.layers.size() && run.rules && i < run.rules->layers.size(); ++i) {
        AnimatorRun::Layer& l = run.layers[i];
        if (!run.paused) l.player.Tick(std::isfinite(dt) && dt > 0 ? dt : 0.f);
        layered = layered || (l.weight > 0 && l.player.ClipIndex() >= 0);
    }
    if (layered || !run.blend.empty()) changed = true;  // (混ぜている・重ねている間は、重みが変わるので毎フレーム)
    const bool hooked = post && *post;
    if (hooked) changed = true;  // (IK・揺れは目標や地面で毎フレーム変わる)
    if (changed || run.version == 0) {
        const animation3d::Pose& base = p.EvaluatePose(run.skin->skeleton);
        (void)base;
        for (size_t i = 0; i < run.layers.size() && run.rules && i < run.rules->layers.size(); ++i) {
            AnimatorRun::Layer& l = run.layers[i];
            if (!(l.weight > 0) || l.player.ClipIndex() < 0) continue;
            const animation3d::Pose& pose = l.player.EvaluatePose(run.skin->skeleton);
            const bool fromFirst = run.rules->layers[i].additiveBase == animation::AdditiveBase::FirstFrame;
            if (fromFirst) l.player.FirstFrame(run.skin->skeleton, l.base, l.baseMorphs);
            if (run.rules->layers[i].mode == animation::LayerMode::Additive) {
                animation3d::AddPose(p.MutablePose(), pose, fromFirst ? l.base : run.rest, l.weight, l.mask);
            } else {
                animation3d::OverlayPose(p.MutablePose(), pose, l.weight, l.mask);
            }
            // 形の重み: 上書きはレイヤーのクリップが持つ形だけ寄せ、足すは初めの重みからの差を足す。
            std::vector<float>& mw = p.MutableMorphWeights();
            const std::vector<float>& lw = l.player.MorphWeights();
            const std::vector<float>& def = fromFirst ? l.baseMorphs : l.player.MorphDefaults();
            for (size_t k = 0; k < mw.size() && k < lw.size(); ++k) {
                if (run.rules->layers[i].mode == animation::LayerMode::Additive) {
                    mw[k] += (lw[k] - (k < def.size() ? def[k] : 0.f)) * l.weight;
                } else if (l.player.MorphAnimated(k)) {
                    mw[k] += (lw[k] - mw[k]) * l.weight;
                }
            }
        }
        // 形の重みの答え (スクリプトの上書きはクリップより強い)。
        run.morphs = p.MorphWeights();
        for (const auto& [name, weight] : run.morphOverride) {
            for (size_t k = 0; k < run.skin->morphs.size() && k < run.morphs.size(); ++k) {
                if (run.skin->morphs[k] == name) run.morphs[k] = weight;
            }
        }
        if (hooked) (*post)(p.MutablePose());
        p.FinishPose(*run.skin, run.bones);
        run.version = next();
    }
    run.moving = changed;
}

bool PlayClip(AnimatorRun& run, Animator& a, std::string_view clip, float fade) {
    a.clip = std::string(clip);
    run.paused = false;
    if (!run.started || !run.clips) {
        // (まだ読んでいない・流し始めていない: 最初の StepAnimator が欄のクリップを流す)
        run.asked = true;
        return !run.clips || FindClip(*run.clips, a.clip) >= 0;
    }
    run.player.speed = std::isfinite(a.speed) ? a.speed : 1.f;
    run.player.loop = a.loop;
    const int index = FindClip(*run.clips, a.clip);
    run.player.Play(index, std::isfinite(fade) && fade > 0 ? fade : 0.f);
    run.playing = a.clip;
    run.dirty = true;
    return index >= 0;
}

float Duration(const AnimatorRun& run) {
    const space3d::Clip* c = CurrentClip(run);
    return c ? c->duration : 0.f;
}

bool Ended(const AnimatorRun& run, const Animator& a) {
    const space3d::Clip* c = CurrentClip(run);
    if (!c || a.loop || !run.started) return false;
    return a.speed < 0 ? run.player.Time() <= 0 : run.player.Time() >= c->duration;
}

namespace {

// 状態の主のクリップ: クリップの状態はそのクリップ、混ぜる状態は 1 つ目のクリップ (足をそろえる時計)。
const std::string& MainClip(const animation::AnimState& s) {
    static const std::string none;
    if (s.kind == animation::StateKind::Clip) return s.clip;
    return s.motions.empty() ? none : s.motions.front().clip;
}

// 混ぜる状態の重みを、今のパラメーターで再生に渡す (クリップの状態: 混ぜない)。
void ApplyBlend(const AnimatorRun& run, const animation::CompiledController& c, int state, std::vector<float>& weights, animation3d::Player& player) {
    animation::StateBlend(c, run.params, state, weights);
    std::vector<animation3d::Player::Motion> motions;
    if (!weights.empty() && run.clips && state >= 0 && static_cast<size_t>(state) < c.states.size()) {
        const animation::AnimState& s = c.states[static_cast<size_t>(state)];
        for (size_t k = 0; k < s.motions.size() && k < weights.size(); ++k) motions.push_back({FindClip(*run.clips, s.motions[k].clip), weights[k]});
    }
    player.SetBlend(motions);
}

// 状態 `index` に入る: そのクリップ・ループ・速さを欄に書き、`fade` 秒で移る。
void EnterState(AnimatorRun& run, Animator& a, int index, float fade) {
    if (!run.rules || index < 0 || static_cast<size_t>(index) >= run.rules->states.size()) return;
    const animation::AnimState& s = run.rules->states[static_cast<size_t>(index)];
    a.loop = s.loop;
    a.speed = std::isfinite(s.speed) ? std::clamp(s.speed, -10.f, 10.f) : 1.f;
    PlayClip(run, a, MainClip(s), fade);
    run.state = index;
    run.stateClock = 0;
    run.looped = false;
    run.lastTime = run.player.Time();
}

// レイヤーの状態 `index` に入る。
void EnterLayerState(AnimatorRun& run, size_t layer, int index, float fade) {
    if (!run.rules || layer >= run.layers.size() || layer >= run.rules->layers.size()) return;
    const animation::CompiledController& c = run.rules->layers[layer].rules;
    AnimatorRun::Layer& l = run.layers[layer];
    if (index < 0 || static_cast<size_t>(index) >= c.states.size()) return;
    const animation::AnimState& s = c.states[static_cast<size_t>(index)];
    l.player.loop = s.loop;
    l.player.speed = std::isfinite(s.speed) ? std::clamp(s.speed, -10.f, 10.f) : 1.f;
    l.player.Play(run.clips ? FindClip(*run.clips, MainClip(s)) : -1, std::isfinite(fade) && fade > 0 ? fade : 0.f);
    l.state = index;
    l.looped = false;
    l.lastTime = l.player.Time();
}

}  // namespace

void BindRules(AnimatorRun& run, Animator& a, std::shared_ptr<const animation::CompiledController> rules) {
    run.rules = std::move(rules);
    run.state = -1;
    run.rule = -1;
    run.checks.clear();
    run.params = {};
    run.blend.clear();
    run.player.SetBlend({});
    BindLayers(run);
    if (!run.rules) return;
    run.params = animation::StartValues(*run.rules);
    for (const auto& [name, value] : run.pendingParams) animation::SetParam(run.params, *run.rules, name, value);
    run.pendingParams.clear();
    EnterState(run, a, 0, 0.f);
    for (size_t i = 0; i < run.layers.size(); ++i) EnterLayerState(run, i, 0, 0.f);
}

void StepAnimatorRules(AnimatorRun& run, Animator& a, float dt) {
    if (!run.rules || run.rules->states.empty()) return;
    if (run.state < 0) EnterState(run, a, 0, 0.f);
    const float step = std::isfinite(dt) && dt > 0 ? dt : 0.f;
    run.stateClock += step;
    // ループが 1 周したか (時刻が戻った: 前向きなら小さく、逆向きなら大きく)。
    const float now = run.player.Time();
    if (run.started && a.loop && !run.paused && ((a.speed >= 0 && now + 1e-4f < run.lastTime) || (a.speed < 0 && now > run.lastTime + 1e-4f))) run.looped = true;
    run.lastTime = now;
    animation::StateNow sn;
    sn.state = run.state;
    sn.t = StateFraction(run);
    sn.ended = a.loop ? run.looped : Ended(run, a);
    const animation::RuleStep r = animation::StepRules(*run.rules, run.params, sn, step, &run.checks);
    if (r.rule >= 0) run.rule = r.rule;
    if (r.to >= 0) EnterState(run, a, r.to, r.fade);
    ApplyBlend(run, *run.rules, run.state, run.blend, run.player);
    // 重ねるレイヤー: それぞれのルール (パラメーターは同じもの)。
    for (size_t i = 0; i < run.layers.size() && i < run.rules->layers.size(); ++i) {
        const animation::CompiledLayer& cl = run.rules->layers[i];
        AnimatorRun::Layer& l = run.layers[i];
        l.weight = animation::LayerWeight(cl, run.params);
        for (const auto& [name, weight] : run.layerWeights) {
            if (name == cl.name) l.weight = weight;  // (スクリプトの SetLayerWeight)
        }
        if (cl.rules.states.empty()) continue;
        if (l.state < 0) EnterLayerState(run, i, 0, 0.f);
        const float lt = l.player.Time();
        if (l.player.loop && l.player.speed >= 0 && lt + 1e-4f < l.lastTime) l.looped = true;
        l.lastTime = lt;
        const space3d::Clip* lc = run.clips && l.player.ClipIndex() >= 0 ? &(*run.clips)[static_cast<size_t>(l.player.ClipIndex())] : nullptr;
        animation::StateNow ln;
        ln.state = l.state;
        ln.t = lc && lc->duration > 0 ? std::clamp(lt / lc->duration, 0.f, 1.f) : 0.f;
        ln.ended = l.player.loop ? l.looped : (lc && lt >= lc->duration);
        const animation::RuleStep lr = animation::StepRules(cl.rules, run.params, ln, 0.f, &l.checks);
        if (lr.rule >= 0) l.rule = lr.rule;
        if (lr.to >= 0) EnterLayerState(run, i, lr.to, lr.fade);
        ApplyBlend(run, cl.rules, l.state, l.blend, l.player);
    }
}

bool PlayState(AnimatorRun& run, Animator& a, std::string_view state, float fade) {
    if (!run.rules) return false;
    const int i = run.rules->FindState(state);
    if (i < 0) return false;
    EnterState(run, a, i, std::isfinite(fade) && fade > 0 ? fade : 0.f);
    return true;
}

float StateFraction(const AnimatorRun& run) {
    const float d = Duration(run);
    if (!(d > 0)) return 0.f;
    return std::clamp(run.player.Time() / d, 0.f, 1.f);
}

std::string_view StateName(const AnimatorRun& run) {
    if (!run.rules || run.state < 0 || static_cast<size_t>(run.state) >= run.rules->states.size()) return {};
    return run.rules->states[static_cast<size_t>(run.state)].name;
}

bool SetRuleParam(AnimatorRun& run, std::string_view name, float value, bool trigger) {
    if (!run.rules) {
        run.pendingParams.emplace_back(std::string(name), trigger ? 1.f : value);
        return true;
    }
    return trigger ? animation::SetTrigger(run.params, *run.rules, name) : animation::SetParam(run.params, *run.rules, name, value);
}

float RuleParam(const AnimatorRun& run, std::string_view name) {
    if (!run.rules) {
        for (auto it = run.pendingParams.rbegin(); it != run.pendingParams.rend(); ++it) {
            if (it->first == name) return it->second;
        }
        return 0.f;
    }
    return animation::ParamOf(run.params, *run.rules, name);
}

void SetBlendShape(AnimatorRun& run, std::string_view name, float weight) {
    std::erase_if(run.morphOverride, [&](const auto& kv) { return kv.first == name; });
    if (std::isfinite(weight) && weight >= 0) run.morphOverride.emplace_back(std::string(name), weight);
    run.dirty = true;  // (次の StepAnimator で作り直す)
}

float BlendShapeOf(const AnimatorRun& run, std::string_view name) {
    if (run.skin) {
        for (size_t k = 0; k < run.skin->morphs.size() && k < run.morphs.size(); ++k) {
            if (run.skin->morphs[k] == name) return run.morphs[k];
        }
    }
    for (const auto& [n, w] : run.morphOverride) {
        if (n == name) return w;
    }
    return 0.f;
}

void SetLayerWeight(AnimatorRun& run, std::string_view layer, float weight) {
    std::erase_if(run.layerWeights, [&](const auto& kv) { return kv.first == layer; });
    if (std::isfinite(weight) && weight >= 0) run.layerWeights.emplace_back(std::string(layer), std::min(weight, 1.f));
}

float LayerWeightOf(const AnimatorRun& run, std::string_view layer) {
    if (run.rules) {
        for (size_t i = 0; i < run.rules->layers.size() && i < run.layers.size(); ++i) {
            if (run.rules->layers[i].name == layer) return run.layers[i].weight;
        }
    }
    for (const auto& [name, weight] : run.layerWeights) {
        if (name == layer) return weight;
    }
    return 0.f;
}

const space3d::Mat4* BoneModelMatrix(const AnimatorRun& run, std::string_view bone) {
    if (!run.skin || run.version == 0) return nullptr;
    const int i = run.skin->skeleton.Find(bone);
    const auto& model = run.player.ModelMatrices();
    if (i < 0 || static_cast<size_t>(i) >= model.size()) return nullptr;
    return &model[static_cast<size_t>(i)];
}

}  // namespace rm::game_x_animation
