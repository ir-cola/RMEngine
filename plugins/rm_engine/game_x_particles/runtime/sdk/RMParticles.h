// RMParticles.h - スクリプトからエンティティのパーティクルを扱う (プラグイン ゲーム制作 × パーティクル が提供する)。
//
//   #include "RMParticles.h"
//   rm::Particles fx(self);
//   fx.Stop();                 // 新しく出さない (出ているものは消えていく); Stop(true): すぐ消す
//   fx.Play();                 // また出す; Play(true): 始めから
//   fx.Emit(30);               // 30 個を一度に (爆発: 始めから出す を切って Emit)
//   fx.Pause();  fx.SetSpeed(2);
//
// パーティクルを複数持つエンティティ: rm::Particles(self, 1) は 2 つ目、rm::Particles(self) はすべて。
// ゲームが動いている間だけ (止まっているエディターのビューは、いつも通りに見せる)。
#pragma once

#include "RM.h"
#include "RMParticlesAbi.h"

namespace rm {

class Particles {
public:
    // エンジンの部品 (Entity::Count<rm::Particles>() など)。
    static constexpr const char* kComponentType = "rm::game_x_particles::ParticleSystem";
    explicit Particles(Entity entity, uint32_t nth = RM_PARTICLES_ALL)
        : entity_(entity), nth_(nth), api_(Part<RmParticlesApi>(RM_PART_PARTICLES, RM_PART_PARTICLES_VERSION)) {}

    // エンティティがパーティクルを持っている (かつ、このエンジンにそのプラグインがある)。
    bool Valid() const { return api_ && api_->count(Api()->ctx, entity_.id) > (nth_ == RM_PARTICLES_ALL ? 0u : nth_); }
    void Play(bool fromStart = false) const {
        if (api_) api_->play(Api()->ctx, entity_.id, nth_, fromStart ? 1 : 0);
    }
    void Stop(bool clear = false) const {
        if (api_) api_->stop(Api()->ctx, entity_.id, nth_, clear ? 1 : 0);
    }
    void Pause(bool paused = true) const {
        if (api_) api_->pause(Api()->ctx, entity_.id, nth_, paused ? 1 : 0);
    }
    void Emit(int count) const {
        if (api_ && count > 0) api_->emit(Api()->ctx, entity_.id, nth_, static_cast<uint32_t>(count));
    }
    // パーティクルを出す (どれか 1 つ、またはすべて)。
    bool Playing() const { return api_ && api_->playing(Api()->ctx, entity_.id, nth_) != 0; }
    // 速さ (時間の倍率; 最初のものの値を返す。設定はすべてに)。
    float Speed() const { return api_ ? api_->speed(Api()->ctx, entity_.id, nth_) : 0.0f; }
    void SetSpeed(float speed) const {
        if (api_) api_->setSpeed(Api()->ctx, entity_.id, nth_, speed);
    }

private:
    Entity entity_;
    uint32_t nth_;
    const RmParticlesApi* api_;
};

}  // namespace rm
