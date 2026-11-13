#include "rm_engine/game_production/runtime/Systems.h"

#include <algorithm>
#include <limits>

namespace rm::game_production {

void ForEachComponent(Scene& scene, std::string_view type, const std::function<void(SceneEntity&, Component&)>& fn) {
    // TreeOrder gives the entities that run; ids, because a system may change the scene's list.
    std::vector<int> ids;
    for (const TreeEntry& t : scene.TreeOrder()) {
        if (t.activeInHierarchy) ids.push_back(t.entity->data.id);
    }
    for (const int id : ids) {
        SceneEntity* e = scene.Find(id);
        if (!e) continue;
        for (size_t i = 0; i < e->components.size(); ++i) {
            Component& c = e->components[i];
            if (c.type == type && c.Known()) fn(*e, c);
            e = scene.Find(id);  // fn may have added components (the list moved) or removed the entity
            if (!e) break;
        }
    }
}

std::vector<std::string> CameraTypes(const RuntimeRegistry& registry) {
    std::vector<std::string> out;
    for (const auto& c : registry.Contributions<ComponentInfo>(kComponentPoint)) {
        if (!c.item->camera) continue;
        std::string name(c.item->type().name);
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
    }
    return out;
}

uint64_t SceneCamera(const Scene& scene, const std::vector<std::string>& cameraTypes, uint64_t wanted) {
    auto isCamera = [&](const SceneEntity& e) {
        return std::any_of(cameraTypes.begin(), cameraTypes.end(), [&](const std::string& t) {
            const Component* c = e.FindComponent(t);
            return c && c->Known();
        });
    };
    if (wanted > 0 && wanted <= static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        if (const SceneEntity* e = scene.Find(static_cast<int>(wanted)); e && isCamera(*e)) return wanted;
    }
    for (const TreeEntry& t : scene.TreeOrder()) {
        if (t.activeInHierarchy && isCamera(*t.entity)) return static_cast<uint64_t>(t.entity->data.id);
    }
    return 0;
}

std::vector<const SystemInfo*> RegisteredSystems(const RuntimeRegistry& registry) {
    std::vector<const SystemInfo*> out;
    for (const auto& s : registry.Contributions<SystemInfo>(kSystemPoint)) out.push_back(s.item);
    return out;
}

void SystemRunner::SetSystems(std::span<const SystemInfo* const> systems) {
    systems_.clear();
    for (const SystemInfo* s : systems) {
        if (s && s->run) systems_.push_back(s);
    }
    std::stable_sort(systems_.begin(), systems_.end(), [](const SystemInfo* a, const SystemInfo* b) {
        if (a->phase != b->phase) return a->phase == SystemPhase::Fixed;
        return a->order < b->order;
    });
}

void SystemRunner::Reset() {
    time_ = fixedTime_ = carry_ = 0;
    frames_ = 0;
    camera_ = 0;
}

int SystemRunner::Advance(Scene& scene, double dt, float fixedStep) {
    dt = std::max(0.0, dt);
    hooks_.clear();
    int steps = 0;
    if (fixedStep > 0) {
        carry_ += dt;
        // A little slack, so 0.1 s of frames with a 0.02 s step is 5 steps (not 4 and a rounding error).
        const double eps = fixedStep * 1e-6;
        while (carry_ + eps >= fixedStep && steps < kMaxFixedSteps) {
            for (const SystemInfo* s : systems_) {
                if (s->phase != SystemPhase::Fixed) continue;
                SystemContext ctx{scene, fixedStep, fixedTime_, frames_, camera_, gravity_, &hooks_, &assets_};
                s->run(ctx);
                camera_ = ctx.camera;
            }
            carry_ -= fixedStep;
            fixedTime_ += fixedStep;
            ++steps;
        }
        if (steps == kMaxFixedSteps) carry_ = std::min(carry_, static_cast<double>(fixedStep));  // let the rest go
        carry_ = std::max(0.0, carry_);
    }
    for (const SystemInfo* s : systems_) {
        if (s->phase != SystemPhase::Frame) continue;
        SystemContext ctx{scene, static_cast<float>(dt), time_, frames_, camera_, gravity_, &hooks_, &assets_};
        s->run(ctx);
        camera_ = ctx.camera;
    }
    time_ += dt;
    ++frames_;
    return steps;
}

}  // namespace rm::game_production
