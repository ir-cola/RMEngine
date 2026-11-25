#include "rm_engine/animation3d/runtime/ClipTime.h"

#include <algorithm>
#include <cmath>

namespace rm::animation3d {

namespace {

// [0, duration) に回す。
float Wrap(float t, float duration) {
    t = std::fmod(t, duration);
    if (t < 0) t += duration;
    if (t >= duration) t = 0;  // 負の小さな数に duration を足すと duration に丸まることがある
    return t;
}

}  // namespace

TimeStep AdvanceTime(float time, float dt, float speed, float duration, bool loop) {
    if (!(duration > 0)) return {};
    if (!std::isfinite(time)) time = 0;
    float delta = dt * speed;
    if (!std::isfinite(delta)) delta = 0;
    if (!loop) return {std::clamp(time + delta, 0.f, duration), 0};

    const float start = Wrap(time, duration);
    const float raw = start + delta;
    float n = std::floor(raw / duration);  // 前へ: またいだ数、後ろへ: -(またいだ数)
    float t = raw - n * duration;
    // 割り算の丸めで 1 つずれたときの手直し
    if (t >= duration) {
        t -= duration;
        n += 1;
    }
    if (t < 0) {
        t += duration;
        n -= 1;
    }
    t = std::clamp(t, 0.f, std::nextafter(duration, 0.f));
    const float crossed = std::min(std::abs(n), 1e9f);
    return {t, static_cast<int>(crossed)};
}

void PassedEvents(float prevTime, const TimeStep& step, float speed, float duration, std::span<const float> eventTimes,
                  std::vector<int>& out, bool includeStart) {
    if (!(duration > 0) || speed == 0 || eventTimes.empty()) return;
    const bool wrapped = step.loops > 0;
    const float prev = std::clamp(std::isfinite(prevTime) ? prevTime : 0.f, 0.f, duration);
    const float now = std::clamp(step.time, 0.f, duration);

    // 範囲の中のイベントを時刻の順に (継ぎ目をまたいだときは duration を 0 として)
    struct Ev {
        float t;
        int index;
    };
    std::vector<Ev> evs;
    evs.reserve(eventTimes.size());
    for (size_t i = 0; i < eventTimes.size(); ++i) {
        float e = eventTimes[i];
        if (!std::isfinite(e) || e < 0 || e > duration) continue;
        if (wrapped && e >= duration) e = 0;
        evs.push_back({e, static_cast<int>(i)});
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });

    auto emit = [&](auto&& inRange, bool ascending) {
        if (ascending) {
            for (const Ev& e : evs) {
                if (inRange(e.t)) out.push_back(e.index);
            }
        } else {
            for (auto it = evs.rbegin(); it != evs.rend(); ++it) {
                if (inRange(it->t)) out.push_back(it->index);
            }
        }
    };
    const int fullLoops = wrapped ? std::min(step.loops - 1, kMaxEventLoops) : 0;
    auto whole = [&](float e) { return e < duration; };

    if (speed > 0) {
        auto fromPrev = [&](float e) { return includeStart ? prev <= e : prev < e; };
        if (!wrapped) {
            emit([&](float e) { return fromPrev(e) && e <= now; }, true);
            return;
        }
        emit([&](float e) { return fromPrev(e) && e < duration; }, true);
        for (int k = 0; k < fullLoops; ++k) emit(whole, true);
        emit([&](float e) { return e <= now; }, true);
    } else {
        auto toPrev = [&](float e) { return includeStart ? e <= prev : e < prev; };
        if (!wrapped) {
            emit([&](float e) { return now <= e && toPrev(e); }, false);
            return;
        }
        emit([&](float e) { return toPrev(e); }, false);
        for (int k = 0; k < fullLoops; ++k) emit(whole, false);
        emit([&](float e) { return now <= e && e < duration; }, false);
    }
}

}  // namespace rm::animation3d
