#include "rm_engine/game_ui/runtime/UiMotion.h"

#include <algorithm>
#include <cmath>

namespace rm::game_ui {

float Ease(UiEase ease, float t) {
    t = std::clamp(std::isfinite(t) ? t : 1.f, 0.f, 1.f);
    switch (ease) {
        case UiEase::Linear: return t;
        case UiEase::In: return t * t * t;
        case UiEase::Out: {
            const float u = 1 - t;
            return 1 - u * u * u;
        }
        case UiEase::InOut: {
            if (t < 0.5f) return 4 * t * t * t;
            const float u = -2 * t + 2;
            return 1 - u * u * u / 2;
        }
        case UiEase::Bounce: {
            // Falls to the end and bounces there, smaller each time.
            constexpr float n = 7.5625f, d = 2.75f;
            if (t < 1 / d) return n * t * t;
            if (t < 2 / d) {
                t -= 1.5f / d;
                return n * t * t + 0.75f;
            }
            if (t < 2.5f / d) {
                t -= 2.25f / d;
                return n * t * t + 0.9375f;
            }
            t -= 2.625f / d;
            return n * t * t + 0.984375f;
        }
        case UiEase::Back: {
            // Goes past the end a little, then comes back to it.
            constexpr float c1 = 1.70158f, c3 = c1 + 1;
            const float u = t - 1;
            return 1 + c3 * u * u * u + c1 * u * u;
        }
    }
    return t;
}

UiLive Combine(const UiLive& a, const UiLive& b) {
    return {{a.offset.x + b.offset.x, a.offset.y + b.offset.y}, a.scale * b.scale, a.turn + b.turn, a.alpha * b.alpha};
}

namespace {

Vec2 SlideWay(UiSlide s) {
    switch (s) {
        case UiSlide::Left: return {-1, 0};
        case UiSlide::Right: return {1, 0};
        case UiSlide::Up: return {0, -1};
        case UiSlide::Down: return {0, 1};
        default: return {0, 0};
    }
}

}  // namespace

UiLive TransitionAt(const UiTransition& t, bool showing, float time) {
    if (!t.enabled) return {};
    const float local = time - std::max(t.delay, 0.f);
    const float f = t.duration > 1e-4f ? local / t.duration : (local >= 0 ? 1.f : 0.f);
    const float e = Ease(t.ease, f);
    const float away = showing ? 1 - e : e;  // how far from the thing as it is (0: there)
    const Vec2 way = SlideWay(t.slide);
    UiLive l;
    l.offset = {way.x * t.distance * away, way.y * t.distance * away};
    l.scale = std::max(0.f, 1 + (t.scale - 1) * away);
    l.turn = t.turn * away;
    l.alpha = t.fade ? std::clamp(1 - away, 0.f, 1.f) : 1.f;
    return l;
}

bool TransitionDone(const UiTransition& t, float time) { return !t.enabled || time >= std::max(t.delay, 0.f) + std::max(t.duration, 0.f); }

UiPathLine PathLine(const UiPath& path) {
    UiPathLine line;
    const std::vector<Vec2>& p = path.points;
    const size_t n = p.size();
    if (n == 0) return line;
    auto add = [&](Vec2 v) {
        if (!line.points.empty()) {
            const Vec2 last = line.points.back();
            line.along.push_back(line.along.back() + std::hypot(v.x - last.x, v.y - last.y));
        } else {
            line.along.push_back(0);
        }
        line.points.push_back(v);
    };
    add(p[0]);
    if (n == 1) return line;
    const size_t segments = path.closed && n > 2 ? n : n - 1;
    auto at = [&](long long i) {
        if (path.closed && n > 2) return p[static_cast<size_t>(((i % static_cast<long long>(n)) + static_cast<long long>(n)) % static_cast<long long>(n))];
        return p[static_cast<size_t>(std::clamp<long long>(i, 0, static_cast<long long>(n) - 1))];
    };
    constexpr int kPieces = 16;  // short straight pieces per curved segment
    for (size_t s = 0; s < segments; ++s) {
        const long long i = static_cast<long long>(s);
        const Vec2 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        if (!path.smooth) {
            add(p2);
            continue;
        }
        for (int k = 1; k <= kPieces; ++k) {
            // Catmull-Rom: through every point, its tangent from the points beside it.
            const float u = static_cast<float>(k) / kPieces, u2 = u * u, u3 = u2 * u;
            auto c = [&](float a, float b, float d, float e) { return 0.5f * (2 * b + (-a + d) * u + (2 * a - 5 * b + 4 * d - e) * u2 + (-a + 3 * b - 3 * d + e) * u3); };
            add({c(p0.x, p1.x, p2.x, p3.x), c(p0.y, p1.y, p2.y, p3.y)});
        }
    }
    return line;
}

UiPathPose PathPoseAt(const UiPathLine& line, float distance) {
    UiPathPose pose;
    if (line.points.empty()) return pose;
    if (line.points.size() == 1) {
        pose.offset = line.points[0];
        return pose;
    }
    const float d = std::clamp(std::isfinite(distance) ? distance : 0.f, 0.f, line.Length());
    // The piece it is on (the first whose end is at or past it).
    size_t i = 1;
    while (i + 1 < line.points.size() && line.along[i] < d) ++i;
    const Vec2 a = line.points[i - 1], b = line.points[i];
    const float len = line.along[i] - line.along[i - 1];
    const float f = len > 1e-6f ? (d - line.along[i - 1]) / len : 0.f;
    pose.offset = {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
    pose.angle = len > 1e-6f ? std::atan2(b.y - a.y, b.x - a.x) * 57.29577951f : 0.f;
    return pose;
}

UiPathPose StepPath(UiPath& path, const UiPathLine& line, float dt) {
    if (!path.started) {
        path.started = true;
        path.playing = path.playOnStart;
        path.travelled = 0;
        path.backward = false;
    }
    const float length = line.Length();
    if (path.playing && length > 1e-6f) {
        path.travelled += std::max(path.speed, 0.f) * std::max(dt, 0.f);
        switch (path.loop) {
            case UiPathLoop::Once:
                if (path.travelled >= length) {
                    path.travelled = length;
                    path.playing = false;
                }
                break;
            case UiPathLoop::Loop: path.travelled = std::fmod(path.travelled, length); break;
            case UiPathLoop::PingPong: {
                // (how many ends were passed, at once: a loop that takes `length` off each time never ends
                // when `travelled` is far more than a float can tell from it)
                const double ends = std::floor(static_cast<double>(path.travelled) / length);
                path.travelled = static_cast<float>(static_cast<double>(path.travelled) - ends * length);
                if (!std::isfinite(path.travelled) || path.travelled >= length || path.travelled < 0) path.travelled = 0;
                if (std::fmod(ends, 2.0) != 0) path.backward = !path.backward;
                break;
            }
        }
    }
    const float f = length > 1e-6f ? path.travelled / length : 0.f;
    float d = Ease(path.ease, f) * length;
    if (path.backward) d = length - d;
    UiPathPose pose = PathPoseAt(line, d);
    if (path.backward) pose.angle += 180;  // (facing the way it goes)
    return pose;
}

}  // namespace rm::game_ui
