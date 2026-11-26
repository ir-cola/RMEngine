#include "rm_engine/theme_seasons/editor/Seasons.h"

#include "editor/widgets/OrnamentPicture.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <array>
#include <vector>

namespace rm::theme_seasons {

namespace {

namespace theme = editor::theme;
using ui::Color;
using ui::Rect;

// 季節の絵の色 (内容の後ろにうっすら敷く: アルファも色のうち)。絵そのものの色は、どのパレットでも同じ。
// 影はテーマのアクセント色 (theme::OrnamentColor) を使う。
namespace season {
Color Petal() { return Color::Hex(0xEE8FAE, 0.42f); }       // 春: 吹雪 (パネルのウィンドウ)
Color PetalTone(int i) {                                    // 春: 上に沿って散る花びら、4 色 (ソメイヨシノの色:
    // ほとんど白に近い淡い桃色から、少し濃い桃色まで。2026-11-25: 前は濃いピンクで本物らしくなかった)
    constexpr uint32_t k[4] = {0xFBE3EA, 0xF7D0DB, 0xF2BCCB, 0xFDEFF3};
    return Color::Hex(k[i & 3], 0.92f);
}
Color PetalEdge() { return Color::Hex(0xD98AA3, 0.38f); }  // その縁 (右下に少しずらした影: 淡い背景でも見える)
// 夏: 右上の雲 (2026-11-25: 本物らしく)。半透明の白い雲に、上の縁の明るい帯を重ね、下ほど濃くなる
// 青灰色の影を重ねる (帯が重なるほど明るい / 暗い)。空 (背景の青) の上でも砂 (パネル) の上でも雲に見える。
// 夏: 左下の太陽は、ほかの季節のシルエット (theme::OrnamentColor) より濃く (2026-11-25: ユーザーの希望)。
// 夏: パネルの砂のざらざら (Painter::FillGrain)。明るい砂色の上に、白っぽい粒と少し濃い砂色の粒
// (2026-11-25 の 2 回目: 茶色の粒と大きな粒が汚れて見えたので、Minecraft の砂ブロックのような明るく
// 淡い黄色の、差の小さい粒に。大きな粒は無し)。
ui::GrainLook Sand() {
    ui::GrainLook g;
    g.light = Color::Hex(0xFFFDF0, 0.60f);
    g.dark = Color::Hex(0xD9C58C, 0.42f);
    g.lightShare = 0.14f;
    g.darkShare = 0.14f;
    g.clumps = 0.f;
    g.seed = 0x5A9Du;
    return g;
}
Color SummerShade() { return theme::Accent.Alpha(0.24f); }  // 夏のシルエット: 太陽・砂浜とパラソル・雲 (同じ濃さ)
Color Leaf(int i) {                                         // 秋: 樹冠、4 色
    constexpr uint32_t k[4] = {0xD8432A, 0xE8832A, 0xE7B43A, 0xB0432A};
    return Color::Hex(k[i & 3], 0.34f);
}
Color Snow() { return Color::Hex(0xFFFFFF); }             // 冬: ウィンドウの一番下に積もる
Color SnowEdge() { return Color::Hex(0xA9BBCF, 0.55f); }  // 積もった雪の輪郭 (薄く: 2026-11-24 ユーザーの希望)
Color SnowLow() { return Color::Hex(0xE6EEF7); }          // 吹きだまりの影になった足元
Color Flake() { return Color::Hex(0xFFFFFF, 0.9f); }      // 降る雪
Color NightFlake() { return Color::Hex(0xFFFFFF, 0.55f); }  // 夜の降る雪 (暗い地では白が強いので薄く)
Color Bucket() { return Color::Hex(0xD9412E); }           // 雪だるまのバケツ
Color BucketRim() { return Color::Hex(0xB0301F); }        // その縁

// 夜 (2026-11-26): 暗い地の上なので、明るい色を薄めに (文字が読めるように)。
Color NightPetal(int i) { return PetalTone(i).Alpha(0.50f); }  // 夜桜: 花びら (縁は付けない: 暗い地ではいらない)
Color NightFlurry() { return Color::Hex(0xF7C6D6, 0.36f); }   // 夜桜: 吹雪 (パネルのウィンドウ)
Color WindStreak() { return Color::Hex(0xF2BCCB, 0.30f); }  // 春: 花びらを運ぶ風の筋 (2026-11-26 の 2 回目)
// 花火 (リアル調): 1 発ごとに筋の色と先の星の色。まわりの光はごく薄く。
struct FireworkLook {
    uint32_t trail, tip;
};
constexpr FireworkLook kFireworks[6] = {{0xFFB347, 0xFFE3A0}, {0xFF5040, 0xFFB8A8}, {0x5CFF8A, 0xD6FFDF},
                                        {0x7A9CFF, 0xE2EAFF}, {0xFF6FC0, 0xFFD3EE}, {0xE8ECFF, 0xFFFFFF}};
// (2026-11-26 の 3 回目: 主張が強すぎたので、かなり薄く)
Color FireworkGlow(int i) { return Color::Hex(kFireworks[i % 6].trail, 0.02f); }
Color FireworkTrail(int i) { return Color::Hex(kFireworks[i % 6].trail, 0.15f); }
Color FireworkTip(int i) { return Color::Hex(kFireworks[i % 6].tip, 0.24f); }
Color SeaFar() { return Color::Hex(0x14456B, 0.50f); }        // 夜の海 (奥の波)
Color SeaNear() { return Color::Hex(0x1F6394, 0.42f); }       // 手前の波
Color DaySeaFar() { return Color::Hex(0x4FA3D9, 0.30f); }     // 夏の朝の海 (奥の波。2026-11-26 の 2 回目)
Color DaySeaNear() { return Color::Hex(0x2F86C2, 0.28f); }    // 手前の波
Color MoonHalo() { return Color::Hex(0xFFE9A8, 0.04f); }      // お月見: 月のまわりの光 (2 重)
Color Moon() { return Color::Hex(0xFFE7A0, 0.20f); }          // (上に文字が来ても読めるように、ごく薄く: 2026-11-26 の 2 回目)
Color MoonSpot() { return Color::Hex(0xE2BF72, 0.12f); }
Color Star() { return Color::Hex(0xFFFFFF, 0.38f); }          // 夜空の星
Color FallNightShade() { return theme::Accent.Alpha(0.17f); }  // お月見のシルエット (ほかの季節より少し明るく)
Color WinterGreen() { return Color::Hex(0x3DBA74, 0.30f); }   // クリスマスのシルエット (緑で濃いめ)
Color DoorDark() { return Color::Hex(0x0E1828, 0.85f); }      // かまくらの入り口
Color DoorLight() { return Color::Hex(0xFFB357, 0.60f); }     // その中の灯り

}  // namespace season

enum class Kind { Spring, Summer, Fall, Winter, SpringNight, SummerNight, FallNight, WinterNight };

// 絵 = 色の付いた図形を、順に塗ったもの (作ったものは w::DrawOrnamentPicture が覚えておく)。
using Picture = editor::w::OrnamentPicture;

// 左下の角の影: どの季節も同じ正方形 (w::OrnamentCorner) に収める (もみじの葉が桜の花と同じ高さになる)。
void Silhouette(Picture& pic, const Rect& w, ui::Path shape, Color color = theme::OrnamentColor()) {
    pic.push_back({ornament::FitInto(shape, editor::w::OrnamentCorner(w)), color});
}

void SpringFlurry(Picture& pic, const Rect& w, float scale) {
    const Rect area{w.x, w.y, std::min(w.w * 0.42f, 560.f * scale), std::min(w.h * 0.38f, 320.f * scale)};
    const int count = std::max(10, static_cast<int>(area.w * area.h / 4200.f));
    pic.push_back({ornament::Petals(area, count, 9.f * std::max(scale, 0.7f), 0x5A6E7Au), season::Petal()});
}

// 雲: 形は本物らしい積雲 (底が平らで、上がもくもく)。色はほかの夏のシルエットと同じ 1 色
// (2026-11-25: ユーザーの希望。白と影の帯で色をリアルにしたものはやめた)。
void SummerCloud(Picture& pic, Vec2 center, float width, uint32_t seed) {
    ornament::CloudShapes cloud = ornament::Cumulus(center, width, seed);
    pic.push_back({std::move(cloud.body), season::SummerShade()});
}

void SummerClouds(Picture& pic, const Rect& w, float scale) {
    const float cw = std::clamp(w.w * 0.16f, 110.f, 260.f) * scale;
    SummerCloud(pic, {w.Right() - w.w * 0.17f, w.y + std::max(70.f, w.h * 0.12f) * scale}, cw, 0x5C10u);
    SummerCloud(pic, {w.Right() - w.w * 0.40f, w.y + std::max(42.f, w.h * 0.07f) * scale}, cw * 0.62f, 0x5C21u);
    SummerCloud(pic, {w.Right() - w.w * 0.06f, w.y + std::max(150.f, w.h * 0.25f) * scale}, cw * 0.5f, 0x5C32u);
}

// 冬: ウィンドウに降る雪 (止めたもの)。(2026-11-24: 上の端のつららはユーザーの希望で外した。
// 形を作る ornament::Icicles は残してある)
void WinterSky(Picture& pic, const Rect& w, float scale, Color flake) {
    const int count = std::max(20, static_cast<int>(w.w * w.h / 3600.f));
    pic.push_back({ornament::Snowfall(w, count, 5.5f * scale, 0x5A0Fu), flake});
}

// 冬: ウィンドウ (エディター本体・Hub) の一番下に積もった雪: 上に輪郭、白い雪、その影になった足元。
// パネルのウィンドウには積もらない。パネルごとにも積もらせない (2026-11-24: ユーザーの希望。前は
// どのパネルの下にも積もった)。ウィンドウの絵なので、一番下のパネルやバーの塗りの上にも内容の後ろに出る。
// 左右の端まで積もり、ウィンドウの端で切れる。高さは決まった値で、形は左端からの位置だけで決まる
// (2026-11-25: ユーザーの希望。大きさを変えても形が変わらない)。
// 夜 (クリスマス) は雪がもっと高く積もり、雪だるまの代わりに一回り大きいかまくら (2026-11-26 の 2 回目)。
void WinterGround(Picture& pic, const Rect& w, const Rect& screen, bool night) {
    const float kDepth = night ? 40.f : 24.f;
    // 雪だるま (2026-11-25): **画面** (モニター) の右下に立つ。ウィンドウが画面の右端まで無いと (小さいウィンドウ、
    // Hub のそのままの大きさ) その外になって見えない。全画面 (最大化) で右下に出る。大きさは DIP (文字と同じく
    // Windows の拡大率で大きくなる)。体は雪と同じ白で、輪郭は雪の輪郭と 1 つにつながる: 輪郭 (雪の縁と、
    // 少し太らせた丸) を先に描き、その上に白 (丸と雪) を重ねる。足元は雪に埋もれる。
    const float kHeight = night ? 48.f : 58.f;  // (かまくら: 高さ 48・幅 77。雪だるまより一回り大きい)
    const float x = screen.Right() - (night ? 80.f : 64.f);
    const float half = night ? 0.8f * kHeight : 0.4f * kHeight;
    const bool snowman = x + half > w.x && x - half < w.Right();
    ornament::SnowmanShapes man, edge;
    ornament::KamakuraShapes hut;
    if (snowman) {
        const float ground = w.y + w.h - ornament::SnowDriftHeight(x - w.x, kDepth) + 4.f;
        if (night) {
            edge.body = std::move(ornament::Kamakura({x, ground}, kHeight, 1.5f).dome);
            hut = ornament::Kamakura({x, ground}, kHeight);
            man.body = std::move(hut.dome);
        } else {
            edge = ornament::Snowman({x, ground}, kHeight, 1.5f);
            man = ornament::Snowman({x, ground}, kHeight);
        }
    }
    if (snowman) pic.push_back({std::move(edge.body), season::SnowEdge()});
    pic.push_back({ornament::SnowDrift({w.x, w.y - 1.5f, w.w, w.h}, kDepth), season::SnowEdge()});
    if (snowman) pic.push_back({std::move(man.body), season::Snow()});
    pic.push_back({ornament::SnowDrift(w, kDepth), season::Snow()});
    pic.push_back({ornament::SnowDrift(w, kDepth * 0.45f), season::SnowLow()});
    if (snowman && night) {
        pic.push_back({std::move(hut.door), season::DoorDark()});
        pic.push_back({std::move(hut.light), season::DoorLight()});
    } else if (snowman) {
        pic.push_back({std::move(man.bucket), season::Bucket()});
        pic.push_back({std::move(man.rim), season::BucketRim()});
    }
}

// 春: 風に吹かれて飛ぶ花びら (朝 = 左から右へ、夜 = 右から左へ)。朝は少しずらした縁を先に描く。
void PetalsInWind(Picture& pic, const Rect& w, float wind, bool night) {
    const float depth = std::max(200.f, w.h * 0.85f);
    if (!night) {
        auto edges = ornament::PetalWind({w.x + 0.8f, w.y + 1.f, w.w, w.h}, depth, 0.3f, wind, 0x5A4Bu);
        for (int i = 0; i < 4; ++i) pic.push_back({std::move(edges.petals[static_cast<size_t>(i)]), season::PetalEdge()});
    }
    auto petals = ornament::PetalWind(w, depth, 0.3f, wind, 0x5A4Bu);
    pic.push_back({std::move(petals.streaks), season::WindStreak()});
    for (int i = 0; i < 4; ++i) {
        pic.push_back({std::move(petals.petals[static_cast<size_t>(i)]), night ? season::NightPetal(i) : season::PetalTone(i)});
    }
}

// 夏の夜: 画面の上のほう全体に、種類と色の違う花火 6 発 (リアル調。2026-11-26 の 2 回目)。
void Fireworks(Picture& pic, const Rect& w, float scale) {
    struct Shot {
        float fx, fy, size;
        ornament::FireworkKind kind;
        int look;
    };
    using K = ornament::FireworkKind;
    static constexpr Shot kShots[] = {{0.07f, 0.18f, 0.62f, K::Peony, 4},  {0.24f, 0.30f, 0.95f, K::Chrysanthemum, 0},
                                      {0.43f, 0.14f, 0.78f, K::Willow, 0}, {0.61f, 0.27f, 1.f, K::Chrysanthemum, 2},
                                      {0.79f, 0.13f, 0.7f, K::Peony, 3},   {0.94f, 0.34f, 0.6f, K::Chrysanthemum, 1}};
    const float r = std::clamp(std::min(w.w, w.h) * 0.15f, 60.f, 190.f) * scale;
    uint32_t seed = 0xF100u;
    for (const Shot& s : kShots) {
        ornament::FireworkShapes f = ornament::FireworkShell({w.x + w.w * s.fx, w.y + w.h * s.fy}, r * s.size, s.kind, ++seed);
        pic.push_back({std::move(f.glow[0]), season::FireworkGlow(s.look)});
        pic.push_back({std::move(f.glow[1]), season::FireworkGlow(s.look)});
        pic.push_back({std::move(f.trails), season::FireworkTrail(s.look)});
        pic.push_back({std::move(f.tips), season::FireworkTip(s.look)});
    }
}

// 夏: 下の端の海 (2 枚の波)。
void Sea(Picture& pic, const Rect& w, Color far, Color near) {
    pic.push_back({ui::shape::WavyBottom(w, 46.f, 0.f), far});
    pic.push_back({ui::shape::WavyBottom(w, 24.f, 2.1f), near});
}

// 夜空の星 (お月見): 上半分に小さな点。
void Stars(Picture& pic, const Rect& w) {
    const Rect sky{w.x, w.y, w.w, w.h * 0.55f};
    const int count = std::max(12, static_cast<int>(sky.w * sky.h / 9000.f));
    pic.push_back({ornament::Snowfall(sky, count, 2.4f, 0x57A2u), season::Star()});
}

Picture Make(Kind kind, const Rect& w, bool floating, const Rect& screen) {
    Picture pic;
    switch (kind) {
        case Kind::Spring:
            if (floating) {
                SpringFlurry(pic, w, 0.6f);  // 花びらだけ (木と花はメインのウィンドウのもの)
                break;
            }
            {
                // 秋の葉と同じように、少しずつ違う桜色の花びらが上に沿って濃く、下へ散らばる。左下に桜の花。
                // (2026-11-24: 右の桜の木はユーザーの希望で外した)
                // ウィンドウの下の方まで散り、下ほどまばら (2026-11-25: 前は上の 3 割ほどまで)。縁 (少しずらした
                // 同じ花びら) を先に描く。
                // 2026-11-26 の 2 回目: 花びらは左から右へ吹かれて飛ぶ (風の筋も)。
                PetalsInWind(pic, w, 1.f, false);
                Silhouette(pic, w, ornament::Sakura({0, 0}, 1.f, 10.f));
            }
            break;
        case Kind::Summer:
            if (floating) {
                SummerClouds(pic, {w.x, w.y, w.w, std::min(w.h, 260.f)}, 0.6f);  // 雲だけ
                break;
            }
            {
                // 左下に太陽、右下に影 (春の木のような、パラソルと砂のお城のある砂の小山)、右上に雲。
                const float h = std::clamp(w.h * 0.40f, 150.f, 380.f);
                pic.push_back({ornament::BeachScene({w.Right() - 0.62f * h, w.Bottom()}, h), season::SummerShade()});  // (太陽と同じ濃さ)
                Sea(pic, w, season::DaySeaFar(), season::DaySeaNear());  // (2026-11-26 の 2 回目: 砂浜より手前に海)
                Silhouette(pic, w, ornament::Sun({0, 0}, 1.f, 10.f), season::SummerShade());
                SummerClouds(pic, w, 1.f);
            }
            break;
        case Kind::Fall:
            if (floating) break;
            {
                // 赤 / オレンジ / 黄色の葉が上に沿って濃く、下へ散らばる。左下にもみじの葉。
                // 春の花びらと同じく、ウィンドウの下の方まで散り、下ほどまばら (2026-11-25)。
                auto canopy = ornament::Canopy(w, std::max(200.f, w.h * 0.85f), 0.3f, 0xF411u);
                for (int i = 0; i < 4; ++i) pic.push_back({std::move(canopy[static_cast<size_t>(i)]), season::Leaf(i)});
                Silhouette(pic, w, ornament::MapleLeaf({0, 0}, 1.f, 10.f));
                // 右下にススキのひとむら (夏の砂浜のように右下の景色。穂は左へ垂れる)。
                const float h = std::clamp(w.h * 0.36f, 140.f, 340.f);
                pic.push_back({ornament::Susuki({w.Right() - 0.18f * h, w.Bottom()}, h, 0x5A5Au), theme::OrnamentColor()});
            }
            break;
        case Kind::Winter:
            WinterSky(pic, w, floating ? 0.8f : 1.f, season::Flake());
            if (!floating) {
                Silhouette(pic, w, ornament::Snowflake({0, 0}, 1.f, 10.f));
                WinterGround(pic, w, screen, false);
            }
            break;
        // --- 夜 (2026-11-26) ---
        case Kind::SpringNight:
            if (floating) {
                const Rect area{w.x, w.y, std::min(w.w * 0.42f, 336.f), std::min(w.h * 0.38f, 192.f)};
                const int count = std::max(10, static_cast<int>(area.w * area.h / 4200.f));
                pic.push_back({ornament::Petals(area, count, 6.3f, 0x5A6E7Au), season::NightFlurry()});
                break;
            }
            {
                // 夜桜: 右から左へ吹かれて飛ぶ花びら (薄め)、左下にぼんぼり (ユーザーが案から選んだ)。(提灯はいったん外した: 2026-11-26 の 2 回目。
                // 形を作る ornament::Garland / AddLantern は残してある)
                PetalsInWind(pic, w, -1.f, true);
                Silhouette(pic, w, ornament::Bonbori({0, 0}, 1.f, 0.f));
            }
            break;
        case Kind::SummerNight:
            if (floating) {
                Fireworks(pic, {w.x, w.y, w.w, std::min(w.h, 300.f)}, 0.5f);
                break;
            }
            // 花火 (上のほう全体)、右下に砂浜 (パラソル無し) とその手前の夜の海、左下に提灯 (案からユーザーが選んだ)。
            Fireworks(pic, w, 1.f);
            {
                const float h = std::clamp(w.h * 0.40f, 150.f, 380.f);
                pic.push_back({ornament::BeachScene({w.Right() - 0.62f * h, w.Bottom()}, h, false), theme::OrnamentColor()});
            }
            Sea(pic, w, season::SeaFar(), season::SeaNear());
            Silhouette(pic, w, ornament::Chochin({0, 0}, 1.f));
            break;
        case Kind::FallNight:
            Stars(pic, w);
            if (floating) break;
            {
                // 右上に満月 (まわりの光 2 重と、薄い影)、右下にススキ、左下に月見団子。
                const float r = std::clamp(std::min(w.w, w.h) * 0.11f, 44.f, 110.f);
                const Vec2 c{w.Right() - std::max(w.w * 0.16f, 1.8f * r), w.y + std::max(w.h * 0.2f, 1.8f * r)};
                ui::Path halo, inner, moon, spots;
                ornament::AddCircle(halo, c, 1.7f * r);
                ornament::AddCircle(inner, c, 1.3f * r);
                ornament::AddCircle(moon, c, r);
                ornament::AddCircle(spots, {c.x - 0.30f * r, c.y - 0.22f * r}, 0.22f * r);
                ornament::AddCircle(spots, {c.x + 0.28f * r, c.y + 0.12f * r}, 0.30f * r);
                ornament::AddCircle(spots, {c.x - 0.12f * r, c.y + 0.45f * r}, 0.14f * r);
                pic.push_back({std::move(halo), season::MoonHalo()});
                pic.push_back({std::move(inner), season::MoonHalo()});
                pic.push_back({std::move(moon), season::Moon()});
                pic.push_back({std::move(spots), season::MoonSpot()});
                const float h = std::clamp(w.h * 0.36f, 140.f, 340.f);
                pic.push_back({ornament::Susuki({w.Right() - 0.18f * h, w.Bottom()}, h, 0x5A5Au), season::FallNightShade()});
                Silhouette(pic, w, ornament::TsukimiDango({0, 0}, 1.f), season::FallNightShade());
            }
            break;
        case Kind::WinterNight:
            WinterSky(pic, w, floating ? 0.8f : 1.f, season::NightFlake());
            if (!floating) {
                // 降る雪、上のほうの真ん中に左上へ飛ぶサンタとトナカイ、左下にクリスマスツリー (どちらも緑)、
                // 下に朝より高く積もった雪とかまくら。(上の電球はいったん外した: 2026-11-26 の 2 回目)
                const float size = std::clamp(w.w * 0.06f, 55.f, 100.f);  // (2026-11-26 の 3 回目: 小さく)
                pic.push_back({ornament::SantaSleigh({w.x + w.w * 0.5f, w.y + std::max(w.h * 0.2f, 1.2f * size)}, size, -14.f),
                               season::WinterGreen()});
                Silhouette(pic, w, ornament::ChristmasTree({0, 0}, 1.f, 0.f), season::WinterGreen());
                WinterGround(pic, w, screen, true);
            }
            break;
    }
    return pic;
}

Picture MakeAny(int kind, const Rect& w, bool floating, const Rect& screen) { return Make(static_cast<Kind>(kind), w, floating, screen); }

template <Kind K>
void DrawWindow(ui::Context& c, const Rect& window, bool floating) {
    // 画面 (モニター) の場所を使うのは冬の雪だるまだけ (ほかの季節は、ウィンドウを動かしても作り直さない)。
    const Rect screen = (K == Kind::Winter || K == Kind::WinterNight) && !floating ? c.Screen() : Rect{};
    editor::w::DrawOrnamentPicture(c, &MakeAny, static_cast<int>(K), window, floating, screen);
}

constexpr editor::w::OrnamentInfo kSpring{"spring", &DrawWindow<Kind::Spring>, nullptr};
// 夏: パネルの面に砂のざらざら (中身の文字より奥に)。
void SummerPanel(ui::Context& c, const Rect& r, float corner) { c.P().FillGrain(r, corner, season::Sand()); }

constexpr editor::w::OrnamentInfo kSummer{"summer", &DrawWindow<Kind::Summer>, &SummerPanel};
constexpr editor::w::OrnamentInfo kFall{"fall", &DrawWindow<Kind::Fall>, nullptr};
constexpr editor::w::OrnamentInfo kWinter{"winter", &DrawWindow<Kind::Winter>, nullptr};
constexpr editor::w::OrnamentInfo kSpringNight{"spring_night", &DrawWindow<Kind::SpringNight>, nullptr};
constexpr editor::w::OrnamentInfo kSummerNight{"summer_night", &DrawWindow<Kind::SummerNight>, nullptr};
constexpr editor::w::OrnamentInfo kFallNight{"fall_night", &DrawWindow<Kind::FallNight>, nullptr};
constexpr editor::w::OrnamentInfo kWinterNight{"winter_night", &DrawWindow<Kind::WinterNight>, nullptr};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    for (const editor::w::OrnamentInfo* o : {&kSpring, &kSummer, &kFall, &kWinter, &kSpringNight, &kSummerNight, &kFallNight, &kWinterNight}) registry.Contribute(editor::w::kOrnamentPoint, *o);
}

}  // namespace rm::theme_seasons
