#pragma once
// パーティクル - effects ready to start from (炎・煙・火花 ...): made of the built-in modules only, so
// they need no picture or file (the old engine's lesson: "○を放つだけで炎が作れない" - a fire must
// be one click). The パーティクル tab makes a new effect from one, or adds its emitters to an effect.
// Their names are Japanese here; the editor shows them through Tr (the plugin's lang/en.json).

#include "rm_engine/particles/runtime/ParticleEffect.h"

#include <span>
#include <string_view>

namespace rm::particles {

struct EffectPreset {
    std::string_view id;
    std::string_view title;  // Japanese
    ParticleEffect (*make)();
};

// In the order offered (the first: NewEffect's soft glow).
std::span<const EffectPreset> EffectPresets();

}  // namespace rm::particles
