#include "rm_engine/space3d/runtime/ClipSettings.h"

#include "core/reflect/Data.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

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

std::vector<bool> UsedClips(const std::vector<Clip>& clips, const std::vector<std::string>& documents) {
    std::vector<bool> used(clips.size(), false);
    if (clips.empty()) return used;
    // JSON の文字列の値 (キーは除く) を全部。
    std::unordered_set<std::string> strings;
    std::function<void(const json::Value&)> walk = [&](const json::Value& v) {
        if (v.IsString()) {
            strings.insert(v.AsString());
        } else if (v.IsArray()) {
            for (const json::Value& x : v.Items()) walk(x);
        } else if (v.IsObject()) {
            for (const auto& [key, x] : v.Members()) walk(x);
        }
    };
    std::vector<const std::string*> texts;  // JSON でない文書 (スクリプトのソース)
    for (const std::string& d : documents) {
        if (auto doc = json::Parse(d)) {
            walk(*doc);
        } else {
            texts.push_back(&d);
        }
    }
    for (size_t i = 0; i < clips.size(); ++i) {
        const Clip& c = clips[i];
        bool keep = i == 0 || c.bake.always || strings.contains(c.name);
        const std::string quoted = "\"" + c.name + "\"";
        for (size_t k = 0; !keep && k < texts.size(); ++k) keep = texts[k]->find(quoted) != std::string::npos;
        used[i] = keep;
    }
    return used;
}

}  // namespace rm::space3d
