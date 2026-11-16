#pragma once
// パーティクル - the modules the engine has (written as .rmfx, the same as a user's: ModuleSource.h).
// An effect names one by its id (ParticleModule::builtin). Their words (module titles, param labels) are
// Japanese here; the editor shows them through Tr (the plugin's lang/en.json).

#include "rm_engine/particles/runtime/ModuleSource.h"

#include <span>
#include <string_view>

namespace rm::particles {

struct BuiltinModule {
    std::string_view id;    // saved in the effect: never changed once shipped
    std::string_view text;  // the .rmfx
};

std::span<const BuiltinModule> BuiltinModules();
// The parsed module of this id (parsed once); null: no such module.
const ModuleSource* FindBuiltin(std::string_view id);

}  // namespace rm::particles
