#include "rm_engine/animation3d/runtime/Player.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rm::animation3d {

void Player::Bind(const space3d::Skeleton& skeleton, std::span<const space3d::Clip> clips) {
    clips_ = clips;
    skeleton_ = &skeleton;
    bindings_.clear();
    bindings_.reserve(clips.size());
    rootBone_.clear();
    rootStart_.clear();
    for (const space3d::Clip& c : clips) {
        bindings_.push_back(BindClip(c, skeleton));
        const int root = c.root.enabled ? RootBone(skeleton, c.root.bone) : -1;
        rootBone_.push_back(root);
        rootStart_.push_back(root >= 0 ? SampleBone(c, bindings_.back(), skeleton, root, 0.f, false) : space3d::BoneTRS{});
    }
    root_ = frozenVelocity_ = lastVelocity_ = {};
    rootActive_ = false;
    BindMorphs(morphNames_, morphDefaults_);  // (クリップが変わった: 曲線を引き直す)
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
        fromMorph_ = morph_;
        fadeLength_ = fadeLeft_ = fade;
        frozenVelocity_ = lastVelocity_;  // (止め絵と一緒に、その時の動きも止めておく)
    } else {
        fadeLength_ = fadeLeft_ = 0;
        frozenVelocity_ = {};
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

void Player::BindMorphs(std::span<const std::string> names, std::span<const float> defaults) {
    std::vector<std::string> n(names.begin(), names.end());
    std::vector<float> d(defaults.begin(), defaults.end());
    morphNames_ = std::move(n);
    morphDefaults_ = std::move(d);
    morphDefaults_.resize(morphNames_.size(), 0.f);
    morphTrack_.assign(clips_.size(), {});
    for (size_t c = 0; c < clips_.size(); ++c) {
        morphTrack_[c].assign(morphNames_.size(), -1);
        const auto& tracks = clips_[c].morphs;
        for (size_t t = 0; t < tracks.size(); ++t) {
            for (size_t k = 0; k < morphNames_.size(); ++k) {
                if (morphTrack_[c][k] < 0 && tracks[t].target == morphNames_[k]) morphTrack_[c][k] = static_cast<int>(t);
            }
        }
    }
    morph_ = morphDefaults_;
    fromMorph_ = morphDefaults_;
}

bool Player::MorphAnimated(size_t k) const {
    auto has = [&](int clip) {
        return clip >= 0 && static_cast<size_t>(clip) < morphTrack_.size() && k < morphTrack_[static_cast<size_t>(clip)].size() &&
               morphTrack_[static_cast<size_t>(clip)][k] >= 0;
    };
    if (has(clip_)) return true;
    for (const Motion& m : blend_) {
        if (m.weight > 0 && has(m.clip)) return true;
    }
    return false;
}

void Player::FirstFrame(const space3d::Skeleton& skeleton, Pose& out, std::vector<float>& morphs) const {
    const space3d::Clip* c = Current();
    if (!c) {
        out = RestPose(skeleton);
        morphs = morphDefaults_;
        return;
    }
    const float start = speed < 0 && !loop ? c->duration : 0.f;
    SampleClip(*c, bindings_[static_cast<size_t>(clip_)], skeleton, start, loop, out);
    Strip(clip_, out);
    MorphsAt(clip_, start, morphs);
}

void Player::MorphsAt(int clip, float time, std::vector<float>& out) const {
    out = morphDefaults_;
    if (clip < 0 || static_cast<size_t>(clip) >= morphTrack_.size()) return;
    const std::vector<int>& tracks = morphTrack_[static_cast<size_t>(clip)];
    for (size_t k = 0; k < tracks.size() && k < out.size(); ++k) {
        if (tracks[k] >= 0) out[k] = SampleMorph(clips_[static_cast<size_t>(clip)], tracks[k], time, loop);
    }
}

int Player::RootBoneOf(int clip) const {
    return clip >= 0 && static_cast<size_t>(clip) < rootBone_.size() ? rootBone_[static_cast<size_t>(clip)] : -1;
}

RootDelta Player::RootTotalsOf(int clip) const {
    const int root = RootBoneOf(clip);
    if (root < 0 || !skeleton_) return {};
    const space3d::Clip& c = clips_[static_cast<size_t>(clip)];
    return RootTotals(c, bindings_[static_cast<size_t>(clip)], *skeleton_, root, c.root);
}

RootDelta Player::ClipRoot(int clip, float prev, const TimeStep& step) const {
    const int root = RootBoneOf(clip);
    if (root < 0 || !skeleton_) return {};
    const space3d::Clip& c = clips_[static_cast<size_t>(clip)];
    return RootDeltaBetween(c, bindings_[static_cast<size_t>(clip)], *skeleton_, root, c.root, prev, step, speed, loop);
}

void Player::Strip(int clip, Pose& pose) const {
    const int root = RootBoneOf(clip);
    if (!stripRoot || root < 0) return;
    StripRoot(pose, root, clips_[static_cast<size_t>(clip)].root, rootStart_[static_cast<size_t>(clip)]);
}

TimeStep Player::Tick(float dt) {
    if (!(dt > 0) || !std::isfinite(dt)) dt = 0;
    if (fadeLeft_ > 0) fadeLeft_ = std::max(0.f, fadeLeft_ - dt);
    prevTime_ = time_;
    lastFresh_ = fresh_;
    // ルートモーション: フェードの間は、止め絵の速さを (1 - 重み) だけ足す。
    const float fadeW = FadeWeight();
    const RootDelta& fv = frozenVelocity_;
    const RootDelta frozen{{fv.move.x * dt, fv.move.y * dt, fv.move.z * dt}, fv.turn * dt};
    const bool frozenMoves = fadeW < 1.f && (fv.turn != 0 || fv.move.x != 0 || fv.move.y != 0 || fv.move.z != 0);
    auto finishRoot = [&](const RootDelta& now, bool active) {
        root_ = fadeW < 1.f ? LerpDelta(frozen, now, fadeW) : now;
        rootActive_ = active || frozenMoves;
        lastVelocity_ = dt > 0 ? RootDelta{{root_.move.x / dt, root_.move.y / dt, root_.move.z / dt}, root_.turn / dt} : RootDelta{};
    };
    const space3d::Clip* c = Current();
    if (!c) {
        last_ = {time_, 0};
        lastFresh_ = false;
        finishRoot({}, false);
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
    // 根が動いた分 (混ぜているときは、混ぜるクリップごとに同じ割合の時刻で出して、同じ重みで混ぜる)。
    RootDelta now;
    bool active = false, mixed = false;
    if (!blend_.empty() && c->duration > 0) {
        const float prevPhase = prevTime_ / c->duration, phase = time_ / c->duration;
        std::vector<RootDelta> deltas;
        std::vector<float> weights;
        for (const Motion& m : blend_) {
            if (!(m.weight > 0) || m.clip < 0 || static_cast<size_t>(m.clip) >= clips_.size()) continue;
            const float d = clips_[static_cast<size_t>(m.clip)].duration;
            deltas.push_back(ClipRoot(m.clip, prevPhase * d, {phase * d, last_.loops}));
            weights.push_back(m.weight);
            active = active || RootBoneOf(m.clip) >= 0;
        }
        if (!weights.empty()) {
            now = MixRootDelta(deltas, weights);
            mixed = true;
        }
    }
    if (!mixed) {
        now = ClipRoot(clip_, prevTime_, last_);
        active = RootBoneOf(clip_) >= 0;
    }
    finishRoot(now, active);
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
    root_ = {};  // (時刻を置いても動かない)
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
        sampleMorph_.assign(morphDefaults_.size(), 0.f);
        for (const Motion& m : blend_) {
            if (!(m.weight > 0) || m.clip < 0 || static_cast<size_t>(m.clip) >= clips_.size()) continue;
            const space3d::Clip& mc = clips_[static_cast<size_t>(m.clip)];
            SampleClip(mc, bindings_[static_cast<size_t>(m.clip)], skeleton, phase * mc.duration, loop, part_);
            Strip(m.clip, part_);
            mix_.Add(part_, m.weight);
            MorphsAt(m.clip, phase * mc.duration, partMorph_);
            for (size_t k = 0; k < sampleMorph_.size(); ++k) sampleMorph_[k] += partMorph_[k] * m.weight;
        }
        if (mix_.TotalWeight() > 0) {
            mix_.Finish(skeleton, sample_);
            for (float& w : sampleMorph_) w /= mix_.TotalWeight();
            mixed = true;
        }
    }
    if (!mixed) {
        if (c) {
            SampleClip(*c, bindings_[static_cast<size_t>(clip_)], skeleton, time_, loop, sample_);
            Strip(clip_, sample_);
        } else {
            sample_ = RestPose(skeleton);
        }
        MorphsAt(c ? clip_ : -1, time_, sampleMorph_);
    }
    if (FadeWeight() < 1.f) {
        if (from_.size() != skeleton.bones.size()) from_ = RestPose(skeleton);
        Blend(from_, sample_, FadeWeight(), pose_);
        if (fromMorph_.size() != sampleMorph_.size()) fromMorph_ = morphDefaults_;
        morph_.resize(sampleMorph_.size());
        const float w = FadeWeight();
        for (size_t k = 0; k < morph_.size(); ++k) morph_[k] = fromMorph_[k] + (sampleMorph_[k] - fromMorph_[k]) * w;
    } else {
        std::swap(pose_, sample_);  // sample_ は次のフレームで全部書き直す
        morph_ = sampleMorph_;
    }
    return pose_;
}

void Player::FinishPose(const space3d::SkinData& skin, std::vector<space3d::BoneMatrix>& out) {
    animation3d::ModelMatrices(skin.skeleton, pose_, model_);
    SkinMatrices(model_, skin.inverseBind, out);
}

}  // namespace rm::animation3d
