#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"

#include "rm_engine/animation3d/runtime/ClipTime.h"

#include <algorithm>
#include <cmath>

namespace rm::game_x_animation {

int FindClip(std::span<const space3d::Clip> clips, std::string_view name) {
    if (clips.empty()) return -1;
    if (name.empty()) return 0;
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

Showing WhatToShow(bool gamePlaying, bool playOnStart, bool previewed, bool hasClip) {
    if (!hasClip) return Showing::BindPose;
    if (gamePlaying) return playOnStart ? Showing::Playing : Showing::FirstFrame;
    return previewed ? Showing::Preview : Showing::FirstFrame;
}

float StartTime(float speed, float duration, bool loop) { return speed < 0 && !loop ? std::max(duration, 0.f) : 0.f; }

void FollowSelection(PreviewClock& clock, int entity) {
    if (clock.entity == entity) return;
    clock = {};
    clock.entity = entity;
}

void AdvancePreview(PreviewClock& clock, float dt, float speed, float duration, bool loop) {
    if (!std::isfinite(speed)) speed = 0;
    if (clock.fresh) {
        clock.fresh = false;
        clock.time = StartTime(speed, duration, loop);
        return;  // (このフレームは始めのコマ)
    }
    if (clock.paused || !(dt > 0) || !std::isfinite(dt)) {
        // (止まっていても、クリップが短くなったら中に収める)
        clock.time = std::clamp(clock.time, 0.f, std::max(duration, 0.f));
        return;
    }
    const animation3d::TimeStep step = animation3d::AdvanceTime(clock.time, dt, speed, duration, loop);
    clock.time = step.time;
    if (!loop && duration > 0 && ((speed > 0 && clock.time >= duration) || (speed < 0 && clock.time <= 0))) clock.paused = true;
}

void TogglePreview(PreviewClock& clock, float speed, float duration, bool loop) {
    if (clock.paused && !loop) {
        const bool atEnd = speed < 0 ? clock.time <= 0 : clock.time >= duration;
        if (atEnd) clock.time = StartTime(speed, duration, loop);
    }
    clock.paused = !clock.paused;
    clock.fresh = false;
}

void SeekPreview(PreviewClock& clock, float time, float duration) {
    clock.time = std::isfinite(time) ? std::clamp(time, 0.f, std::max(duration, 0.f)) : 0.f;
    clock.paused = true;
    clock.fresh = false;
}

bool PoseStamp::Update(const void* model, int clip, float time, bool loop, uint64_t (*next)()) {
    if (version_ != 0 && model == model_ && clip == clip_ && time == time_ && loop == loop_) return false;
    model_ = model;
    clip_ = clip;
    time_ = time;
    loop_ = loop;
    version_ = next();
    return true;
}

}  // namespace rm::game_x_animation
