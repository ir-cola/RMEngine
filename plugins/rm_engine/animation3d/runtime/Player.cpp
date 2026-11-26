#include "rm_engine/animation3d/runtime/Player.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rm::animation3d {

void Player::Bind(const space3d::Skeleton& skeleton, std::span<const space3d::Clip> clips) {
    clips_ = clips;
    bindings_.clear();
    bindings_.reserve(clips.size());
    for (const space3d::Clip& c : clips) bindings_.push_back(BindClip(c, skeleton));
    clip_ = -1;
    time_ = prevTime_ = 0;
    last_ = {};
    fresh_ = lastFresh_ = false;
    fadeLength_ = fadeLeft_ = 0;
}

const space3d::Clip* Player::Current() const {
    if (clip_ < 0 || static_cast<size_t>(clip_) >= clips_.size()) return nullptr;
    return &clips_[static_cast<size_t>(clip_)];
}

void Player::Play(int clipIndex, float fade) {
    // 今見えている姿勢を固めて、そこから移る (まだ Evaluate していなければ休みの姿勢から)
    if (fade > 0 && std::isfinite(fade)) {
        from_ = pose_;
        fadeLength_ = fadeLeft_ = fade;
    } else {
        fadeLength_ = fadeLeft_ = 0;
    }
    clip_ = clipIndex >= 0 && static_cast<size_t>(clipIndex) < clips_.size() ? clipIndex : -1;
    const space3d::Clip* c = Current();
    // 逆再生は終わりから (ループするクリップの終わりは始めと同じ所なので 0 のまま)
    time_ = c && speed < 0 && !loop ? c->duration : 0.f;
    prevTime_ = time_;
    last_ = {time_, 0};
    fresh_ = c != nullptr;
}

void Player::SetBlend(std::span<const Motion> motions) { blend_.assign(motions.begin(), motions.end()); }

float Player::BlendDuration() const {
    const space3d::Clip* c = Current();
    if (blend_.empty()) return c ? c->duration : 0.f;
    double total = 0, weight = 0;
    for (const Motion& m : blend_) {
        if (!(m.weight > 0) || m.clip < 0 || static_cast<size_t>(m.clip) >= clips_.size()) continue;
        total += static_cast<double>(m.weight) * clips_[static_cast<size_t>(m.clip)].duration;
        weight += m.weight;
    }
    if (!(weight > 0)) return c ? c->duration : 0.f;
    return static_cast<float>(total / weight);
}

TimeStep Player::Tick(float dt) {
    if (!(dt > 0) || !std::isfinite(dt)) dt = 0;
    if (fadeLeft_ > 0) fadeLeft_ = std::max(0.f, fadeLeft_ - dt);
    prevTime_ = time_;
    lastFresh_ = fresh_;
    const space3d::Clip* c = Current();
    if (!c) {
        last_ = {time_, 0};
        lastFresh_ = false;
        return last_;
    }
    fresh_ = false;
    // 足をそろえる: 主のクリップの時刻を、割合が「混ぜた長さ」で進むように進める。
    float pace = speed;
    if (!blend_.empty()) {
        const float mixed = BlendDuration();
        if (mixed > 0 && c->duration > 0) pace = speed * c->duration / mixed;
    }
    last_ = AdvanceTime(time_, dt, pace, c->duration, loop);
    time_ = last_.time;
    return last_;
}

void Player::PassedEvents(std::span<const float> eventTimes, std::vector<int>& out) const {
    const space3d::Clip* c = Current();
    if (!c) return;
    animation3d::PassedEvents(prevTime_, last_, speed, c->duration, eventTimes, out, lastFresh_);
}

void Player::SetTime(float time) {
    const space3d::Clip* c = Current();
    if (!c || !std::isfinite(time)) {
        time_ = 0;
    } else if (loop && c->duration > 0) {
        time_ = AdvanceTime(time, 0, 0, c->duration, true).time;  // [0, duration) に回す
    } else {
        time_ = std::clamp(time, 0.f, std::max(c->duration, 0.f));
    }
    prevTime_ = time_;
    last_ = {time_, 0};
}

float Player::FadeWeight() const { return fadeLeft_ > 0 && fadeLength_ > 0 ? 1.f - fadeLeft_ / fadeLength_ : 1.f; }

void Player::Evaluate(const space3d::SkinData& skin, std::vector<space3d::BoneMatrix>& out) {
    EvaluatePose(skin.skeleton);
    FinishPose(skin, out);
}

const Pose& Player::EvaluatePose(const space3d::Skeleton& skeleton) {
    const space3d::Clip* c = Current();
    bool mixed = false;
    if (c && !blend_.empty()) {
        // 混ぜる: どのクリップも、主のクリップと同じ割合の時刻で。
        const float phase = c->duration > 0 ? time_ / c->duration : 0.f;
        mix_.Begin(skeleton.bones.size());
        for (const Motion& m : blend_) {
            if (!(m.weight > 0) || m.clip < 0 || static_cast<size_t>(m.clip) >= clips_.size()) continue;
            const space3d::Clip& mc = clips_[static_cast<size_t>(m.clip)];
            SampleClip(mc, bindings_[static_cast<size_t>(m.clip)], skeleton, phase * mc.duration, loop, part_);
            mix_.Add(part_, m.weight);
        }
        if (mix_.TotalWeight() > 0) {
            mix_.Finish(skeleton, sample_);
            mixed = true;
        }
    }
    if (!mixed) {
        if (c) {
            SampleClip(*c, bindings_[static_cast<size_t>(clip_)], skeleton, time_, loop, sample_);
        } else {
            sample_ = RestPose(skeleton);
        }
    }
    if (FadeWeight() < 1.f) {
        if (from_.size() != skeleton.bones.size()) from_ = RestPose(skeleton);
        Blend(from_, sample_, FadeWeight(), pose_);
    } else {
        std::swap(pose_, sample_);  // sample_ は次のフレームで全部書き直す
    }
    return pose_;
}

void Player::FinishPose(const space3d::SkinData& skin, std::vector<space3d::BoneMatrix>& out) {
    animation3d::ModelMatrices(skin.skeleton, pose_, model_);
    SkinMatrices(model_, skin.inverseBind, out);
}

}  // namespace rm::animation3d
