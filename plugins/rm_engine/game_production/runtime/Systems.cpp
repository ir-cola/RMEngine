#include "rm_engine/game_production/runtime/Systems.h"

#include <algorithm>
#include <limits>

#include <chrono>

namespace rm::game_production {

void ForEachComponent(Scene& scene, std::string_view type, const std::function<void(SceneEntity&, Component&)>& fn) {
    // TreeOrder が動くエンティティを返す。id で持つのは、システムがシーンのリストを変えるかもしれないから。
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
            e = scene.Find(id);  // fn が部品を足した (リストが動いた) か、エンティティを消したかもしれない
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

std::vector<const reflect::TypeDesc*> SettingTypes(const RuntimeRegistry& registry) {
    std::vector<const reflect::TypeDesc*> out;
    for (const auto& s : registry.Contributions<SceneSettingInfo>(kSceneSettingPoint)) {
        const reflect::TypeDesc* t = &s.item->type();
        if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
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

void SystemRunner::Run(size_t index, SystemContext& ctx) {
    if (!timing_) {
        systems_[index]->run(ctx);
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    systems_[index]->run(ctx);
    SystemTime& t = times_[index];
    t.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++t.runs;
}

int SystemRunner::Advance(Scene& scene, double dt, float fixedStep) {
    dt = std::max(0.0, dt);
    hooks_.clear();
    events_.clear();
    times_.assign(timing_ ? systems_.size() : 0, {});
    for (size_t i = 0; i < times_.size(); ++i) times_[i].system = systems_[i];
    int steps = 0;
    if (fixedStep > 0) {
        carry_ += dt;
        // 少し余裕を持たせる。0.02 秒刻みで 0.1 秒分のフレームは 5 ステップ (丸め誤差で 4 にはしない)。
        const double eps = fixedStep * 1e-6;
        while (carry_ + eps >= fixedStep && steps < kMaxFixedSteps) {
            for (size_t i = 0; i < systems_.size(); ++i) {
                if (systems_[i]->phase != SystemPhase::Fixed) continue;
                SystemContext ctx{scene, fixedStep, fixedTime_, frames_, camera_, &hooks_, &assets_, &events_};
                Run(i, ctx);
                camera_ = ctx.camera;
            }
            carry_ -= fixedStep;
            fixedTime_ += fixedStep;
            ++steps;
        }
        if (steps == kMaxFixedSteps) carry_ = std::min(carry_, static_cast<double>(fixedStep));  // 残りは捨てる
        carry_ = std::max(0.0, carry_);
    }
    for (size_t i = 0; i < systems_.size(); ++i) {
        if (systems_[i]->phase != SystemPhase::Frame) continue;
        SystemContext ctx{scene, static_cast<float>(dt), time_, frames_, camera_, &hooks_, &assets_, &events_};
        Run(i, ctx);
        camera_ = ctx.camera;
    }
    time_ += dt;
    ++frames_;
    lastSteps_ = steps;
    return steps;
}

}  // namespace rm::game_production
