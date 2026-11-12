#include "rm_engine/game_production/runtime/SceneRecord.h"

#include <algorithm>

namespace rm::game_production {

SceneRecord SceneRecord::Begin(const Scene& scene, std::span<const int> touched) {
    SceneRecord r;
    r.nextAtBegin_ = scene.NextId();
    if (!touched.empty()) {
        const std::unordered_set<int> all = scene.Subtree(touched);
        r.ids_.assign(all.begin(), all.end());
        std::sort(r.ids_.begin(), r.ids_.end());  // (the same record for the same change)
    }
    r.before_ = scene.CaptureStates(r.ids_);
    return r;
}

void SceneRecord::End(const Scene& scene) {
    // The entities made since Begin: not there before.
    for (int id = nextAtBegin_; id < scene.NextId(); ++id) {
        if (!scene.Find(id) || std::binary_search(ids_.begin(), ids_.end(), id)) continue;
        ids_.push_back(id);
        Scene::EntityState absent;
        absent.id = id;
        before_.push_back(std::move(absent));
    }
    after_ = scene.CaptureStates(ids_);
    bytes_ = 0;
    for (const auto* states : {&before_, &after_}) {
        for (const Scene::EntityState& s : *states) bytes_ += sizeof(s) + (s.present ? json::Write(s.data, false).size() : 0);
    }
}

}  // namespace rm::game_production
