#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"

#include "base/Log.h"
#include "rm_engine/animation3d/runtime/Player.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"

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
        if (!a || !model.skin || !model.clips) return {};  // アニメーターが無い: 休みの姿勢 (ゲーム制作 × 3D のまま)
        Body& b = bodies_[entity];
        b.used = true;
        if (b.skin != model.skin || b.clips != model.clips) {
            // 新しいモデル (初めて・読み直した): 骨の木とクリップをつなぎ直す。
            b.skin = model.skin;
            b.clips = model.clips;
            b.player.Bind(b.skin->skeleton, *b.clips);
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
        if (b.stamp.Update(b.skin.get(), clip, b.player.Time(), a->loop, &space3d::NextPoseVersion)) {
            b.player.Evaluate(*b.skin, b.bones);
            if (show == Showing::Playing && !b.toldPlaying && b.player.Time() != StartTime(speed, (*b.clips)[static_cast<size_t>(clip)].duration, a->loop)) {
                // (書き出したゲームの検査 tools/check_export.ps1 の game_anim がこの行を探す)
                log::Info("アニメーター: 「{}」がクリップ「{}」で骨 {} 本を動かしました ({:.2f} 秒)", Name(entity), (*b.clips)[static_cast<size_t>(clip)].name,
                          b.bones.size(), b.player.Time());
                b.toldPlaying = true;
            }
        }
        return {b.bones, b.stamp.Version(), moving};
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
        bool toldPlaying = false;  // 動かしたことを 1 度知らせた
        bool used = true;          // このフレームに尋ねられた
    };

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
    std::unordered_map<int, Body> bodies_;
};

}  // namespace

std::unique_ptr<game_x_3d::MeshPoser> CreateAnimatorPoser() { return std::make_unique<AnimatorPoser>(); }

}  // namespace rm::game_x_animation
