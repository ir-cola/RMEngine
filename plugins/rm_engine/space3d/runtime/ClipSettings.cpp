#include "rm_engine/space3d/runtime/ClipSettings.h"

#include <algorithm>
#include <cmath>

namespace rm::space3d {

void ApplyClipSettings(std::vector<Clip>& clips, const json::Value& import) {
    const json::Value* all = import.IsObject() ? import.Find("clips") : nullptr;
    if (!all || !all->IsObject()) return;
    for (Clip& clip : clips) {
        const json::Value* one = all->Find(clip.name);
        if (!one || !one->IsObject()) continue;
        const json::Value* events = one->Find("events");
        if (!events || !events->IsArray()) continue;
        clip.events.clear();
        for (const json::Value& ev : events->Items()) {
            if (clip.events.size() >= kMaxClipEvents) break;
            if (!ev.IsObject()) continue;
            const json::Value& time = ev.Get("time");
            const json::Value& name = ev.Get("name");
            if (!time.IsNumber() || !name.IsString() || name.AsString().empty()) continue;
            const double t = time.AsDouble();
            if (!std::isfinite(t)) continue;
            ClipEvent e;
            e.time = std::clamp(static_cast<float>(t), 0.f, std::max(clip.duration, 0.f));
            e.name = name.AsString();
            const double number = ev.Get("number").AsDouble();
            e.number = std::isfinite(number) ? number : 0;
            if (const json::Value& text = ev.Get("text"); text.IsString()) e.text = text.AsString();
            clip.events.push_back(std::move(e));
        }
        std::stable_sort(clip.events.begin(), clip.events.end(), [](const ClipEvent& a, const ClipEvent& b) { return a.time < b.time; });
    }
}

}  // namespace rm::space3d
