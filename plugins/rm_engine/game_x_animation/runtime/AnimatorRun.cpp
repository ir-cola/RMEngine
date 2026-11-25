#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"

#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"

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

const space3d::Mat4* BoneModelMatrix(const AnimatorRun& run, std::string_view bone) {
    if (!run.skin || run.version == 0) return nullptr;
    const int i = run.skin->skeleton.Find(bone);
    const auto& model = run.player.ModelMatrices();
    if (i < 0 || static_cast<size_t>(i) >= model.size()) return nullptr;
    return &model[static_cast<size_t>(i)];
}

}  // namespace rm::game_x_animation
