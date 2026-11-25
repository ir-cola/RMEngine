#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>
#include <vector>

namespace rm::theme_seasons::ornament {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDeg = kPi / 180.f;

Vec2 Rotate(Vec2 v, float degreesCcw) {
    // 画面上で反時計回り (y は下向き): 上 (0, -1) は左へ行く。
    const float a = degreesCcw * kDeg, cs = std::cos(a), sn = std::sin(a);
    return {v.x * cs + v.y * sn, v.y * cs - v.x * sn};
}
Vec2 Add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 Scale(Vec2 a, float s) { return {a.x * s, a.y * s}; }

// 画面上で時計回り (y は下向き) = この座標では面積が正。
float SignedArea(std::span<const Vec2> pts) {
    float a = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2 p = pts[i], q = pts[(i + 1) % pts.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

}  // namespace

float Random::Next() {
    // xorshift32: 小さく速く、同じ種ならどこでも同じ数になる。
    if (state == 0) state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
}

// --- 部品 --------------------------------------------------------------------------

ui::Path FitInto(const ui::Path& path, const ui::Rect& box) {
    if (path.steps.empty()) return path;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    auto grow = [&](Vec2 v) { x0 = std::min(x0, v.x), y0 = std::min(y0, v.y), x1 = std::max(x1, v.x), y1 = std::max(y1, v.y); };
    for (const auto& st : path.steps) {
        grow(st.a);
        if (st.op == ui::Path::Op::Cubic) grow(st.b), grow(st.c);
    }
    const float w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return path;
    const float s = std::min(box.w / w, box.h / h);
    const Vec2 from{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}, to{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
    auto move = [&](Vec2 v) { return Vec2{to.x + (v.x - from.x) * s, to.y + (v.y - from.y) * s}; };
    ui::Path out = path;
    for (auto& st : out.steps) {
        st.a = move(st.a);
        st.b = move(st.b);
        st.c = move(st.c);
    }
    return out;
}

void AddEllipse(ui::Path& p, Vec2 c, float rx, float ry, float degrees) {
    // 3 次曲線 4 本 (おなじみの 0.5523 のハンドル)。画面上で時計回り: 右、下、左、上。
    constexpr float k = 0.5522847f;
    auto at = [&](float x, float y) { return Add(c, Rotate({x, y}, degrees)); };
    p.Begin(at(rx, 0));
    p.Cubic(at(rx, ry * k), at(rx * k, ry), at(0, ry));
    p.Cubic(at(-rx * k, ry), at(-rx, ry * k), at(-rx, 0));
    p.Cubic(at(-rx, -ry * k), at(-rx * k, -ry), at(0, -ry));
    p.Cubic(at(rx * k, -ry), at(rx, -ry * k), at(rx, 0));
}

void AddCircle(ui::Path& p, Vec2 c, float r) { AddEllipse(p, c, r, r, 0); }

void AddPolygon(ui::Path& p, std::span<const Vec2> pts) {
    if (pts.size() < 3) return;
    const bool clockwise = SignedArea(pts) >= 0;
    p.Begin(clockwise ? pts.front() : pts.back());
    for (size_t i = 1; i < pts.size(); ++i) p.Line(clockwise ? pts[i] : pts[pts.size() - 1 - i]);
}

void AddBar(ui::Path& p, Vec2 a, Vec2 b, float wa, float wb) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len <= 0) return;
    const Vec2 n{-dy / len, dx / len};  // 棒を横切る向き
    const Vec2 quad[4] = {Add(a, Scale(n, wa * 0.5f)), Add(b, Scale(n, wb * 0.5f)), Add(b, Scale(n, -wb * 0.5f)),
                          Add(a, Scale(n, -wa * 0.5f))};
    AddPolygon(p, quad);
}

// --- 春 -----------------------------------------------------------------------------------

ui::Path Sakura(Vec2 center, float radius, float degreesCcw) {
    // 上を向いた花びら 1 枚。ユーザーがくれた絵をなぞったもの (先は距離 1): 中心の近くは丸い先、
    // 半分より少し先がいちばん太く、先は 2 つで間に V の切れ込みがある。
    ui::Path path;
    for (int i = 0; i < 5; ++i) {
        const float a = degreesCcw + 72.f * static_cast<float>(i);
        auto at = [&](float x, float y) { return Add(center, Scale(Rotate({x, y}, a), radius)); };
        path.Begin(at(0, -0.095f));
        path.Cubic(at(0.17f, -0.17f), at(0.285f, -0.40f), at(0.285f, -0.58f));
        path.Cubic(at(0.285f, -0.78f), at(0.19f, -0.92f), at(0.09f, -0.996f));
        path.Line(at(0, -0.84f));  // 切れ込み
        path.Line(at(-0.09f, -0.996f));
        path.Cubic(at(-0.19f, -0.92f), at(-0.285f, -0.78f), at(-0.285f, -0.58f));
        path.Cubic(at(-0.285f, -0.40f), at(-0.17f, -0.17f), at(0, -0.095f));
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

ui::Path BeachScene(Vec2 base, float h) {
    ui::Path p;
    // 小山: 幅広で低い丘で、足はウィンドウの下端 (半分は下にはみ出す)。
    AddEllipse(p, {base.x, base.y + 0.04f * h}, 0.78f * h, 0.17f * h, 0);
    AddEllipse(p, {base.x + 0.42f * h, base.y + 0.02f * h}, 0.42f * h, 0.12f * h, -4.f);
    // そこに刺さった左に傾くパラソルと、小山の左にある砂のお城。
    ui::Path parasol = BeachParasol({base.x + 0.18f * h, base.y - 0.08f * h}, 0.86f * h, 12.f);
    p.steps.insert(p.steps.end(), parasol.steps.begin(), parasol.steps.end());
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
        // 長さを 1 にした形 (y: 付け根 -0.5 .. 先 0.5)。右側を下り、切れ込み、左側を上る。
        static constexpr Vec2 kShape[] = {{0, -0.5f},     {0.12f, -0.38f}, {0.22f, -0.2f}, {0.29f, 0},      {0.31f, 0.18f},
                                          {0.27f, 0.34f}, {0.17f, 0.46f},  {0.07f, 0.5f},  {0, 0.4f},       {-0.07f, 0.5f},
                                          {-0.17f, 0.46f}, {-0.27f, 0.34f}, {-0.31f, 0.18f}, {-0.29f, 0},   {-0.22f, -0.2f},
                                          {-0.12f, -0.38f}};
        std::vector<Vec2> pts;
        for (const Vec2 v : kShape) pts.push_back(Add(c, Rotate({v.x * size, v.y * size}, deg)));
        ui::Path p;
        AddPolygon(p, pts);
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

ui::Path Snowfall(const ui::Rect& area, int count, float size, uint32_t seed) {
    ui::Path p;
    Random rnd{seed};
    for (int i = 0; i < count; ++i) {
        const Vec2 c{area.x + area.w * rnd.Next(), area.y + area.h * rnd.Next()};
        AddCircle(p, c, size * (0.35f + 0.65f * rnd.Next()) * 0.5f);
    }
    return p;
}

ui::Path SnowDrift(const ui::Rect& r, float depth) {
    ui::Path p;
    if (r.w <= 0 || depth <= 0) return p;
    const float bottom = r.y + r.h;
    // 高さは左端からのピクセル `l` だけで決まる (ウィンドウの幅・高さに関係しない)。なだらかに
    // (2026-11-24: ユーザーの希望。前は 50 ピクセルほどの細かい波があった): 長い波 2 つだけ。0.46 .. 0.98 倍。
    auto top = [&](float l) {
        const float swells = 0.72f + 0.17f * std::sin(l * 0.017f) + 0.09f * std::sin(l * 0.039f + 1.f);
        return Vec2{r.x + l, bottom - depth * swells};
    };
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

}  // namespace rm::theme_seasons::ornament
