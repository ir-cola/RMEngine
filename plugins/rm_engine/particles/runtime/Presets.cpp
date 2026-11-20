#include "rm_engine/particles/runtime/Presets.h"

#include <initializer_list>
#include <utility>

namespace rm::particles {

namespace {

// 組み込みモジュールと、その値のいくつか (残りはモジュールの初期値)。
ParticleModule M(const char* id, std::initializer_list<ParticleValue> values = {}) {
    ParticleModule m;
    m.builtin = id;
    m.values = values;
    return m;
}
ParticleValue V(const char* name, float x, float y = 0, float z = 0, float w = 0) { return {name, x, y, z, w}; }

ParticleEffect One(ParticleEmitter e) {
    ParticleEffect effect;
    effect.emitters.push_back(std::move(e));
    return effect;
}

ParticleEmitter Emitter(const char* name, float rate, std::vector<ParticleModule> modules) {
    ParticleEmitter e;
    e.name = name;
    e.rate = rate;
    e.modules = std::move(modules);
    return e;
}

ParticleEmitter FireEmitter() {
    ParticleEmitter e = Emitter("炎", 70,
                                {M("shape_cone", {V("angle", 12), V("radius", 0.25f)}),
                                 M("initial", {V("life", 0.6f, 1.0f), V("speed", 1.0f, 2.0f), V("scale", 0.45f, 0.75f), V("tint", 1, 0.55f, 0.15f, 1), V("angle", 0, 360)}),
                                 M("force", {V("acceleration", 0, 1.5f, 0)}), M("noise", {V("strength", 1.5f), V("frequency", 2.0f), V("flow", 1.5f)}),
                                 M("color_over_life", {V("first", 1, 1, 0.8f, 1), V("last", 1, 0.15f, 0.02f, 0)}), M("size_over_life", {V("first", 1), V("last", 0.25f)})});
    e.capacity = 400;
    return e;
}

ParticleEmitter SmokeEmitter(float rate) {
    ParticleEmitter e = Emitter("煙", rate,
                                {M("shape_cone", {V("angle", 15), V("radius", 0.3f)}),
                                 M("initial", {V("life", 2.0f, 3.5f), V("speed", 0.6f, 1.1f), V("scale", 0.6f, 1.0f), V("tint", 0.55f, 0.55f, 0.58f, 0.55f),
                                               V("angle", 0, 360), V("turn", -30, 30)}),
                                 M("force", {V("acceleration", 0.2f, 0.4f, 0), V("drag", 0.4f)}), M("noise", {V("strength", 0.6f), V("frequency", 0.8f)}),
                                 M("color_over_life", {V("first", 1, 1, 1, 1), V("last", 1, 1, 1, 0)}), M("size_over_life", {V("first", 0.8f), V("last", 2.6f)})});
    e.blend = ParticleBlend::Alpha;
    e.shape = ParticleShape::Smoke;
    e.capacity = 300;
    return e;
}

ParticleEffect Fire() {
    ParticleEffect e = One(FireEmitter());
    ParticleEmitter smoke = SmokeEmitter(8);
    smoke.modules[0] = M("shape_cone", {V("angle", 10), V("radius", 0.2f)});
    e.emitters.push_back(std::move(smoke));
    return e;
}

ParticleEffect Smoke() { return One(SmokeEmitter(14)); }

ParticleEffect Sparks() {
    ParticleEmitter e = Emitter("火花", 0,
                                {M("shape_cone", {V("angle", 50), V("radius", 0.05f)}),
                                 M("initial", {V("life", 0.5f, 1.0f), V("speed", 4, 8), V("scale", 0.04f, 0.07f), V("tint", 1, 0.85f, 0.4f, 1)}),
                                 M("force", {V("acceleration", 0, -9.8f, 0)}), M("floor", {V("bounce", 0.35f), V("friction", 0.3f)}),
                                 M("color_over_life", {V("first", 1, 1, 1, 1), V("last", 1, 0.3f, 0.05f, 0)})});
    e.burst = 40;
    e.duration = 1.2f;
    e.stretch = 0.03f;
    e.capacity = 600;
    return One(std::move(e));
}

ParticleEffect Explosion() {
    ParticleEmitter flash = Emitter("火の玉", 0,
                                    {M("shape_sphere", {V("radius", 0.3f)}),
                                     M("initial", {V("life", 0.4f, 0.9f), V("speed", 2, 7), V("scale", 0.6f, 1.3f), V("tint", 1, 0.65f, 0.25f, 1), V("angle", 0, 360)}),
                                     M("force", {V("acceleration", 0, 0.5f, 0), V("drag", 3)}),
                                     M("color_over_life", {V("first", 1, 1, 0.8f, 1), V("last", 0.6f, 0.1f, 0.02f, 0)}), M("size_over_life", {V("first", 0.7f), V("last", 1.8f)})});
    flash.burst = 70;
    flash.duration = 2;
    flash.capacity = 200;
    ParticleEmitter smoke = SmokeEmitter(0);
    smoke.name = "煙";
    smoke.burst = 25;
    smoke.duration = 3;
    smoke.modules[0] = M("shape_sphere", {V("radius", 0.6f)});
    smoke.modules[1] = M("initial", {V("life", 1.5f, 3.0f), V("speed", 0.5f, 1.5f), V("scale", 0.8f, 1.4f), V("tint", 0.35f, 0.33f, 0.32f, 0.7f), V("angle", 0, 360)});
    ParticleEmitter sparks = Sparks().emitters[0];
    sparks.burst = 50;
    sparks.duration = 2;
    sparks.modules[0] = M("shape_sphere", {V("radius", 0.2f)});
    ParticleEffect e;
    e.emitters = {std::move(flash), std::move(smoke), std::move(sparks)};
    for (ParticleEmitter& em : e.emitters) em.loop = false;
    return e;
}

ParticleEffect Sparkle() {
    ParticleEmitter e = Emitter("キラキラ", 25,
                                {M("shape_sphere", {V("radius", 1.0f)}),
                                 M("initial", {V("life", 0.8f, 1.6f), V("speed", 0, 0.2f), V("scale", 0.12f, 0.3f), V("tint", 0.85f, 0.95f, 1, 1), V("turn", -90, 90)}),
                                 M("color_over_life", {V("first", 1, 1, 1, 1), V("last", 1, 1, 1, 0)}), M("size_over_life", {V("first", 1), V("last", 0.2f)})});
    e.shape = ParticleShape::Star;
    return One(std::move(e));
}

ParticleEffect Rain() {
    ParticleEmitter e = Emitter("雨", 400,
                                {M("shape_box", {V("extent", 12, 0, 12)}),
                                 M("initial", {V("life", 1.2f, 1.6f), V("speed", -9, -11), V("scale", 0.02f, 0.03f), V("tint", 0.7f, 0.8f, 1, 0.55f), V("angle", 0, 0)}),
                                 M("collide_depth", {V("vanish", 1)})});
    e.blend = ParticleBlend::Alpha;
    e.shape = ParticleShape::Square;
    e.stretch = 0.04f;
    e.capacity = 1500;
    return One(std::move(e));
}

ParticleEffect Snow() {
    ParticleEmitter e = Emitter("雪", 80,
                                {M("shape_box", {V("extent", 12, 0, 12)}),
                                 M("initial", {V("life", 6, 9), V("speed", -0.6f, -1.0f), V("scale", 0.05f, 0.1f), V("tint", 1, 1, 1, 0.9f)}),
                                 M("noise", {V("strength", 0.5f), V("frequency", 0.5f), V("flow", 0.3f)}),
                                 M("collide_depth", {V("bounce", 0), V("friction", 1), V("thickness", 0.3f)}),
                                 M("color_over_life", {V("first", 1, 1, 1, 1), V("last", 1, 1, 1, 0)})});
    e.blend = ParticleBlend::Alpha;
    e.capacity = 1000;
    return One(std::move(e));
}

ParticleEffect Magic() {
    ParticleEmitter e = Emitter("魔法の光", 6,
                                {M("shape_sphere", {V("radius", 0.3f)}),
                                 M("initial", {V("life", 1.2f, 2.0f), V("speed", 0.4f, 1.0f), V("scale", 0.12f, 0.2f), V("tint", 0.45f, 0.7f, 1, 1)}),
                                 M("noise", {V("strength", 3), V("frequency", 1.5f), V("flow", 1)}),
                                 M("color_over_life", {V("first", 1, 1, 1, 1), V("last", 0.6f, 0.3f, 1, 0)})});
    e.trail = true;
    e.trailTime = 0.6f;
    e.trailPoints = 16;
    e.trailWidth = 0.8f;
    return One(std::move(e));
}

constexpr EffectPreset kPresets[] = {
    {"glow", "光", &NewEffect},         {"fire", "炎", &Fire},       {"smoke", "煙", &Smoke},   {"sparks", "火花", &Sparks},
    {"explosion", "爆発", &Explosion}, {"sparkle", "キラキラ", &Sparkle}, {"rain", "雨", &Rain}, {"snow", "雪", &Snow},
    {"magic", "魔法の光 (トレイル)", &Magic},
};

}  // namespace

std::span<const EffectPreset> EffectPresets() { return kPresets; }

}  // namespace rm::particles
