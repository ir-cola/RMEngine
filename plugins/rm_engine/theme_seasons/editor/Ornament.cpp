#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace rm::theme_seasons::ornament {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDeg = kPi / 180.f;

Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 Scale(Vec2 a, float s) { return {a.x * s, a.y * s}; }

}  // namespace

// --- 春 -----------------------------------------------------------------------------------

ui::Path Sakura(Vec2 center, float radius, float degreesCcw) {
    // 上を向いた花びら 1 枚。ユーザーがくれた絵をなぞったもの (先は距離 1): 中心の近くは丸い先、
    // 半分より少し先がいちばん太く、先は 2 つで間に V の切れ込みがある。
    ui::Path path;
    for (int i = 0; i < 5; ++i) {
        const float a = degreesCcw + 72.f * static_cast<float>(i);
        auto at = [&](float x, float y) { return Add(center, Scale(Rotate({x, y}, a), radius)); };
        // (画面上で時計回り: 左側を上り、切れ込み、右側を下りる。2026-11-26: 前は反時計回りで、枝などと
        // 重ねて塗ると重なった所が穴になった)
        path.Begin(at(0, -0.095f));
        path.Cubic(at(-0.17f, -0.17f), at(-0.285f, -0.40f), at(-0.285f, -0.58f));
        path.Cubic(at(-0.285f, -0.78f), at(-0.19f, -0.92f), at(-0.09f, -0.996f));
        path.Line(at(0, -0.84f));  // 切れ込み
        path.Line(at(0.09f, -0.996f));
        path.Cubic(at(0.19f, -0.92f), at(0.285f, -0.78f), at(0.285f, -0.58f));
        path.Cubic(at(0.285f, -0.40f), at(0.17f, -0.17f), at(0, -0.095f));
    }
    return path;
}

ui::Path CherryTree(const ui::Rect& win, float d) {
    ui::Path p;
    Random rnd{0xC0FFEEu};
    const float right = win.x + win.w, top = win.y, bottom = win.y + win.h;
    // 樹冠: 上のほうで広く、左が少し重く、てっぺんはウィンドウの上端を少し越える。
    const float rx = d, ry = std::min(std::max(0.25f * win.h, 0.45f * d), 0.8f * d);
    const Vec2 crown{right - 0.62f * d, top + 0.8f * ry};
    const Vec2 fork{right - 0.52f * d, crown.y + ry + 0.1f * d};
    const float foot = right - 0.42f * d;
    // 幹: 下の端から枝分かれまで伸びる柱で、少し曲がり、根元で根に広がる。
    constexpr int kSteps = 24;
    std::vector<Vec2> trunk(2 * (kSteps + 1));
    for (int k = 0; k <= kSteps; ++k) {
        const float s = static_cast<float>(k) / kSteps;
        const float x = foot + (fork.x - foot) * s + 0.06f * d * std::sin(kPi * s);
        const float y = bottom + 4.f + (fork.y - bottom - 4.f) * s;
        float half = 0.13f * d * (1.f - 0.45f * s);
        if (s < 0.12f) half *= 1.f + 1.6f * ((0.12f - s) / 0.12f) * ((0.12f - s) / 0.12f);
        trunk[static_cast<size_t>(k)] = {x - half, y};
        trunk[trunk.size() - 1 - static_cast<size_t>(k)] = {x + half, y};
    }
    AddPolygon(p, trunk);
    // 枝: 枝分かれから上と外へ樹冠の中に伸び、それぞれ少し肘のように曲がる。
    const Vec2 ends[] = {{crown.x - 0.72f * rx, crown.y + 0.50f * ry}, {crown.x - 0.30f * rx, crown.y + 0.05f * ry},
                         {crown.x + 0.45f * rx, crown.y + 0.30f * ry}, {crown.x - 0.05f * rx, crown.y - 0.45f * ry},
                         {crown.x + 0.25f * rx, crown.y - 0.15f * ry}};
    const float widths[] = {0.085f, 0.08f, 0.075f, 0.06f, 0.055f};
    for (size_t i = 0; i < std::size(ends); ++i) {
        const float wd = widths[i] * d;
        const Vec2 mid{(fork.x + ends[i].x) * 0.5f + 0.05f * d, (fork.y + ends[i].y) * 0.5f + 0.04f * d};
        AddBar(p, fork, mid, wd, wd * 0.7f);
        AddBar(p, mid, ends[i], wd * 0.7f, 0.02f * d);
    }
    // 幹から出る低い枝。樹冠の下を左へ伸びる。
    const Vec2 lowFrom{foot - 0.01f * d, fork.y + 0.30f * (bottom - fork.y)};
    const Vec2 lowTo{right - 1.30f * d, fork.y - 0.10f * d};
    const Vec2 lowMid{(lowFrom.x + lowTo.x) * 0.5f, (lowFrom.y + lowTo.y) * 0.5f + 0.06f * d};
    AddBar(p, lowFrom, lowMid, 0.07f * d, 0.045f * d);
    AddBar(p, lowMid, lowTo, 0.045f * d, 0.015f * d);
    // 花: 中身の詰まった真ん中、樹冠を埋める大きなふくらみ、縁のまわりの小さなふくらみ (ふわっとして、
    // 泡立ってはいない)、低い枝の先の房。
    AddEllipse(p, crown, 0.72f * rx, 0.72f * ry, 0);
    for (int i = 0; i < 110; ++i) {
        const float a = rnd.Next() * 2.f * kPi, t = std::sqrt(rnd.Next()) * 0.85f;
        AddCircle(p, {crown.x + std::cos(a) * t * rx, crown.y + std::sin(a) * t * ry}, d * (0.10f + 0.08f * rnd.Next()) * (1.f - 0.4f * t));
    }
    for (int i = 0; i < 70; ++i) {
        const float a = rnd.Next() * 2.f * kPi, t = 0.85f + 0.15f * rnd.Next();
        AddCircle(p, {crown.x + std::cos(a) * t * rx, crown.y + std::sin(a) * t * ry}, d * (0.05f + 0.05f * rnd.Next()));
    }
    for (int i = 0; i < 18; ++i) {
        const float a = rnd.Next() * 2.f * kPi, t = std::sqrt(rnd.Next());
        AddCircle(p, {lowTo.x + 0.05f * d + std::cos(a) * t * 0.26f * d, lowTo.y + std::sin(a) * t * 0.14f * d}, d * (0.05f + 0.04f * rnd.Next()));
    }
    // その下に落ちる花びらが少し。
    for (int i = 0; i < 12; ++i) {
        const Vec2 c{right - d * (0.2f + 1.4f * rnd.Next()), crown.y + ry + 0.1f * d + (bottom - crown.y - ry) * 0.7f * rnd.Next()};
        AddEllipse(p, c, 0.025f * d, 0.015f * d, rnd.Next() * 180.f);
    }
    return p;
}

ui::Path PetalStream(const ui::Rect& win, float fromX, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    const float span = std::max(fromX - win.x, 1.f);
    for (int i = 0; i < count; ++i) {
        // どれだけ飛んだか (0 = 木のところ): ほとんどはまだ木の近く。
        const float u = std::pow(rnd.Next(), 1.8f);
        const float x = fromX + 20.f * rnd.Next() - span * u;
        // 樹冠 (上半分) から吹き出され、飛ぶにつれて沈む。
        const float y = win.y + win.h * (0.04f + 0.42f * rnd.Next()) + win.h * 0.35f * u;
        if (y > win.y + win.h) continue;
        const float s = size * (0.6f + 0.7f * rnd.Next()) * (1.f - 0.3f * u);
        AddEllipse(p, {x, y}, s * 0.5f, s * 0.3f, rnd.Next() * 180.f);
    }
    return p;
}

ui::Path Petals(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        // 左上の角の近くに多く、離れるほど少ない。
        const float u = rnd.Next(), v = rnd.Next();
        const Vec2 c{area.x + area.w * u * u, area.y + area.h * v * (0.4f + 0.6f * u)};
        const float s = size * (0.6f + 0.7f * rnd.Next());
        AddEllipse(p, c, s * 0.5f, s * 0.3f, rnd.Next() * 180.f);
    }
    return p;
}

// --- 夏 -----------------------------------------------------------------------------------

ui::Path Sun(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    AddCircle(p, center, 0.50f * r);
    for (int i = 0; i < 12; ++i) {
        const float a = degreesCcw + static_cast<float>(i) * 30.f;
        const float tip = i % 2 == 0 ? 1.f : 0.80f;  // 長い光線と短い光線が交互
        const Vec2 ray[3] = {Add(center, Scale(Rotate({-0.10f, -0.58f}, a), r)), Add(center, Scale(Rotate({0, -tip}, a), r)),
                             Add(center, Scale(Rotate({0.10f, -0.58f}, a), r))};
        AddPolygon(p, ray);
    }
    return p;
}

CloudShapes Cumulus(Vec2 c, float w, uint32_t seed) {
    CloudShapes out;
    if (w <= 0) return out;
    Random rnd{seed};
    auto jitter = [&](float amount) { return (rnd.Next() - 0.5f) * 2.f * amount; };
    const float base = c.y + 0.16f * w;  // 平らな底
    struct Puff {
        float x, y, r;
    };
    std::vector<Puff> puffs;
    // 底に並ぶ小さめのふくらみ (両端はもっと小さい)。どれも底の線より少し下まで出て、そこで平らに切る。
    constexpr float kRow[] = {-0.43f, -0.30f, -0.16f, -0.02f, 0.13f, 0.27f, 0.40f};
    for (size_t i = 0; i < std::size(kRow); ++i) {
        const float end = (i == 0 || i + 1 == std::size(kRow)) ? 0.75f : 1.f;
        const float r = (0.095f + 0.035f * rnd.Next()) * end * w;
        puffs.push_back({c.x + (kRow[i] + jitter(0.015f)) * w, base - 0.78f * r, r});
    }
    // その上に積み上がる大きなふくらみ。少し左寄りの真ん中が一番高い。肩には小さなふくらみ (でこぼこの上の縁)。
    // (x, 底からの高さ, 半径)
    constexpr std::array<std::array<float, 3>, 7> kTall{{{-0.25f, 0.16f, 0.13f}, {0.23f, 0.15f, 0.12f}, {-0.09f, 0.21f, 0.17f},
                                                         {0.10f, 0.19f, 0.15f}, {-0.02f, 0.30f, 0.13f}, {-0.17f, 0.32f, 0.07f},
                                                         {0.15f, 0.30f, 0.075f}}};
    for (const auto& [x, lift, r] : kTall)
        puffs.push_back({c.x + (x + jitter(0.02f)) * w, base - (lift + jitter(0.015f)) * w, r * (0.9f + 0.2f * rnd.Next()) * w});

    // 縦の列ごとに、ふくらみの重なりの上の縁と下の縁 (底の線で切る。底に並ぶふくらみの間は底まで埋める)。
    float x0 = 1e30f, x1 = -1e30f;
    for (const Puff& f : puffs) x0 = std::min(x0, f.x - f.r), x1 = std::max(x1, f.x + f.r);
    const float flatFrom = puffs.front().x, flatTo = puffs[std::size(kRow) - 1].x;
    const int n = std::clamp(static_cast<int>((x1 - x0) / 2.5f), 24, 120);
    std::vector<float> xs(static_cast<size_t>(n) + 1), tops(xs.size()), bottoms(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) {
        const float x = x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(n);
        float top = base, bottom = -1e30f;
        for (const Puff& f : puffs) {
            const float dx = x - f.x;
            if (std::fabs(dx) > f.r + 1e-3f) continue;
            const float half = std::sqrt(std::max(0.f, f.r * f.r - dx * dx));
            top = std::min(top, f.y - half);
            bottom = std::max(bottom, f.y + half);
        }
        bottom = (x >= flatFrom && x <= flatTo) ? base : std::clamp(bottom, top, base);
        xs[i] = x, tops[i] = top, bottoms[i] = bottom;
    }
    // 各列の上の縁から底までのうち、`from` .. `to` の割合の帯 (0 = 上の縁、1 = 底)。上を右へ、下を左へ戻る (時計回り)。
    auto band = [&](float from, float to) {
        std::vector<Vec2> pts;
        pts.reserve(2 * xs.size());
        for (size_t i = 0; i < xs.size(); ++i) pts.push_back({xs[i], tops[i] + (bottoms[i] - tops[i]) * from});
        for (size_t i = xs.size(); i-- > 0;) pts.push_back({xs[i], tops[i] + (bottoms[i] - tops[i]) * to});
        ui::Path p;
        AddPolygon(p, pts);
        return p;
    };
    out.body = band(0.f, 1.f);
    out.light = {band(0.f, 0.42f), band(0.f, 0.26f), band(0.f, 0.12f)};
    out.shade = {band(0.45f, 1.f), band(0.60f, 1.f), band(0.74f, 1.f), band(0.87f, 1.f)};
    return out;
}

ui::Path SandCastle(Vec2 base, float h) {
    ui::Path p;
    Random rnd{0x5A2Du};
    auto at = [&](float x, float y) { return Vec2{base.x + x * h, base.y - y * h}; };
    auto wobble = [&](float amount) { return (rnd.Next() - 0.5f) * 2.f * amount; };
    // 固めた砂の塊: 地面の x0 から x1 まで `top` の高さまで。側面は内側に傾き (足元のほうが広く)、
    // でこぼこにふくらみ、上には銃眼のかわりに丸い塊がいくつか。
    auto lump = [&](float x0, float x1, float top, int bumps, float lean) {
        std::vector<Vec2> pts;
        const float inset = (x1 - x0) * 0.12f;
        const int sides = 5;
        for (int i = 0; i <= sides; ++i) {  // 左側を上る
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            pts.push_back(at(x0 + inset * t + lean * t + wobble(0.012f), top * t));
        }
        for (int i = 1; i < 6; ++i) {  // 上を横切る (少しでこぼこ)
            const float t = static_cast<float>(i) / 6.f;
            pts.push_back(at(x0 + inset + lean + (x1 - x0 - 2.f * inset) * t, top + wobble(0.018f)));
        }
        for (int i = sides; i >= 0; --i) {  // 右側を下る
            const float t = static_cast<float>(i) / static_cast<float>(sides);
            pts.push_back(at(x1 - inset * t + lean * t + wobble(0.012f), top * t));
        }
        AddPolygon(p, pts);
        const float w = x1 - x0 - 2.f * inset;
        for (int i = 0; i < bumps; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bumps);
            AddEllipse(p, at(x0 + inset + lean + w * t, top + 0.01f), w / static_cast<float>(bumps) * 0.42f * h,
                       (0.05f + 0.02f * rnd.Next()) * h, wobble(12.f));
        }
    };
    lump(-0.62f, 0.62f, 0.36f, 5, 0.f);       // 壁
    lump(-0.70f, -0.34f, 0.62f, 2, 0.02f);    // 塔。少し傾く
    lump(0.36f, 0.70f, 0.56f, 2, -0.025f);
    lump(-0.19f, 0.19f, 0.86f, 3, 0.015f);    // 主塔
    AddEllipse(p, at(0.f, 0.86f + 0.07f), 0.07f * h, 0.06f * h, 0);  // てっぺんの丸いこぶ
    // 足元の砂の山 (浜の上ではなく、浜に埋まっている)。
    AddEllipse(p, at(0.f, 0.02f), 0.86f * h, 0.12f * h, 0);
    return p;
}

ui::Path BeachParasol(Vec2 base, float h, float degreesCcw) {
    ui::Path p;
    auto at = [&](float x, float y) { return Add(base, Rotate({x * h, -y * h}, degreesCcw)); };
    AddBar(p, at(0, -0.05f), at(0, 1.02f), 0.035f * h, 0.03f * h);
    // 傘: 平らなドームで、縁は 8 本の骨の間でふちどりのように波打つ。
    const Vec2 top = at(0, 1.0f);
    const float R = 0.62f, rise = 0.32f;
    for (int i = 0; i < 8; ++i) {
        std::vector<Vec2> wedge{top};
        for (int k = 0; k <= 4; ++k) {
            const float t = (static_cast<float>(i) + static_cast<float>(k) / 4.f) / 8.f;  // 0 .. 1、左から右
            const float ang = kPi * (1.f - t);                                          // 180 .. 0 度
            const float sag = 0.04f * std::sin(static_cast<float>(k) / 4.f * kPi);
            wedge.push_back(at(std::cos(ang) * R, 1.0f - rise + std::sin(ang) * rise * 0.15f - sag));
        }
        AddPolygon(p, wedge);
    }
    AddCircle(p, top, 0.035f * h);
    return p;
}

ui::Path BeachScene(Vec2 base, float h, bool parasol) {
    ui::Path p;
    // 小山: 幅広で低い丘で、足はウィンドウの下端 (半分は下にはみ出す)。
    AddEllipse(p, {base.x, base.y + 0.04f * h}, 0.78f * h, 0.17f * h, 0);
    AddEllipse(p, {base.x + 0.42f * h, base.y + 0.02f * h}, 0.42f * h, 0.12f * h, -4.f);
    // そこに刺さった左に傾くパラソルと、小山の左にある砂のお城。
    if (parasol) {
        ui::Path shade = BeachParasol({base.x + 0.18f * h, base.y - 0.08f * h}, 0.86f * h, 12.f);
        p.steps.insert(p.steps.end(), shade.steps.begin(), shade.steps.end());
    }
    ui::Path castle = SandCastle({base.x - 0.36f * h, base.y - 0.09f * h}, 0.30f * h);
    p.steps.insert(p.steps.end(), castle.steps.begin(), castle.steps.end());
    return p;
}

// --- 秋 -----------------------------------------------------------------------------------

namespace {

Vec2 Bezier(Vec2 a, Vec2 b, Vec2 c, Vec2 d, float t) {
    const float u = 1 - t;
    const float k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
    return {a.x * k0 + b.x * k1 + c.x * k2 + d.x * k3, a.y * k0 + b.y * k1 + c.y * k2 + d.y * k3};
}

// 曲線 (3 次ベジエ a..d) に沿った細長い形: 太さは根元で `width`、先へ向かって細くなり、先はとがる。
// 葉・茎・小穂。`steps` 点ずつ両側に置いて 1 つの多角形にする。
void AddBlade(ui::Path& p, Vec2 a, Vec2 b, Vec2 c, Vec2 d, float width, int steps = 14, float taper = 1.f) {
    std::vector<Vec2> left, right;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const Vec2 q = Bezier(a, b, c, d, t);
        const Vec2 q2 = Bezier(a, b, c, d, std::min(1.f, t + 0.01f));
        const Vec2 q1 = Bezier(a, b, c, d, std::max(0.f, t - 0.01f));
        float dx = q2.x - q1.x, dy = q2.y - q1.y;
        const float len = std::max(1e-4f, std::sqrt(dx * dx + dy * dy));
        dx /= len, dy /= len;
        const float half = width * 0.5f * std::pow(1 - t, taper);
        left.push_back({q.x - dy * half, q.y + dx * half});
        right.push_back({q.x + dy * half, q.y - dx * half});
    }
    std::vector<Vec2> pts(left.begin(), left.end());
    for (auto it = right.rbegin(); it != right.rend(); ++it) pts.push_back(*it);
    AddPolygon(p, pts);
}

}  // namespace

ui::Path Susuki(Vec2 base, float h, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    auto jitter = [&](float amount) { return (rnd.Next() - 0.5f) * 2.f * amount; };
    // 細長い葉: 根元からほぼまっすぐ、左へ傾いて立ち、上で左へしなる (いくつかは大きく垂れる)。
    for (int i = 0; i < 10; ++i) {
        const float x0 = base.x + jitter(h * 0.10f);
        const float tall = h * (0.38f + 0.40f * rnd.Next());
        const float lean = h * (0.14f + 0.12f * rnd.Next());
        const float arch = rnd.Next();
        const Vec2 a{x0, base.y + 2}, b{x0 - lean * 0.2f, base.y - tall * 0.6f};
        const Vec2 c{x0 - lean * 0.7f - tall * 0.05f, base.y - tall};
        const Vec2 d{x0 - lean - tall * (0.25f + 0.30f * arch), base.y - tall * (0.95f - 0.45f * arch)};
        AddBlade(p, a, b, c, d, h * (0.016f + 0.008f * rnd.Next()), 20, 0.9f);
    }
    // 穂のある茎 (高さを変えて 5 本)。
    for (int s = 0; s < 5; ++s) {
        const float x0 = base.x + jitter(h * 0.03f) + static_cast<float>(s - 2) * h * 0.055f;
        const float tall = h * (s == 2 ? 1.f : 0.62f + 0.30f * rnd.Next());
        const float lean = tall * (0.28f + 0.10f * rnd.Next());
        // 茎: 根元はほぼまっすぐ、左へ傾き、上で左へ曲がる。
        const Vec2 a{x0, base.y + 2}, b{x0 - lean * 0.15f, base.y - tall * 0.55f};
        const Vec2 c{x0 - lean * 0.55f, base.y - tall * 0.9f}, top{x0 - lean - tall * 0.08f, base.y - tall};
        AddBlade(p, a, b, c, top, h * 0.009f, 16, 0.3f);
        // 穂の軸: 茎の先から左へ出て、すぐに下へ垂れる。
        const float plume = tall * (0.34f + 0.08f * rnd.Next()) * 1.5f;  // (2026-11-25: 穂を 1.5 倍に)
        const Vec2 r0 = top, r1{top.x - plume * 0.30f, top.y + plume * 0.02f};
        const Vec2 r2{top.x - plume * 0.52f, top.y + plume * 0.30f}, r3{top.x - plume * 0.60f, top.y + plume * 0.80f};
        AddBlade(p, r0, r1, r2, r3, h * 0.008f, 14, 0.7f);
        // 小穂: 軸のあちこちから、軸に沿って少し広がり (左右交互)、重さで下へ垂れる細い房。上ほど長い。
        constexpr int kCount = 30;
        for (int k = 0; k < kCount; ++k) {
            const float t = 0.04f + 0.92f * static_cast<float>(k) / static_cast<float>(kCount - 1);
            const Vec2 q = Bezier(r0, r1, r2, r3, t);
            const Vec2 qn = Bezier(r0, r1, r2, r3, std::min(1.f, t + 0.02f));
            float dx = qn.x - q.x, dy = qn.y - q.y;
            const float len = std::max(1e-4f, std::sqrt(dx * dx + dy * dy));
            dx /= len, dy /= len;
            const float side = k % 2 == 0 ? 1.f : -1.f;
            const float branch = plume * (0.60f - 0.35f * t) * (0.75f + 0.5f * rnd.Next());
            const float fan = side * (0.12f + 0.28f * rnd.Next());
            const float tx = dx * std::cos(fan) - dy * std::sin(fan), ty = dx * std::sin(fan) + dy * std::cos(fan);
            float vx = tx * 0.55f - 0.10f, vy = ty * 0.55f + 0.85f;  // 下へ垂れる向き
            const float vl = std::max(1e-4f, std::sqrt(vx * vx + vy * vy));
            vx /= vl, vy /= vl;
            const Vec2 e1{q.x + tx * branch * 0.30f, q.y + ty * branch * 0.30f};
            const Vec2 e2{q.x + (tx * 0.4f + vx * 0.6f) * branch * 0.65f, q.y + (ty * 0.4f + vy * 0.6f) * branch * 0.65f};
            const Vec2 e3{q.x + vx * branch - side * branch * 0.06f, q.y + vy * branch};
            AddBlade(p, q, e1, e2, e3, h * (0.0065f + 0.004f * rnd.Next()), 12, 0.7f);
        }
    }
    return p;
}

ui::Path MapleLeaf(Vec2 center, float r, float degreesCcw, int points) {
    // とがった 5 つの葉先 (真上に 1 つ、斜め上へ外に 2 つ、斜め下へ外に 2 つ)、縁に小さなぎざぎざ、
    // その間に深い切れ込み。葉柄は下に垂れる。
    struct Lobe {
        float angle, length, halfWidth;  // 画面上の度 (270 = 上)
    };
    static constexpr Lobe kLobes[] = {{270, 1.0f, 36}, {208, 0.86f, 32}, {332, 0.86f, 32}, {150, 0.56f, 28}, {30, 0.56f, 28}};
    std::vector<Vec2> outline;
    points = std::max(points, 24);
    for (int i = 0; i < points; ++i) {
        const float a = 360.f * static_cast<float>(i) / static_cast<float>(points);
        float rad = 0.30f;  // 葉の真ん中
        for (const Lobe& l : kLobes) {
            float d = std::fabs(a - l.angle);
            d = std::min(d, 360.f - d);
            if (d >= l.halfWidth) continue;
            const float t = 1.f - d / l.halfWidth;  // 先端で 1
            const float teeth = 1.f + 0.07f * std::fabs(std::sin(d * kDeg * 22.f)) * (1.f - t);
            rad = std::max(rad, (0.30f + (l.length - 0.30f) * std::pow(t, 1.25f)) * teeth);
        }
        outline.push_back(Add(center, Rotate({std::cos(a * kDeg) * rad * r, std::sin(a * kDeg) * rad * r}, degreesCcw)));
    }
    ui::Path p;
    AddPolygon(p, outline);
    AddBar(p, Add(center, Rotate({0, 0.1f * r}, degreesCcw)), Add(center, Rotate({0.05f * r, 0.62f * r}, degreesCcw)), 0.06f * r,
           0.035f * r);
    return p;
}

namespace {

// 桜の花びら 1 枚 (長さ約 size): 付け根が細く、先がふくらみ、先の真ん中に小さな切れ込み (2026-11-25: 前はだ円)。
// 回す前は付け根が上、先が下。
void AddPetal(ui::Path& p, Vec2 c, float size, float deg) {
    // 長さを 1 にした形 (y: 付け根 -0.5 .. 先 0.5)。右側を下り、切れ込み、左側を上る。
    static constexpr Vec2 kShape[] = {{0, -0.5f},     {0.12f, -0.38f}, {0.22f, -0.2f}, {0.29f, 0},      {0.31f, 0.18f},
                                      {0.27f, 0.34f}, {0.17f, 0.46f},  {0.07f, 0.5f},  {0, 0.4f},       {-0.07f, 0.5f},
                                      {-0.17f, 0.46f}, {-0.27f, 0.34f}, {-0.31f, 0.18f}, {-0.29f, 0},   {-0.22f, -0.2f},
                                      {-0.12f, -0.38f}};
    std::vector<Vec2> pts;
    for (const Vec2 v : kShape) pts.push_back(Add(c, Rotate({v.x * size, v.y * size}, deg)));
    AddPolygon(p, pts);
}

// 上の端に沿って濃く、下に向かって薄く散らす (いくつかは `depth` までずっと流れ落ちる)。1 つずつ、場所・
// 大きさ・4 色のどれか・向き (度) で `shape` が形を作る。秋の葉も春の花びらもこれ (同じ散らばり)。
template <class Shape>
std::array<ui::Path, 4> AlongTop(const ui::Rect& win, float depth, float perPixel, uint32_t seed, float least, float more, Shape shape) {
    std::array<ui::Path, 4> out;
    Random rnd{seed};
    const int count = std::max(12, static_cast<int>(win.w * perPixel));
    for (int i = 0; i < count; ++i) {
        const float x = win.x - 10.f + (win.w + 20.f) * rnd.Next();
        const float v = rnd.Next();
        const float y = win.y - 6.f + depth * std::pow(v, 2.2f);
        const float size = (least + more * rnd.Next()) * (1.f - 0.35f * v);
        const int color = static_cast<int>(rnd.Next() * 4.f) % 4;
        const ui::Path one = shape(Vec2{x, y}, size, rnd.Next() * 360.f);
        auto& dst = out[static_cast<size_t>(color)].steps;
        dst.insert(dst.end(), one.steps.begin(), one.steps.end());
    }
    return out;
}

}  // namespace

std::array<ui::Path, 4> Canopy(const ui::Rect& win, float depth, float perPixel, uint32_t seed) {
    return AlongTop(win, depth, perPixel, seed, 7.f, 9.f, [](Vec2 c, float size, float deg) { return MapleLeaf(c, size, deg, 40); });
}

std::array<ui::Path, 4> PetalCanopy(const ui::Rect& win, float depth, float perPixel, uint32_t seed) {
    // 花びら 1 枚 (長さ約 size): 付け根が細く、先がふくらみ、先の真ん中に小さな切れ込みのある桜の花びら
    // (2026-11-25: 前はだ円)。
    return AlongTop(win, depth, perPixel, seed, 6.f, 7.f, [](Vec2 c, float size, float deg) {
        ui::Path p;
        AddPetal(p, c, size, deg);
        return p;
    });
}

// --- 冬 -----------------------------------------------------------------------------------

ui::Path Snowflake(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    for (int i = 0; i < 6; ++i) {
        const float a = degreesCcw + 60.f * static_cast<float>(i);
        auto at = [&](float x, float y) { return Add(center, Scale(Rotate({x, y}, a), r)); };
        // 腕: 中心から太く、先で長く鋭い槍先にふくらむ。
        const Vec2 arm[] = {at(-0.085f, 0), at(-0.075f, -0.62f), at(-0.11f, -0.72f), at(0, -1.f),
                            at(0.11f, -0.72f), at(0.075f, -0.62f), at(0.085f, 0)};
        AddPolygon(p, arm);
        // 各腕に側枝が 2 対。腕の所は太く、先は鋭い。
        for (const auto& [along, len, wd] : {std::tuple{0.30f, 0.30f, 0.075f}, std::tuple{0.56f, 0.24f, 0.06f}}) {
            AddBar(p, at(0, -along), at(-len * 0.80f, -along - len * 0.62f), 2.f * wd * r, 0);
            AddBar(p, at(0, -along), at(len * 0.80f, -along - len * 0.62f), 2.f * wd * r, 0);
        }
    }
    // 中心: 六角形。
    std::vector<Vec2> hex;
    for (int i = 0; i < 6; ++i) hex.push_back(Add(center, Scale(Rotate({0, -0.24f}, degreesCcw + 60.f * static_cast<float>(i) + 30.f), r)));
    AddPolygon(p, hex);
    return p;
}

ui::Path Snowfall(const ui::Rect& area, int count, float size, uint32_t seed) { return ui::shape::Dots(area, count, size, seed); }

float SnowDriftHeight(float l, float depth) {
    return depth * (0.72f + 0.17f * std::sin(l * 0.017f) + 0.09f * std::sin(l * 0.039f + 1.f));
}

SnowmanShapes Snowman(Vec2 base, float height, float grow) {
    SnowmanShapes s;
    // 高さの割り振り: 下の丸 0.46、頭 0.32 (少し重なる)、バケツ 0.20 ほど。
    const float r1 = height * 0.23f, r2 = height * 0.16f;
    const Vec2 c1{base.x, base.y - r1};
    const Vec2 c2{base.x + height * 0.01f, c1.y - r1 - r2 + height * 0.05f};
    AddCircle(s.body, c1, r1 + grow);
    AddCircle(s.body, c2, r2 + grow);
    // バケツ: 逆さにかぶる (口が下で広い)。頭の上の方から、少し右へ傾ける。
    const float tilt = -12.f;  // 度 (画面で時計回り = 右へ傾く)
    auto at = [&](float x, float y) { return Add(c2, Rotate({x, y}, tilt)); };
    const float y0 = -r2 * 0.55f, y1 = y0 - height * 0.20f;
    const Vec2 bucket[] = {at(-r2 * 0.80f, y0), at(-r2 * 0.58f, y1), at(r2 * 0.58f, y1), at(r2 * 0.80f, y0)};
    AddPolygon(s.bucket, bucket);
    const Vec2 rim[] = {at(-r2 * 0.95f, y0 + height * 0.035f), at(-r2 * 0.95f, y0 - height * 0.015f), at(r2 * 0.95f, y0 - height * 0.015f),
                        at(r2 * 0.95f, y0 + height * 0.035f)};
    AddPolygon(s.rim, rim);
    return s;
}

ui::Path SnowDrift(const ui::Rect& r, float depth) {
    ui::Path p;
    if (r.w <= 0 || depth <= 0) return p;
    const float bottom = r.y + r.h;
    // 高さは左端からのピクセル `l` だけで決まる (ウィンドウの幅・高さに関係しない)。なだらかに
    // (2026-11-24: ユーザーの希望。前は 50 ピクセルほどの細かい波があった): 長い波 2 つだけ。0.46 .. 0.98 倍。
    auto top = [&](float l) { return Vec2{r.x + l, bottom - SnowDriftHeight(l, depth)}; };
    // 画面上で時計回り: 吹きだまりの上を右へ、そのあと下を戻る。点は左端から 4 ピクセルごとの決まった所だけに
    // 置き、両端は 1 区切り越える (2026-11-25: 端まで積もる。前は両端で下がっていた): 見える線はいつも同じ点の
    // 間なので、幅を変えても形は変わらない。
    p.Begin(top(-4.f));
    for (int i = 0;; ++i) {
        const float l = 4.f * static_cast<float>(i);
        p.Line(top(l));
        if (l >= r.w) {
            p.Line({r.x + l, bottom});
            break;
        }
    }
    p.Line({r.x - 4.f, bottom});
    return p;
}

IcicleShapes Icicles(const ui::Rect& win, float maxLength, uint32_t seed) {
    IcicleShapes out;
    Random rnd{seed};
    const float top = win.y, right = win.x + win.w;
    // 左端から 260 .. 640 ピクセルごとに小さな 1 組。ウィンドウの内側で始まる組はすべて作る
    // (端を越える組は描く側が切る)。だから出たり消えたりしない。
    for (float gx = win.x + 60.f + 160.f * rnd.Next(); gx < right; gx += 260.f + 380.f * rnd.Next()) {
        const int count = 1 + static_cast<int>(rnd.Next() * 3.f) % 3;
        const float groupW = 6.f + 10.f * static_cast<float>(count);
        // つららがぶら下がる雪の塊と、その上のもっと小さい塊。
        AddEllipse(out.snow, {gx + groupW * 0.5f - 4.f, top + 1.f}, groupW * 0.5f + 5.f, 5.f, 0);
        AddEllipse(out.snow, {gx + groupW * 0.35f - 4.f, top + 3.5f}, groupW * 0.22f + 3.f, 3.f, 0);
        for (int i = 0; i < count; ++i) {
            const float x = gx + 10.f * static_cast<float>(i) + 3.f * rnd.Next();
            const float len = maxLength * (i == count / 2 ? 0.7f + 0.3f * rnd.Next() : 0.3f + 0.35f * rnd.Next());
            const float half = 3.2f + 3.0f * rnd.Next() * (len / maxLength + 0.3f);
            const float lean = (rnd.Next() - 0.5f) * 3.f;  // 先が少し片側に寄る
            const float phase = rnd.Next() * 6.28f;
            const float y0 = top + 2.f;
            // 輪郭: 右側を下り左側を上る。幅は鋭い先に向かって細くなり、
            // 小さな波 (水が輪になって凍った所) があり、全体が `lean` のほうへ曲がる。
            std::vector<Vec2> pts;
            constexpr int kSteps = 14;
            auto at = [&](float t, float side) {
                const float width = half * std::pow(1.f - t, 0.85f) * (1.f + 0.13f * std::sin(t * 17.f + phase) * (1.f - t));
                return Vec2{x + lean * t * t + side * width, y0 + len * t};
            };
            for (int k = 0; k <= kSteps; ++k) pts.push_back(at(static_cast<float>(k) / kSteps, 1.f));
            for (int k = kSteps - 1; k >= 0; --k) pts.push_back(at(static_cast<float>(k) / kSteps, -1.f));
            AddPolygon(out.ice, pts);
            // つや: 左側に沿った細い明るい筋で、先の手前で消える。
            std::vector<Vec2> shine;
            for (int k = 1; k <= 9; ++k) {
                const float t = static_cast<float>(k) / 14.f;
                const Vec2 edge = at(t, -1.f);
                shine.push_back({edge.x + half * 0.25f, edge.y});
            }
            for (int k = 9; k >= 1; --k) {
                const float t = static_cast<float>(k) / 14.f;
                const Vec2 edge = at(t, -1.f);
                shine.push_back({edge.x + half * (0.25f + 0.35f * (1.f - t)), edge.y});
            }
            AddPolygon(out.shine, shine);
        }
    }
    return out;
}

// --- 夜 -----------------------------------------------------------------------------------

ui::Path HanamiDango(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    auto at = [&](float x, float y) { return Add(center, Rotate({x * r, y * r}, degreesCcw)); };
    // 串の上の端に寄せた団子 3 つ (2026-11-26: 前は串の真ん中。下は持つ所)。
    AddBar(p, at(0, 1.f), at(0, -0.99f), 0.07f * r, 0.05f * r);
    for (int i = 0; i < 3; ++i) AddCircle(p, at(0, -0.71f + 0.43f * static_cast<float>(i)), 0.24f * r);
    return p;
}

GarlandShapes Garland(const ui::Rect& win, float swing, float sag, int perSwing, float width) {
    GarlandShapes g;
    swing = std::max(swing, 20.f);
    perSwing = std::max(perSwing, 0);
    const float top = win.y + 1.f;
    for (float x0 = win.x; x0 < win.Right(); x0 += swing) {
        auto at = [&](float t) { return Vec2{x0 + swing * t, top + sag * 4.f * t * (1.f - t)}; };
        constexpr int kSteps = 12;
        for (int k = 0; k < kSteps; ++k) {
            AddBar(g.cord, at(static_cast<float>(k) / kSteps), at(static_cast<float>(k + 1) / kSteps), width, width);
        }
        for (int k = 0; k < perSwing; ++k) g.points.push_back(at((static_cast<float>(k) + 0.5f) / static_cast<float>(perSwing)));
    }
    return g;
}

void AddLantern(ui::Path& body, ui::Path& caps, Vec2 hang, float h) {
    const float w = 0.62f * h;
    const Vec2 c{hang.x, hang.y + 0.5f * h};
    AddEllipse(body, c, 0.5f * w, 0.40f * h, 0);
    const ui::Rect top{hang.x - 0.24f * w, hang.y, 0.48f * w, 0.12f * h};
    const ui::Rect bottom{hang.x - 0.24f * w, hang.y + 0.88f * h, 0.48f * w, 0.12f * h};
    for (const ui::Rect& r : {top, bottom}) {
        const Vec2 q[4] = {{r.x, r.y}, {r.Right(), r.y}, {r.Right(), r.Bottom()}, {r.x, r.Bottom()}};
        AddPolygon(caps, q);
    }
}

ui::Path TsukimiDango(Vec2 center, float r) {
    ui::Path p;
    const float d = 0.19f * r;  // 団子の半径
    // 上の団子のてっぺん = center.y - r。段は 1 つ上がるごとに 1.6 d。
    const float row0 = center.y - r + d + 2.f * 1.6f * d;  // 一番下の段の中心
    for (int row = 0; row < 3; ++row) {
        const int n = 3 - row;
        for (int i = 0; i < n; ++i) {
            const float x = center.x + (static_cast<float>(i) - 0.5f * static_cast<float>(n - 1)) * 2.f * d;
            AddCircle(p, {x, row0 - 1.6f * d * static_cast<float>(row)}, d);
        }
    }
    // 三方: 団子の下の、少し広い盆と、下へ細くなる台。
    const float tray = row0 + 0.9f * d;
    const Vec2 board[4] = {{center.x - 3.9f * d, tray}, {center.x + 3.9f * d, tray}, {center.x + 3.6f * d, tray + 0.55f * d},
                           {center.x - 3.6f * d, tray + 0.55f * d}};
    AddPolygon(p, board);
    const Vec2 stand[4] = {{center.x - 2.4f * d, tray + 0.5f * d}, {center.x + 2.4f * d, tray + 0.5f * d}, {center.x + 3.0f * d, tray + 2.6f * d},
                           {center.x - 3.0f * d, tray + 2.6f * d}};
    AddPolygon(p, stand);
    return p;
}

ui::Path ChristmasTree(Vec2 center, float r, float degreesCcw) {
    ui::Path p;
    auto at = [&](float x, float y) { return Add(center, Rotate({x * r, y * r}, degreesCcw)); };
    // 3 段: 上ほど小さく、下の段に少し重なる。
    struct Tier {
        float top, bottom, half;
    };
    for (const Tier& t : {Tier{-0.78f, -0.22f, 0.36f}, Tier{-0.48f, 0.18f, 0.52f}, Tier{-0.16f, 0.62f, 0.70f}}) {
        const Vec2 tri[3] = {at(0, t.top), at(t.half, t.bottom), at(-t.half, t.bottom)};
        AddPolygon(p, tri);
    }
    const Vec2 trunk[4] = {at(-0.11f, 0.60f), at(0.11f, 0.60f), at(0.11f, 0.86f), at(-0.11f, 0.86f)};
    AddPolygon(p, trunk);
    ui::shape::AddStar(p, at(0, -0.84f), 5, 0.16f * r, 0.07f * r, degreesCcw);
    return p;
}


// --- 2026-11-26 の 2 回目: 風に吹かれる花びら・花火 (リアル)・かまくら・サンタ・左下の影の案 --------------------

namespace {

// 点の並び `pts` に沿った帯: 各点での太さ `widths` (0 で先がとがる)。1 つの多角形。
void AddStroke(ui::Path& p, std::span<const Vec2> pts, std::span<const float> widths) {
    if (pts.size() < 2 || widths.size() != pts.size()) return;
    std::vector<Vec2> left, right;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2 a = pts[i > 0 ? i - 1 : 0], b = pts[i + 1 < pts.size() ? i + 1 : i];
        const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(std::hypot(dx, dy), 1e-4f);
        const Vec2 n{-dy / len * widths[i] * 0.5f, dx / len * widths[i] * 0.5f};
        left.push_back(Add(pts[i], n));
        right.push_back(Add(pts[i], Scale(n, -1.f)));
    }
    left.insert(left.end(), right.rbegin(), right.rend());
    AddPolygon(p, left);
}

// 半径 1 の向きで作った点を、`center` に `radius` 倍・`degreesCcw` 回して置く。
struct Place {
    Vec2 center;
    float radius, degrees;
    Vec2 operator()(float x, float y) const { return Add(center, Rotate({x * radius, y * radius}, degrees)); }
};

// 点を置いた多角形 (半径 1 の座標で)。
void AddShape(ui::Path& p, const Place& at, std::initializer_list<Vec2> pts) {
    std::vector<Vec2> out;
    for (const Vec2 v : pts) out.push_back(at(v.x, v.y));
    AddPolygon(p, out);
}

}  // namespace

PetalWindShapes PetalWind(const ui::Rect& win, float depth, float perPixel, float wind, uint32_t seed) {
    PetalWindShapes out;
    Random rnd{seed};
    const float dir = wind >= 0 ? 1.f : -1.f;
    const float startX = dir > 0 ? win.x - 30.f : win.x + win.w + 30.f;
    constexpr int kGusts = 7;
    const int total = std::max(14, static_cast<int>(win.w * perPixel));
    for (int g = 0; g < kGusts; ++g) {
        const float u = (static_cast<float>(g) + 0.3f + 0.4f * rnd.Next()) / kGusts;  // 0 = 上 .. 1 = 下
        const float y0 = win.y - 10.f + depth * 0.75f * std::pow(u, 1.3f);
        const float len = win.w * (0.75f + 0.45f * rnd.Next());
        const float drop = depth * (0.12f + 0.25f * rnd.Next());
        const float amp = 14.f + 26.f * rnd.Next();
        const float phase = rnd.Next() * 2.f * kPi;
        // 風の道すじ: 風上の端から風下へ、少しずつ下がり、うねる。
        auto at = [&](float s) { return Vec2{startX + dir * len * s, y0 + drop * s * s + amp * std::sin(2.f * kPi * 1.1f * s + phase) * s}; };
        auto along = [&](float s) {
            const Vec2 a = at(s), b = at(s + 0.01f);
            return Vec2{b.x - a.x, b.y - a.y};
        };
        const int n = total / kGusts;
        for (int i = 0; i < n; ++i) {
            const float s = std::pow(rnd.Next(), 1.5f);  // 風上ほど多い
            const Vec2 c0 = at(s);
            const float spread = 8.f + 60.f * s;  // 風下ほど広がる
            const Vec2 c{c0.x + (rnd.Next() - 0.5f) * spread * 0.6f, c0.y + (rnd.Next() - 0.5f) * spread};
            if (c.y > win.y + win.h + 10.f) continue;
            const float size = (6.f + 7.f * rnd.Next()) * (1.f - 0.25f * s);
            const Vec2 t = along(s);
            // 花びらの長い向きを風の向きにそろえ、少しだけ回る。
            const float deg = std::atan2(t.x, t.y) / kDeg + (rnd.Next() - 0.5f) * 70.f;
            AddPetal(out.petals[static_cast<size_t>(rnd.Next() * 4.f) % 4], c, size, deg);
        }
        // 風の筋: 道すじに沿った細い線 2 本 (両端がとがる)。
        for (int k = 0; k < 2; ++k) {
            const float s0 = 0.05f + 0.6f * rnd.Next(), s1 = s0 + 0.12f + 0.12f * rnd.Next();
            const float off = (rnd.Next() - 0.5f) * 30.f;
            constexpr int kSteps = 16;
            std::vector<Vec2> pts;
            std::vector<float> widths;
            for (int j = 0; j <= kSteps; ++j) {
                const float f = static_cast<float>(j) / kSteps;
                const Vec2 q = at(s0 + (s1 - s0) * f);
                pts.push_back({q.x, q.y + off});
                widths.push_back(1.6f * std::sin(kPi * f));
            }
            AddStroke(out.streaks, pts, widths);
        }
    }
    return out;
}

FireworkShapes FireworkShell(Vec2 c, float R, FireworkKind kind, uint32_t seed) {
    FireworkShapes f;
    Random rnd{seed};
    AddCircle(f.glow[0], c, 0.95f * R);
    AddCircle(f.glow[1], c, 0.55f * R);
    auto trail = [&](float angle, float len, float inner, float droop, float width, int steps) {
        std::vector<Vec2> pts;
        std::vector<float> widths;
        const Vec2 d = Rotate({0, -1}, angle);
        for (int j = 0; j <= steps; ++j) {
            const float s = static_cast<float>(j) / static_cast<float>(steps);
            const float r = len * (inner + (1.f - inner) * s);
            pts.push_back({c.x + d.x * r, c.y + d.y * r + droop * s * s});
            widths.push_back(width * std::pow(s, 0.7f));  // 中心の近くは細く消え、外ほど太く明るい
        }
        AddStroke(f.trails, pts, widths);
        return pts.back();
    };
    switch (kind) {
        case FireworkKind::Chrysanthemum: {
            // 菊: 中心から放射に広がる光の筋。少し重さで垂れ、先に明るい星。縁のまわりにきらめく粒。
            constexpr int kRays = 72;
            for (int i = 0; i < kRays; ++i) {
                const float a = 360.f * (static_cast<float>(i) + 0.5f * (rnd.Next() - 0.5f)) / kRays;
                const Vec2 tip = trail(a, R * (0.86f + 0.14f * rnd.Next()), 0.18f, 0.10f * R, R * 0.012f + 0.7f, 10);
                AddCircle(f.tips, tip, R * 0.012f + 1.1f);
            }
            for (int i = 0; i < 40; ++i) {
                const float a = rnd.Next() * 360.f, r = R * (0.75f + 0.35f * rnd.Next());
                const Vec2 q = Add(c, Rotate({0, -r}, a));
                AddCircle(f.tips, {q.x, q.y + 0.1f * R}, R * 0.005f + 0.6f);
            }
            break;
        }
        case FireworkKind::Willow: {
            // 柳: 金色の長い筋が、大きく垂れて流れ落ちる。先に星は無く、筋の途中にこまかい粒。
            constexpr int kRays = 46;
            for (int i = 0; i < kRays; ++i) {
                const float a = 360.f * (static_cast<float>(i) + 0.4f * (rnd.Next() - 0.5f)) / kRays;
                const float len = R * (0.75f + 0.25f * rnd.Next());
                trail(a, len, 0.08f, 0.6f * R, R * 0.007f + 0.6f, 14);
                const Vec2 d = Rotate({0, -1}, a);
                for (int k = 0; k < 3; ++k) {
                    const float s = 0.5f + 0.5f * rnd.Next();
                    const float r = len * (0.08f + 0.92f * s);
                    AddCircle(f.tips, {c.x + d.x * r, c.y + d.y * r + 0.6f * R * s * s}, R * 0.004f + 0.5f);
                }
            }
            break;
        }
        case FireworkKind::Peony: {
            // 牡丹: 筋の無い光の粒が 2 重の球に。粒のうしろに短い残りの粒。
            for (const auto& [count, reach, size] : {std::tuple{80, 1.f, 0.016f}, std::tuple{44, 0.55f, 0.012f}}) {
                for (int i = 0; i < count; ++i) {
                    const float a = 360.f * (static_cast<float>(i) + 0.5f * rnd.Next()) / static_cast<float>(count);
                    const float r = R * reach * (0.9f + 0.1f * rnd.Next());
                    const Vec2 d = Rotate({0, -1}, a);
                    AddCircle(f.tips, {c.x + d.x * r, c.y + d.y * r + 0.06f * R}, R * size + 0.9f);
                    for (int k = 1; k <= 2; ++k) {
                        const float back = r * (1.f - 0.07f * static_cast<float>(k));
                        AddCircle(f.trails, {c.x + d.x * back, c.y + d.y * back + 0.05f * R}, (R * size + 0.9f) * (1.f - 0.3f * static_cast<float>(k)));
                    }
                }
            }
            break;
        }
    }
    return f;
}

KamakuraShapes Kamakura(Vec2 base, float h, float grow) {
    KamakuraShapes k;
    // 雪のドーム: 底の幅 1.6 h の、少し平たい半円。入り口は手前の左寄りに、アーチ形。中に灯り。
    const float rx = 0.8f * h + grow, ry = (h + grow) / 0.92f;  // (下の 0.92: てっぺんが少し平たい。高さはちょうど h)
    std::vector<Vec2> dome;
    constexpr int kSteps = 40;
    for (int i = 0; i <= kSteps; ++i) {
        const float a = kPi + kPi * static_cast<float>(i) / kSteps;  // 左 → 上 → 右
        dome.push_back({base.x + rx * std::cos(a), base.y + ry * std::sin(a) * (1.f - 0.08f * std::sin(a) * std::sin(a))});
    }
    AddPolygon(k.dome, dome);
    auto arch = [&](ui::Path& p, float cx, float w, float top) {
        std::vector<Vec2> pts{{cx - w, base.y + 2.f}};
        for (int i = 0; i <= 16; ++i) {
            const float a = kPi + kPi * static_cast<float>(i) / 16.f;
            pts.push_back({cx + w * std::cos(a), base.y - top + w + w * std::sin(a)});
        }
        pts.push_back({cx + w, base.y + 2.f});
        AddPolygon(p, pts);
    };
    arch(k.door, base.x - 0.18f * h, 0.27f * h, 0.52f * h);
    arch(k.light, base.x - 0.18f * h, 0.19f * h, 0.42f * h);
    return k;
}

ui::Path SantaSleigh(Vec2 center, float size, float degreesCcw) {
    ui::Path p;
    const Place at{center, size, degreesCcw};
    // 左へ飛ぶトナカイ 2 頭 (前足を前へ、後ろ足を後ろへ伸ばして駆ける)。
    for (const float x : {-0.78f, -0.30f}) {
        auto q = [&](float u, float v) { return at(x + u, v); };
        AddEllipse(p, q(0, 0), 0.20f * size, 0.075f * size, degreesCcw);                     // 胴
        AddBar(p, q(-0.15f, -0.02f), q(-0.25f, -0.13f), 0.065f * size, 0.05f * size);      // 首
        AddEllipse(p, q(-0.285f, -0.155f), 0.06f * size, 0.036f * size, degreesCcw + 20.f);  // 頭
        AddBar(p, q(-0.32f, -0.15f), q(-0.355f, -0.13f), 0.03f * size, 0.02f * size);       // 鼻
        // 角: 後ろ上へ枝分かれ。
        AddBar(p, q(-0.27f, -0.18f), q(-0.22f, -0.30f), 0.016f * size, 0.01f * size);
        AddBar(p, q(-0.245f, -0.24f), q(-0.29f, -0.30f), 0.012f * size, 0.008f * size);
        AddBar(p, q(-0.23f, -0.27f), q(-0.17f, -0.31f), 0.012f * size, 0.008f * size);
        // 足: 前 2 本と後ろ 2 本。
        AddBar(p, q(-0.12f, 0.04f), q(-0.25f, 0.10f), 0.035f * size, 0.025f * size);
        AddBar(p, q(-0.25f, 0.10f), q(-0.31f, 0.07f), 0.025f * size, 0.018f * size);
        AddBar(p, q(-0.08f, 0.05f), q(-0.18f, 0.13f), 0.03f * size, 0.022f * size);
        AddBar(p, q(0.12f, 0.04f), q(0.26f, 0.11f), 0.04f * size, 0.025f * size);
        AddBar(p, q(0.26f, 0.11f), q(0.33f, 0.09f), 0.025f * size, 0.018f * size);
        AddBar(p, q(0.08f, 0.05f), q(0.20f, 0.14f), 0.03f * size, 0.022f * size);
        AddBar(p, q(0.19f, -0.03f), q(0.24f, -0.06f), 0.03f * size, 0.01f * size);  // しっぽ
    }
    // 手綱: トナカイからそりへ。
    AddBar(p, at(-0.62f, -0.02f), at(-0.46f, 0.0f), 0.012f * size, 0.012f * size);
    AddBar(p, at(-0.14f, -0.02f), at(0.22f, 0.02f), 0.012f * size, 0.012f * size);
    // そり: 前が巻き上がった滑り木と、箱。
    AddShape(p, at, {{0.20f, 0.13f}, {0.24f, 0.06f}, {0.28f, 0.10f}, {0.30f, 0.16f}, {0.86f, 0.16f}, {0.90f, 0.12f}, {0.92f, 0.13f}, {0.88f, 0.19f},
                     {0.28f, 0.19f}, {0.22f, 0.17f}});
    AddShape(p, at, {{0.32f, 0.13f}, {0.30f, -0.02f}, {0.36f, -0.06f}, {0.50f, 0.0f}, {0.80f, -0.03f}, {0.86f, 0.13f}});
    // サンタ: 体と頭、とがった帽子、後ろに大きな袋。
    AddCircle(p, at(0.56f, -0.07f), 0.10f * size);
    AddCircle(p, at(0.53f, -0.20f), 0.055f * size);
    AddShape(p, at, {{0.48f, -0.22f}, {0.58f, -0.24f}, {0.66f, -0.30f}, {0.70f, -0.27f}, {0.60f, -0.20f}});
    AddCircle(p, at(0.705f, -0.28f), 0.022f * size);
    AddEllipse(p, at(0.76f, -0.08f), 0.12f * size, 0.11f * size, degreesCcw);
    return p;
}

// --- 左下の影の案 (2026-11-26: 見本にして選んでもらう) ---------------------------------------

ui::Path Bonbori(Vec2 center, float radius, float degreesCcw) {
    ui::Path p;
    const Place at{center, radius, degreesCcw};
    AddShape(p, at, {{-0.30f, -0.80f}, {-0.22f, -0.92f}, {0, -1.f}, {0.22f, -0.92f}, {0.30f, -0.80f}});         // 屋根
    AddShape(p, at, {{-0.34f, -0.81f}, {0.34f, -0.81f}, {0.40f, -0.62f}, {0.36f, -0.20f}, {0.24f, -0.10f}, {-0.24f, -0.10f},
                     {-0.36f, -0.20f}, {-0.40f, -0.62f}});                                                            // 紙のほや
    AddBar(p, at(0, -0.12f), at(0, 0.80f), 0.08f * radius, 0.08f * radius);                                          // 柱
    AddShape(p, at, {{-0.12f, 0.74f}, {0.12f, 0.74f}, {0.12f, 0.86f}, {-0.12f, 0.86f}});
    AddShape(p, at, {{-0.38f, 0.86f}, {0.38f, 0.86f}, {0.30f, 1.f}, {-0.30f, 1.f}});                                  // 台
    return p;
}

ui::Path SakuraBranch(Vec2 center, float radius, float degreesCcw) {
    ui::Path p;
    const Place at{center, radius, degreesCcw};
    AddBlade(p, at(-1.f, 0.78f), at(-0.4f, 0.45f), at(0.2f, 0.0f), at(0.9f, -0.62f), 0.13f * radius, 18);  // 太い枝
    AddBlade(p, at(-0.25f, 0.32f), at(-0.25f, 0.0f), at(-0.35f, -0.3f), at(-0.5f, -0.55f), 0.06f * radius, 12);
    AddBlade(p, at(0.35f, -0.12f), at(0.5f, 0.1f), at(0.7f, 0.2f), at(0.95f, 0.18f), 0.05f * radius, 12);
    const Vec2 flowers[] = {{-0.5f, -0.6f}, {-0.2f, -0.2f}, {0.92f, -0.66f}, {0.55f, -0.38f}, {0.97f, 0.16f}, {0.1f, 0.12f}};
    const float sizes[] = {0.24f, 0.2f, 0.22f, 0.2f, 0.2f, 0.17f};
    for (size_t i = 0; i < std::size(flowers); ++i) {
        ui::Path f = Sakura(at(flowers[i].x, flowers[i].y), sizes[i] * radius, degreesCcw + 17.f * static_cast<float>(i));
        p.steps.insert(p.steps.end(), f.steps.begin(), f.steps.end());
    }
    AddCircle(p, at(0.72f, -0.08f), 0.06f * radius);  // つぼみ
    AddCircle(p, at(-0.6f, 0.2f), 0.06f * radius);
    return p;
}

ui::Path TokkuriSakazuki(Vec2 center, float radius) {
    ui::Path p;
    const Place at{center, radius, 0.f};
    // 徳利 (左): 口が少し開き、首が細く、下がふくらむ。
    static constexpr Vec2 kHalf[] = {{0.13f, -1.f}, {0.09f, -0.9f}, {0.08f, -0.66f}, {0.16f, -0.45f}, {0.33f, -0.16f},
                                     {0.40f, 0.14f}, {0.36f, 0.44f}, {0.25f, 0.60f}};
    std::vector<Vec2> bottle;
    for (const Vec2 v : kHalf) bottle.push_back(at(-0.38f + v.x, v.y));
    for (size_t i = std::size(kHalf); i-- > 0;) bottle.push_back(at(-0.38f - kHalf[i].x, kHalf[i].y));
    AddPolygon(p, bottle);
    // 盃 (右): 浅い器と小さな足。上に桜の花が 1 つ浮かぶ。
    AddShape(p, at, {{0.20f, 0.32f}, {0.98f, 0.32f}, {0.92f, 0.42f}, {0.78f, 0.50f}, {0.68f, 0.52f}, {0.70f, 0.60f}, {0.48f, 0.60f},
                     {0.50f, 0.52f}, {0.40f, 0.50f}, {0.26f, 0.42f}});
    ui::Path f = Sakura(at(0.6f, 0.05f), 0.2f * radius, 12.f);
    p.steps.insert(p.steps.end(), f.steps.begin(), f.steps.end());
    return p;
}

ui::Path Goldfish(Vec2 center, float radius, float degreesCcw) {
    ui::Path p;
    const Place at{center, radius, degreesCcw};
    AddEllipse(p, at(-0.25f, 0), 0.48f * radius, 0.33f * radius, degreesCcw);  // 体 (左を向く)
    AddShape(p, at, {{0.12f, -0.06f}, {0.45f, -0.42f}, {0.85f, -0.66f}, {0.72f, -0.28f}, {0.60f, 0.0f}, {0.72f, 0.30f}, {0.85f, 0.66f},
                     {0.45f, 0.44f}, {0.12f, 0.06f}});                                                    // ひらひらの尾
    AddShape(p, at, {{-0.35f, -0.28f}, {-0.05f, -0.52f}, {0.10f, -0.22f}});                                // 背びれ
    AddShape(p, at, {{-0.30f, 0.22f}, {-0.12f, 0.44f}, {-0.05f, 0.24f}});                                   // 腹びれ
    return p;
}

ui::Path Furin(Vec2 center, float radius) {
    ui::Path p;
    const Place at{center, radius, 0.f};
    AddBar(p, at(0, -1.f), at(0, -0.6f), 0.03f * radius, 0.03f * radius);  // つるす糸
    std::vector<Vec2> bell;
    for (int i = 0; i <= 20; ++i) {
        const float a = kPi + kPi * static_cast<float>(i) / 20.f;
        bell.push_back(at(0.42f * std::cos(a), -0.12f + 0.5f * std::sin(a)));
    }
    bell.push_back(at(0.46f, -0.06f));
    bell.push_back(at(-0.46f, -0.06f));
    AddPolygon(p, bell);                                                     // ガラスの鉢
    AddBar(p, at(0, -0.1f), at(0, 0.3f), 0.025f * radius, 0.025f * radius);  // 舌の糸
    AddShape(p, at, {{-0.13f, 0.30f}, {0.13f, 0.30f}, {0.19f, 0.98f}, {-0.07f, 1.0f}});  // 短冊 (少し揺れる)
    return p;
}

ui::Path Kakigori(Vec2 center, float radius) {
    ui::Path p;
    const Place at{center, radius, 0.f};
    // 氷の山: 丸いふくらみを重ねる。
    AddEllipse(p, at(0, -0.02f), 0.58f * radius, 0.5f * radius, 0);
    AddCircle(p, at(-0.22f, -0.32f), 0.3f * radius);
    AddCircle(p, at(0.2f, -0.36f), 0.3f * radius);
    AddCircle(p, at(0, -0.6f), 0.26f * radius);
    // 足の付いたガラスの器。
    AddShape(p, at, {{-0.66f, 0.05f}, {0.66f, 0.05f}, {0.42f, 0.38f}, {0.10f, 0.46f}, {-0.10f, 0.46f}, {-0.42f, 0.38f}});
    AddBar(p, at(0, 0.44f), at(0, 0.82f), 0.12f * radius, 0.1f * radius);
    AddEllipse(p, at(0, 0.86f), 0.38f * radius, 0.08f * radius, 0);
    // さじ。
    AddBar(p, at(0.25f, -0.4f), at(0.62f, -0.95f), 0.06f * radius, 0.05f * radius);
    AddEllipse(p, at(0.66f, -0.98f), 0.09f * radius, 0.05f * radius, -55.f);
    return p;
}

ui::Path MosquitoCoil(Vec2 center, float radius) {
    ui::Path p;
    // 渦巻き: 外から内へ 3 周半。帯の太さ 0.1。中心から細い煙が立つ。
    constexpr int kSteps = 220;
    constexpr float kTurns = 3.5f;
    std::vector<Vec2> pts;
    std::vector<float> widths;
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const float a = 2.f * kPi * kTurns * t;
        const float r = radius * (0.92f - 0.8f * t);
        pts.push_back({center.x + r * std::cos(a), center.y + 0.62f * r * std::sin(a)});  // (斜めから見て少しつぶれる)
        widths.push_back(0.1f * radius);
    }
    AddStroke(p, pts, widths);
    std::vector<Vec2> smoke;
    std::vector<float> sw;
    const Vec2 from = pts.back();
    for (int i = 0; i <= 20; ++i) {
        const float s = static_cast<float>(i) / 20.f;
        smoke.push_back({from.x + 0.12f * radius * std::sin(s * 2.f * kPi * 1.2f), from.y - 1.f * radius * s});
        sw.push_back(0.04f * radius * (1.f - s));
    }
    AddStroke(p, smoke, sw);
    return p;
}


// --- 夏の夜の影の別案 (2026-11-26 の 3 回目) -----------------------------------------------------

namespace {

// 穴: 反時計回りの楕円。同じパスの時計回りの形の中に置くと、そこだけ塗られない (巻き数が 0 になる)。
void AddHole(ui::Path& p, Vec2 c, float rx, float ry, float degrees) {
    constexpr float k = 0.5522847f;
    auto at = [&](float x, float y) { return Add(c, Rotate({x, y}, degrees)); };
    p.Begin(at(rx, 0));
    p.Cubic(at(rx, -ry * k), at(rx * k, -ry), at(0, -ry));
    p.Cubic(at(-rx * k, -ry), at(-rx, -ry * k), at(-rx, 0));
    p.Cubic(at(-rx, ry * k), at(-rx * k, ry), at(0, ry));
    p.Cubic(at(rx * k, ry), at(rx, ry * k), at(rx, 0));
}

}  // namespace

ui::Path SenkoHanabi(Vec2 center, float radius) {
    ui::Path p;
    const Place at{center, radius, 0.f};
    // こよりの持ち手: 左上から斜めに垂れ、先は少しよじれて細くなる。
    AddBlade(p, at(-0.62f, -1.f), at(-0.4f, -0.72f), at(-0.12f, -0.42f), at(0.04f, -0.2f), 0.05f * radius, 16);
    AddCircle(p, at(0.07f, -0.1f), 0.12f * radius);  // 火の玉
    // 松葉のような火花: 玉から外へ伸びる筋の先が、何本にも細かく枝分かれする。
    Random rnd{0x5E4Bu};
    for (int i = 0; i < 9; ++i) {
        const float a = -100.f + 260.f * static_cast<float>(i) / 8.f + (rnd.Next() - 0.5f) * 14.f;  // 上を除くまわり
        const float len = 0.38f + 0.3f * rnd.Next();
        const Vec2 from = at(0.07f, -0.1f);
        const Vec2 d = Rotate({0, -1}, a);
        const Vec2 mid = Add(from, Scale(d, len * radius));
        AddBar(p, Add(from, Scale(d, 0.1f * radius)), mid, 0.022f * radius, 0.012f * radius);
        for (int k = -2; k <= 2; ++k) {
            const Vec2 e = Rotate(d, 22.f * static_cast<float>(k));
            AddBar(p, mid, Add(mid, Scale(e, (0.12f + 0.06f * rnd.Next()) * radius)), 0.012f * radius, 0.004f * radius);
        }
    }
    return p;
}

ui::Path Ramune(Vec2 center, float radius) {
    ui::Path p;
    const Place at{center, radius, 0.f};
    // 瓶の片側の輪郭 (上から下へ): 口、細い首、ビー玉の入るふくらみ、くびれ、肩、まっすぐな胴、丸い底。
    static constexpr Vec2 kHalf[] = {{0.11f, -1.f},  {0.11f, -0.9f},  {0.08f, -0.85f}, {0.17f, -0.73f}, {0.19f, -0.62f}, {0.15f, -0.52f},
                                     {0.09f, -0.45f}, {0.14f, -0.32f}, {0.27f, -0.12f}, {0.30f, 0.05f},  {0.30f, 0.88f},  {0.27f, 0.97f},
                                     {0.18f, 1.f}};
    std::vector<Vec2> bottle;
    for (const Vec2 v : kHalf) bottle.push_back(at(v.x, v.y));
    for (size_t i = std::size(kHalf); i-- > 0;) bottle.push_back(at(-kHalf[i].x, kHalf[i].y));
    AddPolygon(p, bottle);
    AddHole(p, at(0, -0.62f), 0.1f * radius, 0.1f * radius, 0);              // ビー玉 (透けて見える)
    AddHole(p, at(0, 0.38f), 0.2f * radius, 0.16f * radius, 0);              // 胴の模様 (丸いラベル)
    return p;
}

ui::Path Watermelon(Vec2 center, float radius) {
    ui::Path p;
    // 半月に切ったスイカ (平らな切り口が上)。皮との境目の細い線と、種は穴。
    const Vec2 c{center.x, center.y - 0.25f * radius};
    std::vector<Vec2> half;
    for (int i = 0; i <= 40; ++i) {
        const float a = kPi * static_cast<float>(i) / 40.f;  // 右 → 下 → 左
        half.push_back({c.x + radius * std::cos(a), c.y + radius * std::sin(a)});
    }
    AddPolygon(p, half);
    // 皮との境目: 半径 0.80 .. 0.86 の細い弧 (反時計回りの帯 = 穴)。
    std::vector<Vec2> band;
    for (int i = 0; i <= 40; ++i) {
        const float a = kPi * (0.04f + 0.92f * static_cast<float>(i) / 40.f);
        band.push_back({c.x + 0.86f * radius * std::cos(a), c.y + 0.86f * radius * std::sin(a)});
    }
    for (int i = 40; i >= 0; --i) {
        const float a = kPi * (0.04f + 0.92f * static_cast<float>(i) / 40.f);
        band.push_back({c.x + 0.80f * radius * std::cos(a), c.y + 0.80f * radius * std::sin(a)});
    }
    p.Begin(band.back());
    for (size_t i = band.size() - 1; i-- > 0;) p.Line(band[i]);  // (逆向きに: 穴)
    static constexpr Vec2 kSeeds[] = {{-0.45f, 0.18f}, {-0.15f, 0.12f}, {0.18f, 0.16f}, {0.48f, 0.2f}, {-0.3f, 0.42f}, {0.05f, 0.45f}, {0.33f, 0.4f}};
    for (const Vec2 s : kSeeds) AddHole(p, {c.x + s.x * radius, c.y + s.y * radius}, 0.035f * radius, 0.06f * radius, s.x * -40.f);
    return p;
}

ui::Path WaterYoyo(Vec2 center, float radius) {
    ui::Path p;
    const Place at{center, radius, 0.f};
    AddCircle(p, at(0, 0.36f), 0.62f * radius);                                     // 水の入った風船
    AddShape(p, at, {{-0.08f, -0.24f}, {0.08f, -0.24f}, {0.05f, -0.36f}, {-0.05f, -0.36f}});  // 結び目
    AddBar(p, at(0, -0.34f), at(0.08f, -0.8f), 0.03f * radius, 0.03f * radius);   // ゴムひも
    AddCircle(p, at(0.1f, -0.88f), 0.12f * radius);                                 // 指にかける輪
    AddHole(p, at(0.1f, -0.88f), 0.07f * radius, 0.07f * radius, 0);
    AddHole(p, at(0, 0.18f), 0.5f * radius, 0.045f * radius, -12.f);                // 模様の帯
    AddHole(p, at(0.02f, 0.5f), 0.52f * radius, 0.045f * radius, -12.f);
    return p;
}


ChochinShapes Chochin(Vec2 center, float radius) {
    // 提灯 (2026-11-26 の 3 回目: 夏の夜の左下。ユーザーがくれた絵の形): 上に細い針金の持ち手 (縦長のアーチ)、
    // 少し反った幅の広い上の帯、まるい胴、細い下の帯。胴の真ん中の白い面 (`panel`) は別の形で、少し明るく塗る。
    // 竹の骨は胴と白い面の両方に、細い穴の線として通す。
    ChochinShapes k;
    const Place at{center, radius, 0.f};
    {
        std::vector<Vec2> arch;
        std::vector<float> widths;
        for (int i = 0; i <= 32; ++i) {
            const float t = -0.5f * kPi + kPi * static_cast<float>(i) / 32.f;
            arch.push_back(at(0.27f * std::sin(t), -0.5f - 0.5f * std::cos(t)));
            widths.push_back(0.028f * radius);
        }
        AddStroke(k.body, arch, widths);
    }
    AddShape(k.body, at, {{-0.29f, -0.55f}, {0, -0.575f}, {0.29f, -0.55f}, {0.29f, -0.44f}, {0, -0.425f}, {-0.29f, -0.44f}});  // 上の帯
    AddEllipse(k.body, at(0, 0.05f), 0.56f * radius, 0.52f * radius, 0);                                                 // 胴
    AddShape(k.body, at, {{-0.22f, 0.53f}, {0, 0.545f}, {0.22f, 0.53f}, {0.22f, 0.65f}, {0, 0.665f}, {-0.22f, 0.65f}});    // 下の帯
    // 白い面: 上の帯から下の帯まで、真ん中がふくらむ縦長の形。
    std::vector<Vec2> panel;
    for (int i = 0; i <= 24; ++i) {
        const float u = static_cast<float>(i) / 24.f;  // 0 = 上 .. 1 = 下
        panel.push_back(at(0.13f + 0.15f * std::sin(kPi * u) - 0.04f * u, -0.43f + 0.97f * u));
    }
    for (int i = 24; i >= 0; --i) {
        const float u = static_cast<float>(i) / 24.f;
        panel.push_back(at(-(0.13f + 0.15f * std::sin(kPi * u) - 0.04f * u), -0.43f + 0.97f * u));
    }
    AddPolygon(k.panel, panel);
    // 竹の骨: 胴の幅いっぱいの細い横線 (白い面の上も同じ所)。
    for (int i = 0; i < 11; ++i) {
        const float y = -0.36f + 0.083f * static_cast<float>(i);
        const float d = (y - 0.05f) / 0.52f;
        const float half = 0.56f * std::sqrt(std::max(0.f, 1.f - d * d)) * 0.9f;
        AddHole(k.body, at(0, y), half * radius, 0.009f * radius, 0);
        // 白い面の骨は、その幅の中だけ (外にはみ出した穴は、塗りの巻き数が -1 になって逆に塗られる)。
        const float u = (y + 0.43f) / 0.97f;
        const float panelHalf = (0.13f + 0.15f * std::sin(kPi * u) - 0.04f * u) * 0.92f;
        AddHole(k.panel, at(0, y), panelHalf * radius, 0.009f * radius, 0);
    }
    return k;
}

}  // namespace rm::theme_seasons::ornament
