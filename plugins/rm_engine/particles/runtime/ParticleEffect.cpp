#include "rm_engine/particles/runtime/ParticleEffect.h"

#include <format>

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

namespace rm::particles {

std::optional<ParticleEffect> ReadEffect(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns) {
    json::ParseError pe;
    const auto v = json::Parse(bytes, &pe);
    if (!v) {
        if (error) *error = std::format("JSON として読めません ({} 行目: {})", pe.line, pe.message);
        return std::nullopt;
    }
    if (!v->IsObject() || v->Get("kind").AsString() != kEffectKind) {
        if (error) *error = "パーティクルのエフェクトのファイルではありません";
        return std::nullopt;
    }
    EffectFile file;
    reflect::Unknowns u;
    reflect::Load(file, *v, &u);
    if (unknowns) *unknowns = std::move(u);
    return std::move(file.effect);
}

std::string WriteEffect(const ParticleEffect& effect, const reflect::Unknowns* unknowns) {
    EffectFile file;
    file.kind = kEffectKind;
    file.effect = effect;
    return json::Write(reflect::Save(file, unknowns));
}

}  // namespace rm::particles
