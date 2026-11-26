#include "rm_engine/theme_explore/editor/Motion.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::theme_explore::motion {

namespace {

constexpr float kTau = 2.f * std::numbers::pi_v<float>;

// sin(t * speed + phase) を、大きな t でも細かさを失わないように double で。
float Wave(double t, double speed, double phase) { return static_cast<float>(std::sin(t * speed + phase)); }

// 色の段を 0 .. 1 でなめらかにたどる。
template <size_t N>
Color Ramp(const Color (&stops)[N], float x) {
    x = std::clamp(x, 0.f, 1.f) * static_cast<float>(N - 1);
    const size_t i = std::min(static_cast<size_t>(x), N - 2);
    return rm::Mix(stops[i], stops[i + 1], x - static_cast<float>(i));
}

}  // namespace

float Rand(uint32_t seed, uint32_t index, uint32_t salt) {
    // (PCG の混ぜ方)
    uint32_t h = seed * 747796405u + index * 2891336453u + salt * 277803737u + 0x9E3779B9u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return static_cast<float>(h >> 8) * (1.f / 16777216.f);
}

float Wrap(float v, float lo, float hi) {
    const float span = hi - lo;
    if (!(span > 0)) return lo;
    float r = std::fmod(v - lo, span);
    if (r < 0) r += span;
    if (r >= span) r = 0;  // (丸めで span ちょうどになったとき)
    return lo + r;
}

float Twinkle(double t, float phase, float speed, float depth) {
    // 3 つの波 (速さの比が割り切れない) の重なり: -1 .. 1 → 0 .. 1
    const float w = (Wave(t, speed, phase) + 0.6f * Wave(t, speed * 2.37, phase * 1.7 + 1.3) + 0.35f * Wave(t, speed * 5.11, phase * 2.9 + 4.1)) / 1.95f;
    const float f = 0.5f + 0.5f * w;
    return 1.f - std::clamp(depth, 0.f, 1.f) * (1.f - f * f);
}

float Flicker(double t, float phase) {
    const float w = (Wave(t, 11.0, phase) + 0.7f * Wave(t, 23.7, phase * 2.1 + 0.4) + 0.5f * Wave(t, 37.3, phase * 3.3 + 2.2)) / 2.2f;
    return std::clamp(0.5f + 0.5f * w, 0.f, 1.f);
}

float Sway(double t, float phase, float speed) { return (Wave(t, speed, phase) + 0.5f * Wave(t, speed * 2.71, phase * 1.9 + 0.7)) / 1.5f; }

Life LifeAt(double t, float period, float offset) {
    if (!(period > 0)) return {};
    const double x = t / period + offset;
    const double whole = std::floor(x);
    Life l;
    l.age = std::clamp(static_cast<float>(x - whole), 0.f, 1.f);
    l.cycle = static_cast<uint32_t>(static_cast<int64_t>(whole));
    return l;
}

// --- 宇宙 ---------------------------------------------------------------------------------------

Star StarAt(const ui::Rect& w, uint32_t i, double t, uint32_t seed) {
    Star s;
    const float kind = Rand(seed, i, 1);
    s.layer = kind < 0.70f ? 0 : (kind < 0.95f ? 1 : 2);
    // 深いほど遅く流れる (視差)。左へ、少し下へ。
    constexpr float kSpeed[3] = {1.2f, 2.6f, 4.6f};
    const float speed = kSpeed[s.layer];
    const float x = w.x + w.w * Rand(seed, i, 2) - static_cast<float>(std::fmod(t * speed, 1.0e6));
    const float y = w.y + w.h * Rand(seed, i, 3) + static_cast<float>(std::fmod(t * speed * 0.18, 1.0e6));
    s.at = {Wrap(x, w.x - 4.f, w.x + w.w + 4.f), Wrap(y, w.y - 4.f, w.y + w.h + 4.f)};
    constexpr float kRadius[3][2] = {{0.40f, 0.65f}, {0.60f, 0.90f}, {0.90f, 1.40f}};
    constexpr float kBright[3][2] = {{0.30f, 0.70f}, {0.55f, 0.90f}, {0.80f, 1.00f}};
    const float r = Rand(seed, i, 4);
    s.radius = kRadius[s.layer][0] + (kRadius[s.layer][1] - kRadius[s.layer][0]) * r;
    const float base = kBright[s.layer][0] + (kBright[s.layer][1] - kBright[s.layer][0]) * Rand(seed, i, 5);
    // 小さな星ほど強くまたたく (大気ではないが、絵として)。
    const float depth = s.layer == 0 ? 0.55f : (s.layer == 1 ? 0.40f : 0.25f);
    s.brightness = base * Twinkle(t, kTau * Rand(seed, i, 6), 0.6f + 2.2f * Rand(seed, i, 7), depth);
    // 多くは白に近く、ときどき青い星・橙の星。
    const float c = Rand(seed, i, 8);
    s.temperature = c < 0.18f ? 0.05f * c / 0.18f : (c < 0.85f ? 0.25f + 0.25f * (c - 0.18f) / 0.67f : 0.6f + 0.4f * (c - 0.85f) / 0.15f);
    return s;
}

Color StarColor(float temperature) {
    static const Color kStops[] = {{0.62f, 0.74f, 1.00f, 1}, {0.85f, 0.90f, 1.00f, 1}, {1.00f, 0.98f, 0.95f, 1}, {1.00f, 0.88f, 0.66f, 1}, {1.00f, 0.68f, 0.45f, 1}};
    return Ramp(kStops, temperature);
}

Meteor MeteorAt(const ui::Rect& w, double t, uint32_t seed, float slot) {
    Meteor m;
    if (!(slot > 1.5f) || w.w <= 0 || w.h <= 0) return m;
    const Life life = LifeAt(t, slot, 0.f);
    const uint32_t n = life.cycle;
    if (Rand(seed, n, 20) > 0.7f) return m;  // この枠には来ない
    const float duration = 0.7f + 0.6f * Rand(seed, n, 21);
    const float start = (slot - duration - 0.2f) * Rand(seed, n, 22);
    const float local = life.age * slot - start;
    if (local < 0 || local > duration) return m;
    const float u = local / duration;  // 0 .. 1
    // 上のほうから、左下へ (200 .. 240 度) 斜めに。
    const float angle = (200.f + 40.f * Rand(seed, n, 23)) * std::numbers::pi_v<float> / 180.f;
    const Vec2 dir{std::cos(angle), -std::sin(angle)};  // (画面の y は下向き: 下へ行くと y が増える)
    const float travel = std::clamp(w.w * 0.28f, 220.f, 520.f) * (0.8f + 0.4f * Rand(seed, n, 24));
    const Vec2 from{w.x + w.w * (0.25f + 0.7f * Rand(seed, n, 25)), w.y + w.h * (0.05f + 0.45f * Rand(seed, n, 26))};
    m.visible = true;
    m.head = {from.x + dir.x * travel * u, from.y + dir.y * travel * u};
    const float tailLength = travel * 0.45f * std::min(1.f, u * 3.f) * (1.f - 0.5f * u);  // 伸びてから縮む
    m.tail = {-dir.x * tailLength, -dir.y * tailLength};
    m.brightness = std::sin(std::numbers::pi_v<float> * u) * (0.7f + 0.3f * Rand(seed, n, 27));
    return m;
}

// --- 深海 ---------------------------------------------------------------------------------------

Bubble BubbleAt(const ui::Rect& w, uint32_t i, double t, uint32_t seed) {
    Bubble b;
    // 3 つの噴き出し口 (場所は種から) と、ばらばらの泡 (4 つに 1 つ)。
    const uint32_t vent = i % 4;
    const float ventX = vent < 3 ? w.x + w.w * (0.55f + 0.4f * Rand(seed, vent, 30)) : w.x + w.w * Rand(seed, i, 31);
    const float rise = 38.f + 34.f * Rand(seed, i, 32);  // DIP / 秒
    const float height = w.h + 40.f;
    const Life life = LifeAt(t, height / rise, Rand(seed, i, 33));
    const float y = w.y + w.h + 20.f - life.age * height;
    const float wobble = (3.f + 6.f * Rand(seed, i, 34)) * Sway(t, kTau * Rand(seed, i, 35), 2.2f + 2.f * Rand(seed, i, 36));
    const float drift = (vent < 3 ? 14.f : 30.f) * (Rand(seed, i, 37 + life.cycle) - 0.5f) + 18.f * life.age * (Rand(seed, i, 38) - 0.5f);
    b.at = {ventX + drift + wobble, y};
    b.radius = (1.6f + 4.8f * Rand(seed, i, 39) * Rand(seed, i, 40)) * (1.f + 0.35f * life.age);  // 上るほど (水圧が下がり) 大きく
    b.alpha = std::min({1.f, life.age * 12.f, (1.f - life.age) * 8.f});
    return b;
}

Mote SnowAt(const ui::Rect& w, uint32_t i, double t, uint32_t seed) {
    Mote m;
    const bool isNear = Rand(seed, i, 50) < 0.25f;
    const float fall = isNear ? 7.f + 6.f * Rand(seed, i, 51) : 2.5f + 3.f * Rand(seed, i, 51);
    const float x = w.x + w.w * Rand(seed, i, 52) + (isNear ? 9.f : 5.f) * Sway(t, kTau * Rand(seed, i, 53), 0.25f + 0.3f * Rand(seed, i, 54)) +
                    static_cast<float>(std::fmod(t * 1.5, 1.0e6));  // (流れ: 右へゆっくり)
    const float y = w.y + w.h * Rand(seed, i, 55) + static_cast<float>(std::fmod(t * fall, 1.0e6));
    m.at = {Wrap(x, w.x - 6.f, w.x + w.w + 6.f), Wrap(y, w.y - 6.f, w.y + w.h + 6.f)};
    m.radius = isNear ? 1.1f + 1.0f * Rand(seed, i, 56) : 0.5f + 0.6f * Rand(seed, i, 56);
    // 上 (光が届く所) ほど見える。
    const float depth = (m.at.y - w.y) / std::max(w.h, 1.f);
    m.alpha = (isNear ? 0.55f : 0.35f) * (1.f - 0.6f * depth) * (0.6f + 0.4f * Rand(seed, i, 57));
    return m;
}

Mote GlowAt(const ui::Rect& w, uint32_t i, double t, uint32_t seed) {
    Mote m;
    // 深い所 (下の 2/3) を、ゆっくり漂う。
    const float x = w.x + w.w * Rand(seed, i, 60) + 40.f * Sway(t, kTau * Rand(seed, i, 61), 0.05f + 0.06f * Rand(seed, i, 62));
    const float y = w.y + w.h * (0.35f + 0.62f * Rand(seed, i, 63)) + 25.f * Sway(t, kTau * Rand(seed, i, 64), 0.04f + 0.05f * Rand(seed, i, 65));
    m.at = {Wrap(x, w.x, w.x + w.w), y};
    m.radius = 1.4f + 1.8f * Rand(seed, i, 66);
    // ゆっくり光ってはまた暗くなる (ときどき強く)。
    const float pulse = 0.5f + 0.5f * Sway(t, kTau * Rand(seed, i, 67), 0.35f + 0.5f * Rand(seed, i, 68));
    m.alpha = std::pow(std::clamp(pulse, 0.f, 1.f), 3.f);
    return m;
}

// --- 火山 ---------------------------------------------------------------------------------------

Ember EmberAt(const ui::Rect& w, uint32_t i, double t, uint32_t seed, Vec2 vent, float ventShare) {
    Ember e;
    const float lifetime = 3.5f + 5.5f * Rand(seed, i, 70);
    const Life life = LifeAt(t, lifetime, Rand(seed, i, 71));
    const uint32_t n = i * 131u + life.cycle;  // (一生ごとに変わる数の種)
    // 生まれる場所: ventShare の割合は火口 (噴き上がる)、残りは下の端のどこでも (舞い上がる)。
    const bool fromVent = Rand(seed, n, 72) < ventShare;
    const Vec2 born = fromVent ? Vec2{vent.x + 14.f * (Rand(seed, n, 73) - 0.5f), vent.y}
                               : Vec2{w.x + w.w * Rand(seed, n, 74), w.y + w.h + 6.f - w.h * 0.10f * Rand(seed, n, 75)};
    const float rise = fromVent ? 45.f + 80.f * Rand(seed, i, 76) : 22.f + 60.f * Rand(seed, i, 76);  // DIP / 秒
    const float side = fromVent ? 50.f * (Rand(seed, n, 85) - 0.5f) : 0.f;  // 火口からは左右にも散る
    const float age = life.age * lifetime;  // 秒
    // 上るほど遅くなる (空気に止められる)。横へは風 (右から左) と、渦のような揺れ。
    const float slow = 1.f - 0.35f * life.age;
    const float windSpeed = 6.f + 14.f * Rand(seed, i, 77);
    const float swirlSize = 8.f + 22.f * Rand(seed, i, 78), swirlSpeed = 0.6f + 1.2f * Rand(seed, i, 80);
    const float phase = kTau * Rand(seed, i, 79);
    e.at = {born.x + (side * slow - windSpeed) * age + swirlSize * Sway(t, phase, swirlSpeed), born.y - rise * age * slow};
    e.velocity = {side * slow - windSpeed + swirlSize * swirlSpeed * 0.6f * Sway(t + 0.4, phase, swirlSpeed), -rise * (1.f - 0.7f * life.age)};
    e.radius = 0.7f + 1.6f * Rand(seed, i, 81) * Rand(seed, i, 82);
    e.heat = std::clamp(1.f - life.age * (0.8f + 0.4f * Rand(seed, i, 83)), 0.f, 1.f);
    const float fade = std::min({1.f, life.age * 10.f, (1.f - life.age) * 3.f});
    e.alpha = fade * (0.55f + 0.45f * Flicker(t, kTau * Rand(seed, i, 84)));
    return e;
}

Color EmberColor(float heat) {
    static const Color kStops[] = {{0.45f, 0.04f, 0.01f, 1}, {0.85f, 0.18f, 0.02f, 1}, {1.00f, 0.45f, 0.06f, 1}, {1.00f, 0.75f, 0.25f, 1}, {1.00f, 0.95f, 0.70f, 1}};
    return Ramp(kStops, heat);
}

}  // namespace rm::theme_explore::motion
