#include "rm_engine/particles/runtime/ParticleEffect.h"

namespace rm::particles {

ParticleEffect NewEffect() {
    ParticleEmitter e;
    e.name = "光";
    e.rate = 40;
    auto module = [](const char* id) {
        ParticleModule m;
        m.builtin = id;
        return m;
    };
    e.modules = {module("shape_cone"), module("initial"), module("force"), module("color_over_life"), module("size_over_life")};
    // A slow rise (the force's default pulls down): a little upward instead.
    e.modules[2].values.push_back({"acceleration", 0, 0.5f, 0, 0});
    ParticleEffect effect;
    effect.emitters.push_back(std::move(e));
    return effect;
}

}  // namespace rm::particles
