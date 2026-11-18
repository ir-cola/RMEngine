#pragma once
// How many particles each 3D picture plays (the scene view, the game view, the exported game): what the
// drawers counted at their last frame - read by the editor's debug panel. Kept for the whole program,
// by drawer; an entry not told again for a while is forgotten.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rm::game_x_particles {

struct PictureParticles {
    std::string picture;  // "scene" / "game" / "preview" (WorldDrawFrame::picture; empty: the game's own)
    bool playing = false;
    size_t systems = 0;   // パーティクル components it found
    size_t effects = 0;   // effects playing (each with its player on the GPU)
    uint32_t emitters = 0;
    uint64_t capacity = 0;  // particles they may hold at most
    uint64_t alive = 0;     // alive now (as read back from the GPU)
    bool aliveKnown = false;
    double at = 0;  // NowSeconds() when told
};

// A drawer tells what it played this frame (`drawer`: who it is - one entry each).
void ReportParticles(const void* drawer, PictureParticles stats);
// The drawers told within the last `maxAge` seconds, in no special order.
std::vector<PictureParticles> RecentParticles(double maxAge = 1.0);

}  // namespace rm::game_x_particles
