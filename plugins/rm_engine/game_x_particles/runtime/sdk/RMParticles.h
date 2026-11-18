// RMParticles.h - the entity's パーティクル from a script (the plugin ゲーム制作 × パーティクル gives it).
//
//   #include "RMParticles.h"
//   rm::Particles fx(self);
//   fx.Stop();                 // no new particles (what is out dies away); Stop(true): gone at once
//   fx.Play();                 // letting out again; Play(true): from the start
//   fx.Emit(30);               // 30 particles at once (an explosion: 始めから出す off, then Emit)
//   fx.Pause();  fx.SetSpeed(2);
//
// An entity with several パーティクル: rm::Particles(self, 1) is the second; rm::Particles(self) every one.
// While the game plays only (the editor's view while stopped shows them as it always does).
#pragma once

#include "RM.h"
#include "RMParticlesAbi.h"

namespace rm {

class Particles {
public:
    // The engine's component (Entity::Count<rm::Particles>() ...).
    static constexpr const char* kComponentType = "rm::game_x_particles::ParticleSystem";
    explicit Particles(Entity entity, uint32_t nth = RM_PARTICLES_ALL)
        : entity_(entity), nth_(nth), api_(Part<RmParticlesApi>(RM_PART_PARTICLES, RM_PART_PARTICLES_VERSION)) {}

    // The entity has パーティクル (and this engine has the plugin).
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
    // Letting out particles (any of them, for every one).
    bool Playing() const { return api_ && api_->playing(Api()->ctx, entity_.id, nth_) != 0; }
    // 速さ (the time's multiplier; the first one's, for every one).
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
