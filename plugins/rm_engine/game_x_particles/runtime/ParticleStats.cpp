#include "rm_engine/game_x_particles/runtime/ParticleStats.h"

#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace rm::game_x_particles {

namespace {
std::mutex g_mutex;
std::unordered_map<const void*, PictureParticles>& Store() {
    static std::unordered_map<const void*, PictureParticles> store;
    return store;
}
}  // namespace

void ReportParticles(const void* drawer, PictureParticles stats) {
    stats.at = NowSeconds();
    std::lock_guard lock(g_mutex);
    Store()[drawer] = std::move(stats);
}

std::vector<PictureParticles> RecentParticles(double maxAge) {
    const double now = NowSeconds();
    std::lock_guard lock(g_mutex);
    std::vector<PictureParticles> out;
    for (auto it = Store().begin(); it != Store().end();) {
        if (now - it->second.at > 60) {
            it = Store().erase(it);  // (a drawer gone long ago)
            continue;
        }
        if (now - it->second.at <= maxAge) out.push_back(it->second);
        ++it;
    }
    return out;
}

}  // namespace rm::game_x_particles
