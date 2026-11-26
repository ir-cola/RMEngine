#include "rm_engine/weather/runtime/Lightning.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace rm::weather {

namespace {

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Normalize(Vec3 a) {
    const float l = Length(a);
    return l > 1e-12f ? Mul(a, 1 / l) : Vec3{0, 0, 0};
}

// 起きるかの見込み: 刻みごとに 40 回に 1 回ほど (前の版の空の閃光と同じ)。
constexpr float kChance = 0.975f;

// 線 a -> b を、中点を横 (軸 u, v の向き) にずらしながら `levels` 回 2 つに分けた点の列。ずらす量は
// 最初 `amp`、1 回ごとに半分ほどに。`h`: 次に使う数の番号 (Hash01)。
std::vector<Vec3> Jagged(Vec3 a, Vec3 b, Vec3 u, Vec3 v, float amp, int levels, uint32_t& h) {
    std::vector<Vec3> pts = {a, b};
    for (int level = 0; level < levels; ++level) {
        std::vector<Vec3> next;
        next.reserve(pts.size() * 2);
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            next.push_back(pts[i]);
            const float su = Hash01(h++) * 2 - 1, sv = Hash01(h++) * 2 - 1;
            next.push_back(Add(Mul(Add(pts[i], pts[i + 1]), 0.5f), Add(Mul(u, su * amp), Mul(v, sv * amp))));
        }
        next.push_back(pts.back());
        pts = std::move(next);
        amp *= 0.55f;
    }
    return pts;
}

// 向き d に直角な、互いに直角な 2 つの軸。
void Across(Vec3 d, Vec3& u, Vec3& v) {
    const Vec3 dir = Normalize(d);
    u = Normalize(Cross(dir, std::fabs(dir.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 0, 1}));
    v = Cross(dir, u);
}

}  // namespace

float Hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.f;
}

std::optional<Strike> StrikeAt(double seconds) {
    if (!(seconds > 0) || !std::isfinite(seconds)) return std::nullopt;
    const auto now = static_cast<int64_t>(std::floor(seconds / kStrikeTick));
    const auto back = static_cast<int64_t>(std::ceil(kBoltLife / kStrikeTick));
    for (int64_t t = now; t >= 0 && t >= now - back; --t) {
        const auto tick = static_cast<uint32_t>(t);
        if (Hash01(tick * 2u) <= kChance) continue;
        const float age = static_cast<float>(seconds - static_cast<double>(t) * kStrikeTick);
        if (age >= kBoltLife) break;
        return Strike{tick, std::max(age, 0.f), 0.5f + 0.5f * Hash01(tick * 2u + 1u)};
    }
    return std::nullopt;
}

float BoltGlow(float age) {
    if (!(age >= 0) || age >= kBoltLife) return 0;
    if (age < 0.05f) return 1;      // 落ちた
    if (age < 0.09f) return 0.25f;  // 一瞬暗く
    if (age < 0.15f) return 0.85f;  // 2 度目
    const float u = (age - 0.15f) / (kBoltLife - 0.15f);
    return 0.55f * (1 - u) * (1 - u);  // 残光
}

float FlashGlow(float age) {
    if (!(age >= 0)) return 0;
    if (age < 0.05f) return 1;
    if (age < 0.09f) return 0.35f;
    if (age < 0.15f) return 0.8f;
    if (age < 0.4f) return 0.3f * (1 - (age - 0.15f) / 0.25f);
    return 0;
}

std::vector<BoltSegment> BoltPath(uint32_t seed, Vec3 top, Vec3 bottom) {
    std::vector<BoltSegment> out;
    const Vec3 d = Sub(bottom, top);
    const float len = Length(d);
    if (!(len > 1e-3f) || !std::isfinite(len)) return out;
    Vec3 u, v;
    Across(d, u, v);
    uint32_t h = seed * 7919u + 17u;
    // 幹: 7 回分けて 128 本。
    const std::vector<Vec3> trunk = Jagged(top, bottom, u, v, len * 0.12f, 7, h);
    for (size_t i = 0; i + 1 < trunk.size(); ++i) out.push_back({trunk[i], trunk[i + 1], 1.f, 1.f});
    // 枝: 幹の上から 1 割 〜 7 割の所から、斜め下へ (幹の 2 〜 4 割の長さ)。先へ行くほど暗く細く。
    const int branches = 2 + static_cast<int>(Hash01(h++) * 3);
    const Vec3 down = Mul(d, 1 / len);
    for (int b = 0; b < branches; ++b) {
        const auto at = static_cast<size_t>((0.1f + 0.6f * Hash01(h++)) * static_cast<float>(trunk.size() - 1));
        const float angle = Hash01(h++) * 2 * std::numbers::pi_v<float>;
        const Vec3 side = Add(Mul(u, std::cos(angle)), Mul(v, std::sin(angle)));
        const float reach = len * (0.2f + 0.2f * Hash01(h++));
        const Vec3 start = trunk[at];
        const Vec3 end = Add(start, Mul(Normalize(Add(Mul(down, 0.75f), Mul(side, 0.65f))), reach));
        Vec3 bu, bv;
        Across(Sub(end, start), bu, bv);
        const std::vector<Vec3> twig = Jagged(start, end, bu, bv, reach * 0.15f, 5, h);
        const float n = static_cast<float>(twig.size() - 1);
        for (size_t i = 0; i + 1 < twig.size(); ++i) {
            const float t = static_cast<float>(i) / n;
            out.push_back({twig[i], twig[i + 1], 0.45f * (1 - 0.6f * t), 0.6f * (1 - 0.8f * t)});
        }
    }
    return out;
}

void BoltPlace(uint32_t seed, Vec3 eye, float ground, Vec3& top, Vec3& bottom) {
    const float angle = Hash01(seed * 3u + 101u) * 2 * std::numbers::pi_v<float>;
    const float dist = kBoltNear + (kBoltFar - kBoltNear) * Hash01(seed * 3u + 102u);
    bottom = {eye.x + std::cos(angle) * dist, ground, eye.z + std::sin(angle) * dist};
    // 雲の下の端は、落ちる所の真上から少しずれる (稲妻は斜めに走る)。
    const float lean = Hash01(seed * 3u + 103u) * 2 * std::numbers::pi_v<float>;
    const float leanBy = 60.f * Hash01(seed * 3u + 104u);
    top = {bottom.x + std::cos(lean) * leanBy, ground + kBoltHeight, bottom.z + std::sin(lean) * leanBy};
}

void BoltRibbons(std::span<const BoltSegment> segments, Vec3 eye, float coreWidth, float haloWidth, std::vector<BoltVertex>& out) {
    out.clear();
    out.reserve(segments.size() * 12);
    for (const BoltSegment& s : segments) {
        const Vec3 along = Sub(s.b, s.a);
        const Vec3 toEye = Sub(eye, Mul(Add(s.a, s.b), 0.5f));
        Vec3 side = Normalize(Cross(along, toEye));
        if (Length(side) < 0.5f) side = Vec3{1, 0, 0};
        for (const auto& [width, light] : {std::pair{coreWidth, 1.f}, std::pair{haloWidth, 0.18f}}) {
            const Vec3 off = Mul(side, width * s.width * 0.5f);
            const float l = light * s.glow;
            const BoltVertex a0{Sub(s.a, off), -1, l}, a1{Add(s.a, off), 1, l}, b0{Sub(s.b, off), -1, l}, b1{Add(s.b, off), 1, l};
            for (const BoltVertex& p : {a0, a1, b1, a0, b1, b0}) out.push_back(p);
        }
    }
}

}  // namespace rm::weather
