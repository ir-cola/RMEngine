#include "rm_engine/game_production/runtime/PlayServices.h"

namespace rm::game_production {

void PlayServices::Start(PlayHost host) {
    Stop();
    host_ = std::make_unique<PlayHost>(std::move(host));
    if (!host_->registry) return;
    for (const auto& c : host_->registry->Contributions<PlayServiceInfo>(kPlayServicePoint)) {
        if (!c.item->create) continue;
        if (auto s = c.item->create(*host_)) list_.emplace_back(c.item->name ? c.item->name : "", std::move(s));
    }
}

void PlayServices::Frame(const PlayFrame& f) {
    PlayFrame mine = f;
    mine.services = this;
    for (auto& [name, s] : list_) s->Frame(mine);
}

SoundPlayer* PlayServices::Sounds() const {
    for (const auto& [name, s] : list_) {
        if (auto* p = dynamic_cast<SoundPlayer*>(s.get())) return p;
    }
    return nullptr;
}

void PlayServices::SetPaused(bool paused) {
    for (auto& [name, s] : list_) s->SetPaused(paused);
}

void PlayServices::SceneSwitched() {
    for (auto& [name, s] : list_) s->SceneSwitched();
}

void PlayServices::Stop() {
    while (!list_.empty()) list_.pop_back();
    host_.reset();
}

PlayService* PlayServices::Find(std::string_view name) const {
    for (const auto& [n, s] : list_) {
        if (n == name) return s.get();
    }
    return nullptr;
}

bool EntityPose(const RuntimeRegistry& registry, const Scene& scene, int entity, Pose& out) {
    for (const auto& c : registry.Contributions<PlaceInfo>(kPlacePoint)) {
        if (c.item->pose && c.item->pose(scene, entity, out)) return true;
    }
    return false;
}

}  // namespace rm::game_production
