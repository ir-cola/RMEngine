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
// The particles an emitter lets out in the next `dt` seconds when a script may have stopped it (emitting
// false: none of its own - the clock stays) and asked for `extra` at once (rm::Particles::Emit): at most
// its capacity.
uint32_t EmitterBirths(EmitterClock& c, const ParticleEmitter& e, float dt, bool emitting, uint32_t extra);

// Finds a module's source: a built-in one, or the .rmfx a module names (null: none; `file` its name for
// the compiler's messages). A file read again (saved meanwhile) is a new source: the player that sees it
// makes its shaders again.
using ModuleLookup = std::function<std::shared_ptr<const ModuleSource>(const ParticleModule& m, std::string& file)>;
// Built-in modules only (a module of a file is left out).
std::shared_ptr<const ModuleSource> BuiltinLookup(const ParticleModule& m, std::string& file);

// What particles collide with this frame (CollideShapes / CollideDepth); null: nothing.
struct CollisionWorld {
    std::span<const CollisionShape> shapes;  // the colliders' shapes in the world
    // The picture's depth from its last frame (the renderer's ExtraDrawContext::depth) and the camera it
    // was drawn with; null: no depth to collide with.
    ID3D11ShaderResourceView* depth = nullptr;
    uint32_t samples = 1;
    float viewProj[16]{};
    float inverse[16]{};  // clip -> world
    float eye[3]{};
    uint32_t width = 0, height = 0;
};

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
    // Letting out new particles (a script's Stop: false - what is out lives on and dies away), and `count`
    // more of each emitter at the next Advance (a script's Emit).
    void SetEmitting(bool on);
    bool Emitting() const { return emitting_; }
    void Emit(uint32_t count) { pending_ += count; }
    // Moves it on by dt (lets out new particles where `world` puts the emitter - a column-major 4x4).
    void Advance(ID3D11Device* device, ID3D11DeviceContext* ctx, const float world[16], float dt, const CollisionWorld* collide = nullptr);
    // Draws its particles: the caller has bound the picture's render target and depth buffer. `images`:
    // the picture an emitter's 絵 names on the GPU (null: not there - the dot's shape is drawn instead).
    using ImageLookup = std::function<ID3D11ShaderResourceView*(const AssetRef&)>;
    void Draw(ID3D11DeviceContext* ctx, const ViewConstants& view, const ImageLookup& images = {});

    // Why an emitter cannot run (its shaders did not compile: the compiler's words); empty: it runs.
    std::string Error(size_t emitter) const;
    // Every emitter has finished (not looping, and its particles have had time to die).
    bool Finished() const;
    // Its emitters that run, the particles they hold at most, and how many are alive (as last read back
    // from the GPU while CountingAlive; aliveKnown false: not read yet).
    struct Census {
        uint32_t emitters = 0;
        uint64_t capacity = 0;
        uint64_t alive = 0;
        bool aliveKnown = true;
    };
    Census Count() const;
    // What went wrong since the last call (a module that cannot be read, a shader that does not compile:
    // Japanese, for the console) - each told once.
    std::vector<std::string> TakeProblems();

private:
    struct Emitter;
    static bool MakeCounter(ID3D11Device* device, Emitter& e);
    static void ReadBackCount(ID3D11DeviceContext* ctx, Emitter& e);
    std::shared_ptr<const ParticleEffect> effect_;
    std::vector<std::unique_ptr<Emitter>> emitters_;
    std::vector<std::string> problems_;
    float time_ = 0;
    bool emitting_ = true;
    uint32_t pending_ = 0;
    // What the emitters collide with (shared by them): the shapes and the camera of the depth.
    Microsoft::WRL::ComPtr<ID3D11Buffer> shapes_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shapesView_;
    size_t shapesCapacity_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> collideCb_;
};

// Counting the particles alive (for the debug panel): each player counts them on the GPU for the next
// `seconds` and reads the number back a few frames later. Not asked: nothing is counted (no cost).
void CountAliveFor(double seconds);
bool CountingAlive();

// Direct3D can run the particles (feature level 11_0 or later).
bool CanRunParticles(ID3D11Device* device);

}  // namespace rm::particles
