#pragma once
// ゲーム制作 × パーティクル - the particles in a 3D picture (ゲーム制作 × 3D's WorldDrawer): one
// EffectPlayer per entity with パーティクル, moved on by the picture's time and drawn into it.
//
//   playing (Play, the exported game)   every パーティクル with 始めから出す
//   not playing (the editor)            the selected ones (to be looked at), from the start when selected
//
// An effect is read from its asset once and looked at again now and then (a file saved meanwhile -
// the パーティクル tab, another program - plays as it is now). A problem (an effect that cannot be read, a
// shader that does not compile) is told once in the console.

#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace rm::game_x_particles {

std::unique_ptr<game_x_3d::WorldDrawer> CreateParticleDraw();

// The modules an effect names: the built-in ones, and the files (.rmfx) read through `read` - looked at
// again now and then (a file saved meanwhile is a new source: the players make their shaders again).
class ModuleFiles {
public:
    using Read = std::function<std::optional<std::string>(const AssetRef&)>;
    using Name = std::function<std::string(const AssetRef&)>;
    // `often`: looked at again every quarter second (the editor), else every second and a half.
    std::shared_ptr<const particles::ModuleSource> Find(const particles::ParticleModule& m, std::string& file, const Read& read, const Name& name,
                                                        bool often);

private:
    struct Entry {
        std::shared_ptr<const particles::ModuleSource> source;
        std::string bytes;
        std::string name;
        double lookedAt = -1e9;
    };
    std::unordered_map<std::string, Entry> files_;  // by asset id
};

// How often an asset is read again (seconds): the editor while stopped, else.
inline constexpr double kLookOften = 0.25, kLookSeldom = 1.5;
double NowSeconds();

// The camera of a 3D picture as the particles' shaders take it.
particles::ViewConstants ViewOf(const space3d::ExtraDrawContext& c);

}  // namespace rm::game_x_particles
