// RMAnim.h - スクリプトから骨のアニメーションを動かす (プラグイン ゲーム制作 × アニメーション が提供する)。
//
//   #include "RMAnim.h"
//   rm::Animator anim(self);                 // このエンティティの部品「アニメーター」
//   anim.PlayClip("Run", 0.2f);              // クリップ Run を始めから。0.2 秒かけて今の姿勢から移る (クロスフェード)
//   anim.SetSpeed(1.5f);  anim.Pause();  anim.Resume();
//   std::string c = anim.Clip();  float t = anim.Time();  bool done = anim.Ended();
//   rm::Vec3 hand = anim.BonePosition("RightHand");   // 骨のワールドの位置 (手に持たせる、エフェクトを出す)
//   rm::Quat head = anim.BoneRotation("Head");
//
// クリップのイベント (モデルのインスペクターの「クリップ」の帯で置く) は、そのエンティティのスクリプトの
// OnEvent に届く (送り手はエンジン):
//   void OnEvent(rm::Entity self, const rm::Event& e) { if (e.Is("Footstep")) { ... e.number / e.text ... } }
//
// ゲームが動いている間だけ (止まっているエディターのビューは、いつも通りに見せる)。PlayClip・SetSpeed・SetLoop は
// 部品の欄を書き換える (Play 中のインスペクターにも出る)。
#pragma once

#include "RM.h"
#include "RMAnimAbi.h"

#include <string>
#include <string_view>

namespace rm {

class Animator {
public:
    // エンジンの部品 (Entity::Has<rm::Animator>() など)。
    static constexpr const char* kComponentType = "rm::game_x_animation::Animator";
    explicit Animator(Entity entity) : entity_(entity), api_(Part<RmAnimApi>(RM_PART_ANIM, RM_PART_ANIM_VERSION)) {}

    // エンティティがアニメーターを持っている (かつ、このエンジンにそのプラグインがある)。
    bool Valid() const { return api_ && api_->has(Api()->ctx, entity_.id) != 0; }

    // クリップを始めから流す (`fade` 秒のクロスフェード。0: すぐ)。モデルにそのクリップが無いと false。
    bool PlayClip(std::string_view clip, float fade = 0.0f) const {
        const std::string c(clip);
        return api_ && api_->playClip(Api()->ctx, entity_.id, c.c_str(), fade) != 0;
    }
    bool HasClip(std::string_view clip) const {
        const std::string c(clip);
        return api_ && api_->hasClip(Api()->ctx, entity_.id, c.c_str()) != 0;
    }
    // 今のクリップの名前 (部品の欄。空: モデルの最初のクリップ)。
    std::string Clip() const {
        if (!api_) return {};
        char buf[128];
        const uint32_t n = api_->clip(Api()->ctx, entity_.id, buf, sizeof(buf));
        if (n < sizeof(buf)) return std::string(buf, n);
        std::string s(n + 1, '\0');
        api_->clip(Api()->ctx, entity_.id, s.data(), n + 1);
        s.resize(n);
        return s;
    }

    void Pause(bool paused = true) const {
        if (api_) api_->setPaused(Api()->ctx, entity_.id, paused ? 1 : 0);
    }
    void Resume() const { Pause(false); }
    bool Paused() const { return api_ && api_->paused(Api()->ctx, entity_.id) != 0; }
    // 速さ (倍率。負の数で逆に流れる)。
    float Speed() const { return api_ ? api_->speed(Api()->ctx, entity_.id) : 0.0f; }
    void SetSpeed(float speed) const {
        if (api_) api_->setSpeed(Api()->ctx, entity_.id, speed);
    }
    bool Loop() const { return api_ && api_->loop(Api()->ctx, entity_.id) != 0; }
    void SetLoop(bool loop) const {
        if (api_) api_->setLoop(Api()->ctx, entity_.id, loop ? 1 : 0);
    }
    // 今のクリップの中の時刻 (秒) と、その長さ。
    float Time() const { return api_ ? api_->time(Api()->ctx, entity_.id) : 0.0f; }
    void SetTime(float seconds) const {
        if (api_) api_->setTime(Api()->ctx, entity_.id, seconds);
    }
    float Duration() const { return api_ ? api_->duration(Api()->ctx, entity_.id) : 0.0f; }
    // ループでないクリップが端まで流れた (攻撃が終わったら待機に戻す)。
    bool Ended() const { return api_ && api_->ended(Api()->ctx, entity_.id) != 0; }

    // 骨のワールドの位置と回転。その骨が無い (まだ動いていない) ときはエンティティのワールドの位置と回転。
    Vec3 BonePosition(std::string_view bone) const {
        float p[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1};
        Bone(bone, p, r);
        return {p[0], p[1], p[2]};
    }
    Quat BoneRotation(std::string_view bone) const {
        float p[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1};
        Bone(bone, p, r);
        return {r[0], r[1], r[2], r[3]};
    }
    bool HasBone(std::string_view bone) const {
        float p[3], r[4];
        return Bone(bone, p, r);
    }

    // --- 状態とルール (部品の「状態とルール」に .rmanim を入れたとき) ---
    // パラメーター (ルールの条件で使う値)。スクリプトはこれを入れるだけで、どのクリップを流すかはルールが決める。
    void SetFloat(std::string_view name, float value) const { SetParam(name, value, false); }
    void SetInt(std::string_view name, int value) const { SetParam(name, static_cast<float>(value), false); }
    void SetBool(std::string_view name, bool value) const { SetParam(name, value ? 1.0f : 0.0f, false); }
    // トリガー: それを使ったルールが当たると (か 0.2 秒で) 消える (跳ぶ・攻撃)。
    void SetTrigger(std::string_view name) const { SetParam(name, 1.0f, true); }
    float GetFloat(std::string_view name) const {
        const std::string n(name);
        return rules_ ? rules_->param(Api()->ctx, entity_.id, n.c_str()) : 0.0f;
    }
    bool GetBool(std::string_view name) const { return GetFloat(name) != 0.0f; }
    // 今の状態の名前と、その中の割合 (0..1)。
    std::string State() const {
        if (!rules_) return {};
        char buf[128];
        const uint32_t n = rules_->state(Api()->ctx, entity_.id, buf, sizeof(buf));
        if (n < sizeof(buf)) return std::string(buf, n);
        std::string s(n + 1, '\0');
        rules_->state(Api()->ctx, entity_.id, s.data(), n + 1);
        s.resize(n);
        return s;
    }
    float StateTime() const { return rules_ ? rules_->stateTime(Api()->ctx, entity_.id) : 0.0f; }
    // ルールを飛ばして状態へ (`fade` 秒で)。次のフレームからルールがまた働く。無い名前: false。
    bool Play(std::string_view state, float fade = 0.0f) const {
        const std::string s(state);
        return rules_ && rules_->playState(Api()->ctx, entity_.id, s.c_str(), fade) != 0;
    }

private:
    bool Bone(std::string_view bone, float* p, float* r) const {
        const std::string b(bone);
        return api_ && api_->bone(Api()->ctx, entity_.id, b.c_str(), p, r) != 0;
    }
    void SetParam(std::string_view name, float value, bool trigger) const {
        const std::string n(name);
        if (rules_) rules_->setParam(Api()->ctx, entity_.id, n.c_str(), value, trigger ? 1 : 0);
    }

    Entity entity_;
    const RmAnimApi* api_;
    const RmAnimRulesApi* rules_ = Part<RmAnimRulesApi>(RM_PART_ANIM_RULES, RM_PART_ANIM_RULES_VERSION);
};

}  // namespace rm
