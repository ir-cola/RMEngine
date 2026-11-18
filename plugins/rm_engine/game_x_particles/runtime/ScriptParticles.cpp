// The particles' part of the scripts' API (RMParticles.h / RMParticlesAbi.h): what a script asks is written
// into the パーティクル of the scene that plays (its RM_TRANSIENT fields); the drawer does it (ParticleDraw.cpp).
// RmApi::ctx is game production's ScriptWorld.

#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
#include "rm_engine/game_x_particles/runtime/sdk/RMParticlesAbi.h"

#include <algorithm>
#include <functional>
#include <limits>

namespace rm::game_x_particles {

namespace {

game_production::SceneEntity* EntityOf(void* ctx, uint64_t id) {
    auto& w = *static_cast<game_production::ScriptWorld*>(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return nullptr;
    return w.scene->Find(static_cast<int>(id));
}

// Each パーティクル `nth` names (every one for RM_PARTICLES_ALL).
void Each(void* ctx, uint64_t id, uint32_t nth, const std::function<void(ParticleSystem&)>& fn) {
    game_production::SceneEntity* e = EntityOf(ctx, id);
    if (!e) return;
    const std::string type = reflect::TypeOf<ParticleSystem>().name;
    for (size_t i = 0;; ++i) {
        game_production::Component* c = e->FindComponent(type, i);
        if (!c) break;
        if ((nth == RM_PARTICLES_ALL || i == nth) && c->Known()) fn(*static_cast<ParticleSystem*>(c->value.Get()));
    }
}

uint32_t Count(void* ctx, uint64_t id) {
    const game_production::SceneEntity* e = EntityOf(ctx, id);
    return e ? static_cast<uint32_t>(e->CountComponents(reflect::TypeOf<ParticleSystem>().name)) : 0;
}
void Play(void* ctx, uint64_t id, uint32_t nth, int fromStart) {
    Each(ctx, id, nth, [&](ParticleSystem& ps) {
        ps.scriptPlay = 1;
        ps.paused = false;
        if (fromStart) ++ps.restarts;
    });
}
void Stop(void* ctx, uint64_t id, uint32_t nth, int clear) {
    Each(ctx, id, nth, [&](ParticleSystem& ps) {
        ps.scriptPlay = 2;
        if (clear) ++ps.restarts;
    });
}
void Pause(void* ctx, uint64_t id, uint32_t nth, int paused) {
    Each(ctx, id, nth, [&](ParticleSystem& ps) { ps.paused = paused != 0; });
}
void Emit(void* ctx, uint64_t id, uint32_t nth, uint32_t count) {
    Each(ctx, id, nth, [&](ParticleSystem& ps) { ps.emitted += std::min<uint32_t>(count, 100000); });
}
int Playing(void* ctx, uint64_t id, uint32_t nth) {
    bool any = false;
    Each(ctx, id, nth, [&](ParticleSystem& ps) { any = any || Emitting(ps); });
    return any ? 1 : 0;
}
float Speed(void* ctx, uint64_t id, uint32_t nth) {
    float s = 0;
    bool first = true;
    Each(ctx, id, nth, [&](ParticleSystem& ps) {
        if (first) s = ps.speed;
        first = false;
    });
    return s;
}
void SetSpeed(void* ctx, uint64_t id, uint32_t nth, float speed) {
    Each(ctx, id, nth, [&](ParticleSystem& ps) { ps.speed = std::clamp(speed, 0.f, 10.f); });
}

constexpr RmParticlesApi kTable{&Count, &Play, &Stop, &Pause, &Emit, &Playing, &Speed, &SetSpeed};

}  // namespace

const game_production::ScriptApiPart& ScriptParticlesPart() {
    static const game_production::ScriptApiPart part{RM_PART_PARTICLES, RM_PART_PARTICLES_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_particles
