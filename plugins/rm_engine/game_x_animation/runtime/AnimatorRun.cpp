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

void BindModel(AnimatorRun& run, std::shared_ptr<const space3d::SkinData> skin, std::shared_ptr<const std::vector<space3d::Clip>> clips) {
    run.skin = std::move(skin);
    run.clips = std::move(clips);
    if (run.skin && run.clips) run.player.Bind(run.skin->skeleton, *run.clips);
    run.started = false;
    run.playing.clear();
    run.bones.clear();
    run.version = 0;
    run.moving = false;
    run.dirty = false;
}

void StepAnimator(AnimatorRun& run, const Animator& a, float dt, std::vector<const space3d::ClipEvent*>& fired, uint64_t (*next)()) {
    run.moving = false;
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
    if (changed || run.version == 0) {
        p.Evaluate(*run.skin, run.bones);
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

// 状態 `index` に入る: そのクリップ・ループ・速さを欄に書き、`fade` 秒で移る。
void EnterState(AnimatorRun& run, Animator& a, int index, float fade) {
    if (!run.rules || index < 0 || static_cast<size_t>(index) >= run.rules->states.size()) return;
    const animation::AnimState& s = run.rules->states[static_cast<size_t>(index)];
    a.loop = s.loop;
    a.speed = std::isfinite(s.speed) ? std::clamp(s.speed, -10.f, 10.f) : 1.f;
    PlayClip(run, a, s.clip, fade);
    run.state = index;
    run.stateClock = 0;
    run.looped = false;
    run.lastTime = run.player.Time();
}

}  // namespace

void BindRules(AnimatorRun& run, Animator& a, std::shared_ptr<const animation::CompiledController> rules) {
    run.rules = std::move(rules);
    run.state = -1;
    run.rule = -1;
    run.checks.clear();
    run.params = {};
    if (!run.rules) return;
    run.params = animation::StartValues(*run.rules);
    for (const auto& [name, value] : run.pendingParams) animation::SetParam(run.params, *run.rules, name, value);
    run.pendingParams.clear();
    EnterState(run, a, 0, 0.f);
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

const space3d::Mat4* BoneModelMatrix(const AnimatorRun& run, std::string_view bone) {
    if (!run.skin || run.version == 0) return nullptr;
    const int i = run.skin->skeleton.Find(bone);
    const auto& model = run.player.ModelMatrices();
    if (i < 0 || static_cast<size_t>(i) >= model.size()) return nullptr;
    return &model[static_cast<size_t>(i)];
}

}  // namespace rm::game_x_animation
