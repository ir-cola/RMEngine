#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"

#include "base/Log.h"
#include "rm_engine/animation3d/runtime/MorphPreview.h"
#include "rm_engine/animation3d/runtime/Player.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"

#include <algorithm>
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::game_x_animation {

PreviewClock& EditorPreview() {
    static PreviewClock clock;
    return clock;
}

namespace {

class AnimatorPoser final : public game_x_3d::MeshPoser {
public:
    void Begin(const game_x_3d::MeshPoseFrame& f) override {
        if (type_.empty()) type_ = reflect::TypeOf<Animator>().name;
        scene_ = &f.scene;
        selection_.assign(f.selection.begin(), f.selection.end());
        dt_ = std::isfinite(f.dt) && f.dt > 0 ? f.dt : 0.f;
        playing_ = f.playing;
        ownsClock_ = !f.playing && f.picture && std::strcmp(f.picture, "scene") == 0;
        animating_ = false;
        // 前のフレームに使わなかった物は忘れる (消えた・アニメーターを外した・モデルを替えた)。
        std::erase_if(bodies_, [](const auto& kv) { return !kv.second.used; });
        for (auto& [id, b] : bodies_) b.used = false;
        // プレビューの時計の持ち主: 選んだもののうち、アニメーターを持つ最後のもの (シーンビューの絵だけが決める)。
        previewed_ = 0;
        if (!f.playing) {
            for (auto it = f.selection.rbegin(); it != f.selection.rend(); ++it) {
                if (AnimatorOf(*it)) {
                    previewed_ = *it;
                    break;
                }
            }
            if (ownsClock_) {
                PreviewClock& clock = EditorPreview();
                FollowSelection(clock, previewed_);
                // (クリップは Pose が知らせ直す: 骨のあるモデルでなくなったら「クリップが無い」)
                clock.hasClip = false;
                clock.duration = 0;
                clock.clip.clear();
            }
            previewed_ = EditorPreview().entity == previewed_ ? previewed_ : 0;
        }
    }

    game_x_3d::MeshPoseAnswer Pose(int entity, const game_x_3d::SkinnedModel& model) override {
        const Animator* a = AnimatorOf(entity);
        if (!a && model.skin && !playing_) {
            // アニメーターが無い物: 止まっていて選んだ物だけ、インスペクターのつまみで試す形の重みを見せる (休みの姿勢で)。
            Body& b = bodies_[entity];
            b.used = true;
            b.morphs = model.skin->morphDefaults;
            if (!TryOn(entity, *model.skin, b.morphs)) return {};
            animating_ = true;
            b.rest.assign(model.skin->skeleton.bones.size(), space3d::BoneMatrix{});
            return {b.rest, b.morphs, 0, true};
        }
        if (!a || !model.skin || !model.clips) return {};  // アニメーターが無い: 休みの姿勢 (ゲーム制作 × 3D のまま)
        if (playing_ && a->run && a->run->version != 0 && a->run->skin && a->run->owner == entity &&
            a->run->skin->skeleton.bones.size() == model.skin->skeleton.bones.size()) {
            // ゲームが動いている: システム「アニメーション」が決めた姿勢 (このフレームにもう 1 度は計算しない)。
            const AnimatorRun& run = *a->run;
            animating_ = animating_ || run.moving || (!run.paused && run.player.ClipIndex() >= 0);
            return {run.bones, run.morphs, run.version, run.moving};
        }
        Body& b = bodies_[entity];
        b.used = true;
        if (b.skin != model.skin || b.clips != model.clips) {
            // 新しいモデル (初めて・読み直した): 骨の木とクリップをつなぎ直す。
            b.skin = model.skin;
            b.clips = model.clips;
            b.player.Bind(b.skin->skeleton, *b.clips);
            b.player.BindMorphs(b.skin->morphs, b.skin->morphDefaults);
            b.clip = -2;
            b.stamp.Reset();
            b.told.clear();
        }
        const int clip = FindClip(*b.clips, a->clip);
        if (clip < 0 && !a->clip.empty() && b.told != a->clip) {
            log::Warn("アニメーター ({}): モデルにクリップ「{}」がないので、バインドの形で描きます", Name(entity), a->clip);
            b.told = a->clip;
        }
        const bool previewed = !playing_ && entity == previewed_ && previewed_ != 0;
        const Showing show = WhatToShow(playing_, a->playOnStart, previewed, clip >= 0);
        if (previewed && ownsClock_) {
            // (時計に今のクリップを知らせる: シーンビューの上の操作が見せる)
            PreviewClock& clock = EditorPreview();
            clock.hasClip = clip >= 0;
            clock.clip = clip >= 0 ? (*b.clips)[static_cast<size_t>(clip)].name : a->clip;
            clock.duration = clip >= 0 ? (*b.clips)[static_cast<size_t>(clip)].duration : 0.f;
        }
        if (show == Showing::BindPose) {
            b.clip = -2;
            return {};
        }
        const float speed = std::isfinite(a->speed) ? a->speed : 1.f;
        b.player.loop = a->loop;
        b.player.speed = speed;
        const bool restart = b.clip != clip || b.showing != show;
        if (restart) b.player.Play(clip);
        bool moving = false;
        switch (show) {
            case Showing::FirstFrame: break;  // (Play が始めの時刻に置いた)
            case Showing::Playing:
                if (!restart) {
                    const float before = b.player.Time();
                    b.player.Tick(dt_);
                    moving = b.player.Time() != before;
                }
                animating_ = animating_ || moving || restart || (a->loop && speed != 0);
                break;
            case Showing::Preview: {
                PreviewClock& clock = EditorPreview();
                const float duration = (*b.clips)[static_cast<size_t>(clip)].duration;
                if (ownsClock_) AdvancePreview(clock, dt_, speed, duration, a->loop);
                b.player.SetTime(clock.time);
                moving = !clock.paused;
                animating_ = animating_ || !clock.paused;
                break;
            }
            case Showing::BindPose: break;
        }
        b.clip = clip;
        b.showing = show;
        if (b.stamp.Update(b.skin.get(), clip, b.player.Time(), a->loop, &space3d::NextPoseVersion)) b.player.Evaluate(*b.skin, b.bones);
        // 形の重み: クリップのもの。止まっていて選んだ物は、モデルのインスペクターのつまみで試す重みを上書き。
        b.morphs = b.player.MorphWeights();
        if (TryOn(entity, *b.skin, b.morphs)) moving = animating_ = true;  // (つまみを動かすと描き直す)
        return {b.bones, b.morphs, b.stamp.Version(), moving};
    }

    bool Animating() const override { return animating_; }

private:
    struct Body {
        std::shared_ptr<const space3d::SkinData> skin;
        std::shared_ptr<const std::vector<space3d::Clip>> clips;  // Player が指すもの (持っておく)
        animation3d::Player player;
        int clip = -2;  // 今流しているクリップ (-2: まだ)
        Showing showing = Showing::BindPose;
        PoseStamp stamp;
        std::vector<space3d::BoneMatrix> bones;
        std::string told;          // 見つからないと知らせたクリップの名前
        bool used = true;          // このフレームに尋ねられた
        std::vector<float> morphs;               // 形の重みの答え
        std::vector<space3d::BoneMatrix> rest;   // (アニメーターの無い物: 休みの姿勢のスキンの行列 = 単位)
    };

    // 止まっていて選んだエンティティなら、そのモデルの試している形の重みを `morphs` に上書きする。上書きした: true。
    bool TryOn(int entity, const space3d::SkinData& skin, std::vector<float>& morphs) const {
        if (playing_ || std::find(selection_.begin(), selection_.end(), entity) == selection_.end()) return false;
        const game_production::SceneEntity* e = scene_ ? scene_->Find(entity) : nullptr;
        const game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<game_x_3d::MeshRenderer>().name) : nullptr;
        if (!c || !c->Known()) return false;
        const auto& mr = *static_cast<const game_x_3d::MeshRenderer*>(c->value.Get());
        const animation3d::MorphTry* tried = animation3d::TriedMorphs(mr.model.guid);
        if (!tried) return false;
        morphs.resize(skin.morphs.size(), 0.f);
        bool any = false;
        for (const auto& [name, weight] : *tried) {
            for (size_t k = 0; k < skin.morphs.size(); ++k) {
                if (skin.morphs[k] == name) morphs[k] = weight, any = true;
            }
        }
        return any;
    }

    const Animator* AnimatorOf(int entity) const {
        const game_production::SceneEntity* e = scene_ ? scene_->Find(entity) : nullptr;
        const game_production::Component* c = e ? e->FindComponent(type_) : nullptr;
        return c && c->Known() ? static_cast<const Animator*>(c->value.Get()) : nullptr;
    }
    std::string Name(int entity) const {
        const game_production::SceneEntity* e = scene_ ? scene_->Find(entity) : nullptr;
        return e ? e->data.name : std::to_string(entity);
    }

    std::string type_;
    const game_production::Scene* scene_ = nullptr;
    float dt_ = 0;
    bool playing_ = false, ownsClock_ = false, animating_ = false;
    int previewed_ = 0;
    std::vector<int> selection_;
    std::unordered_map<int, Body> bodies_;
};

}  // namespace

std::unique_ptr<game_x_3d::MeshPoser> CreateAnimatorPoser() { return std::make_unique<AnimatorPoser>(); }

}  // namespace rm::game_x_animation
