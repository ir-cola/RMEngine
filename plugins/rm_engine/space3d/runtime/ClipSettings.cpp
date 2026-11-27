#include "rm_engine/space3d/runtime/ClipSettings.h"

#include "core/reflect/Data.h"

#include <algorithm>
#include <cmath>

namespace rm::space3d {

float ClampEventTime(float time, float duration) {
    if (!std::isfinite(time)) return 0.f;
    const float end = std::isfinite(duration) ? std::max(duration, 0.f) : 0.f;
    return std::clamp(time, 0.f, end);
}

void ApplyClipSettings(std::vector<Clip>& clips, const json::Value& import) {
    const json::Value* all = import.IsObject() ? import.Find(kClipsKey) : nullptr;
    if (!all || !all->IsObject()) return;
    for (Clip& clip : clips) {
        const json::Value* one = all->Find(clip.name);
        if (!one || !one->IsObject()) continue;
        if (const json::Value* root = one->Find(kClipRootKey); root && root->IsObject()) {
            clip.root = {};
            reflect::Load(clip.root, *root);
        }
        if (const json::Value* bake = one->Find(kClipBakeKey); bake && bake->IsObject()) {
            clip.bake = {};
            reflect::Load(clip.bake, *bake);
        }
        const json::Value* events = one->Find("events");
        if (!events || !events->IsArray()) continue;
        clip.events.clear();
        for (const json::Value& v : events->Items()) {
            if (clip.events.size() >= kMaxClipEvents) break;
            if (!v.IsObject()) continue;
            ClipEvent e;
            reflect::Load(e, v);
            if (e.name.empty()) continue;
            e.time = ClampEventTime(e.time, clip.duration);
            if (!std::isfinite(e.number)) e.number = 0;
            clip.events.push_back(std::move(e));
        }
        std::stable_sort(clip.events.begin(), clip.events.end(), [](const ClipEvent& a, const ClipEvent& b) { return a.time < b.time; });
    }
}

}  // namespace rm::space3d
