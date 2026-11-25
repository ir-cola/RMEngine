#include "rm_engine/animation3d/editor/ClipEvents.h"

#include "core/reflect/Data.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace rm::animation3d {

namespace {

constexpr char kEventsKey[] = "events";

// "clips" の中のクリップ `clip` の "events" の配列。無い (配列でない) ときは null。
const json::Value* EventsIn(const json::Value& clips, std::string_view clip) {
    if (!clips.IsObject()) return nullptr;
    const json::Value* c = clips.Find(clip);
    if (!c || !c->IsObject()) return nullptr;
    const json::Value* events = c->Find(kEventsKey);
    return events && events->IsArray() ? events : nullptr;
}

std::vector<json::Value> EventValues(const json::Value& clips, std::string_view clip) {
    const json::Value* events = EventsIn(clips, clip);
    return events ? events->Items() : std::vector<json::Value>{};
}

double TimeOf(const json::Value& e) { return e.IsObject() ? e.Get("time").AsDouble() : 0.0; }

// `events` をクリップ `clip` に入れ直した "clips": 時刻の順に並べ (同じ時刻は前の順)、空になったものは消す。
// `tracked`: 追う番号 (並べた後の番号に書き換える)。
json::Value WithEvents(const json::Value& clips, std::string_view clip, const std::vector<json::Value>& events, size_t* tracked) {
    std::vector<size_t> order(events.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return TimeOf(events[a]) < TimeOf(events[b]); });
    json::Value list = json::Value::MakeArray();
    size_t found = tracked ? *tracked : 0;
    for (size_t k = 0; k < order.size(); ++k) {
        list.Push(events[order[k]]);
        if (tracked && order[k] == *tracked) found = k;
    }
    if (tracked) *tracked = found;

    // (配列やほかの値に壊れていた "clips" / クリップは、オブジェクトに作り直す)
    json::Value out = clips.IsObject() ? clips : json::Value::MakeObject();
    const json::Value* was = out.Find(clip);
    json::Value c = was && was->IsObject() ? *was : json::Value::MakeObject();
    if (list.Size() == 0) {
        c.Erase(kEventsKey);
    } else {
        c.Set(kEventsKey, std::move(list));
    }
    if (c.Size() == 0) {
        out.Erase(clip);
    } else {
        out.Set(clip, std::move(c));
    }
    return out.Size() == 0 ? json::Value() : out;
}

ClipEvent EventOf(const json::Value& v, reflect::Unknowns* unknowns) {
    ClipEvent e;
    if (v.IsObject()) reflect::Load(e, v, unknowns);
    return e;
}

}  // namespace

float ClampEventTime(float time, float duration) {
    if (!std::isfinite(time)) return 0.f;
    const float end = std::isfinite(duration) ? std::max(duration, 0.f) : 0.f;
    return std::clamp(time, 0.f, end);
}

std::vector<ClipEvent> ReadClipEvents(const json::Value& clips, std::string_view clip) {
    std::vector<ClipEvent> out;
    // 配列の要素 1 つに 1 つ (番号を配列と同じにする。オブジェクトでない物は既定の値)。
    for (const json::Value& v : EventValues(clips, clip)) out.push_back(EventOf(v, nullptr));
    return out;
}

json::Value AddClipEvent(const json::Value& clips, std::string_view clip, const ClipEvent& e, float duration, size_t* index) {
    std::vector<json::Value> events = EventValues(clips, clip);
    ClipEvent put = e;
    put.time = ClampEventTime(put.time, duration);
    events.push_back(reflect::Save(put));
    size_t at = events.size() - 1;
    json::Value out = WithEvents(clips, clip, events, &at);
    if (index) *index = at;
    return out;
}

json::Value SetClipEvent(const json::Value& clips, std::string_view clip, size_t index, const ClipEvent& e, float duration, size_t* newIndex) {
    std::vector<json::Value> events = EventValues(clips, clip);
    if (newIndex) *newIndex = index;
    if (index >= events.size()) return clips;
    reflect::Unknowns unknowns;  // そのイベントの知らないキー (残す)
    (void)EventOf(events[index], &unknowns);
    ClipEvent put = e;
    put.time = ClampEventTime(put.time, duration);
    events[index] = reflect::Save(put, &unknowns);
    size_t at = index;
    json::Value out = WithEvents(clips, clip, events, &at);
    if (newIndex) *newIndex = at;
    return out;
}

json::Value MoveClipEvent(const json::Value& clips, std::string_view clip, size_t index, float time, float duration, size_t* newIndex) {
    const std::vector<json::Value> events = EventValues(clips, clip);
    if (index >= events.size()) {
        if (newIndex) *newIndex = index;
        return clips;
    }
    ClipEvent e = EventOf(events[index], nullptr);
    e.time = time;
    return SetClipEvent(clips, clip, index, e, duration, newIndex);
}

json::Value RemoveClipEvent(const json::Value& clips, std::string_view clip, size_t index) {
    std::vector<json::Value> events = EventValues(clips, clip);
    if (index >= events.size()) return clips;
    events.erase(events.begin() + static_cast<std::ptrdiff_t>(index));
    return WithEvents(clips, clip, events, nullptr);
}

}  // namespace rm::animation3d
