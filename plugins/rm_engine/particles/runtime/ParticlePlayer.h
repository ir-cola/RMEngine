#pragma once
// パーティクル - one effect playing on the GPU (Direct3D 11, feature level 11_0: compute shaders).
//
//   EmitterClock   how many particles an emitter lets out this frame (rate, burst, duration, loop). Pure:
//                  tested in tests/TestParticles.cpp
//   EffectPlayer   an effect's emitters on the GPU: each a buffer of particles (a ring: a new particle
//                  takes the next slot - the oldest goes when it is full), a compute shader that starts
//                  the new ones and one that moves all of them, and a draw of squares facing the camera
//                  (ParticleShader.h). The shaders are made once per text and device and shared by every
//                  player (a hundred torches of one effect compile once).
//
// The player is made again when the GPU's devices are (platform::Gfx::Generation: its owner's job).

#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rm::particles {

struct EmitterClock {
    float age = 0;          // seconds into this round
    float owed = 0;         // part of a particle still to let out (rate x time)
    bool burstDone = false;
    bool finished = false;  // not looping and its duration is over
};
// The particles to let out in the next `dt` seconds (at most the emitter's capacity).
uint32_t AdvanceClock(EmitterClock& c, const ParticleEmitter& e, float dt);

// Finds a module's source: a built-in one, or the .rmfx a module names (null: none; `file` its name for
// the compiler's messages). A file read again (saved meanwhile) is a new source: the player that sees it
// makes its shaders again.
using ModuleLookup = std::function<std::shared_ptr<const ModuleSource>(const ParticleModule& m, std::string& file)>;
// Built-in modules only (a module of a file is left out).
std::shared_ptr<const ModuleSource> BuiltinLookup(const ParticleModule& m, std::string& file);

class EffectPlayer {
public:
    EffectPlayer();
    ~EffectPlayer();
    EffectPlayer(const EffectPlayer&) = delete;
    EffectPlayer& operator=(const EffectPlayer&) = delete;

    // The effect to play (kept as given until Set again: the owner keeps it alive). Asked every frame: the
    // modules are looked up again, and made again only when the effect or a module's source changed.
    // Emitters whose shaders stay the same keep their particles; values only change the constant buffers.
    void Set(std::shared_ptr<const ParticleEffect> effect, const ModuleLookup& lookup);
    const std::shared_ptr<const ParticleEffect>& Effect() const { return effect_; }

    // From the start again (no particles, the clocks at 0).
    void Restart();
    // Moves it on by dt (lets out new particles where `world` puts the emitter - a column-major 4x4).
    void Advance(ID3D11Device* device, ID3D11DeviceContext* ctx, const float world[16], float dt);
    // Draws its particles: the caller has bound the picture's render target and depth buffer.
    void Draw(ID3D11DeviceContext* ctx, const ViewConstants& view);

    // Why an emitter cannot run (its shaders did not compile: the compiler's words); empty: it runs.
    std::string Error(size_t emitter) const;
    // Every emitter has finished (not looping, and its particles have had time to die).
    bool Finished() const;
    // What went wrong since the last call (a module that cannot be read, a shader that does not compile:
    // Japanese, for the console) - each told once.
    std::vector<std::string> TakeProblems();

private:
    struct Emitter;
    std::shared_ptr<const ParticleEffect> effect_;
    std::vector<std::unique_ptr<Emitter>> emitters_;
    std::vector<std::string> problems_;
    float time_ = 0;
};

// Direct3D can run the particles (feature level 11_0 or later).
bool CanRunParticles(ID3D11Device* device);

}  // namespace rm::particles
