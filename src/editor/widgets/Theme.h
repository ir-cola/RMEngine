#pragma once
// エディターの見た目。コアの UI の土台 (src/ui) は、わざと色を持たない:
// 同じ土台で作ったゲームの UI は、エディターのテーマが変わっても変わってはいけない。
//
// 組み込みのパレット (黒 = ダーク、既定。白 = ライト) とテーマの一覧 (Themes():
// オプション > 色のテーマ > 「テーマを変更…」 が出すもの)。下の色は今使っているパレットで、ApplyTheme が
// 切り替える。どの画面も描くときにこれを読むので、切り替えは次のフレームで見える。
// 組み込みは 黒、白、PC の設定に合わせる。ほかのテーマはすべてテーマパックから来る: テーマファイルを
// 持つプラグイン (plugins/<author>/<name>/themes/*.json。プラグインを読むときに ThemeFile.h が読む。
// パックの絵はそのコードで、w::kOrnamentPoint に出る)。新しいテーマ = パックの中のテーマファイル。
// ほかのテーマに従うテーマもある (2026-11-26: 季節テーマパックの「季節で切り替える」): ソフトを立ち上げた日付の
// 季節 (と、予定では朝か夜か) で、パックのどのテーマの色と絵を使うかが決まる (ResolveFollowing)。

#include "editor/lang/Lang.h"
#include "ui/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {
struct OrnamentInfo;  // テーマパックの絵 (Widgets.h)
}

namespace rm::editor::theme {

using ui::Color;

struct Palette {
    Color bg, sidebar, panel, panelHeader, border, borderHover, field, hover, pressed, selected;
    Color accent, accentHover, accentPressed, text, textDim, textFaint, danger, warn, folder, shadow;
    Color scrollThumb, scrollThumbHot, dim;
    uint32_t caption;  // タイトルバー (0xRRGGBB)
    bool light;        // ライトテーマ (下のグラデーションは、ライト用かダーク用に作る)
};

inline constexpr Palette kDark{
    Color::Hex(0x1B1C1F), Color::Hex(0x151618), Color::Hex(0x232428), Color::Hex(0x2A2B30), Color::Hex(0x34363C),
    Color::Hex(0x4A4D55), Color::Hex(0x141518), Color::Hex(0x2D2F35), Color::Hex(0x26282D), Color::Hex(0x3B2326),
    Color::Hex(0xD62828), Color::Hex(0xE84444), Color::Hex(0xB31F1F), Color::Hex(0xE8E8EA), Color::Hex(0x9EA1A9),
    Color::Hex(0x6B6E76), Color::Hex(0xFF6B6B), Color::Hex(0xF2C14E), Color::Hex(0xE3B341), Color::Hex(0x000000, 0.35f),
    Color::Hex(0x45474E), Color::Hex(0x6A6D75), Color::Hex(0x000000, 0.55f), 0x151618, false,
};

inline constexpr Palette kLight{
    Color::Hex(0xF3F3F5), Color::Hex(0xE8E8EC), Color::Hex(0xFFFFFF), Color::Hex(0xEDEDF1), Color::Hex(0xD3D5DB),
    Color::Hex(0xB2B5BE), Color::Hex(0xFFFFFF), Color::Hex(0xE3E4E9), Color::Hex(0xD8D9DF), Color::Hex(0xF8DADC),
    Color::Hex(0xD62828), Color::Hex(0xE84444), Color::Hex(0xB31F1F), Color::Hex(0x1D1E23), Color::Hex(0x585B64),
    Color::Hex(0x8C8F98), Color::Hex(0xD03030), Color::Hex(0xA87A06), Color::Hex(0xC9921B), Color::Hex(0x000000, 0.14f),
    Color::Hex(0xC2C4CB), Color::Hex(0x9EA1AA), Color::Hex(0x000000, 0.35f), 0xE8E8EC, true,
};

// 今使っているパレット (Apply を呼ぶまではダーク)。
inline Color Bg = kDark.bg;
inline Color Sidebar = kDark.sidebar;
inline Color Panel = kDark.panel;
inline Color PanelHeader = kDark.panelHeader;
inline Color Border = kDark.border;
inline Color BorderHover = kDark.borderHover;
inline Color Field = kDark.field;
inline Color Hover = kDark.hover;
inline Color Pressed = kDark.pressed;
inline Color Selected = kDark.selected;
inline Color Accent = kDark.accent;
inline Color AccentHover = kDark.accentHover;
inline Color AccentPressed = kDark.accentPressed;
inline Color Text = kDark.text;
inline Color TextDim = kDark.textDim;
inline Color TextFaint = kDark.textFaint;
inline Color Danger = kDark.danger;
inline Color Warn = kDark.warn;
inline Color Folder = kDark.folder;
inline Color Shadow = kDark.shadow;
inline Color ScrollThumb = kDark.scrollThumb;
inline Color ScrollThumbHot = kDark.scrollThumbHot;
inline Color Dim = kDark.dim;  // ダイアログの後ろ
inline uint32_t CaptionRgb = kDark.caption;
inline bool IsLight = false;
inline const w::OrnamentInfo* CurrentOrnament = nullptr;  // テーマの絵 (ほとんどは無し)

// 季節。ソフトを立ち上げた日付の月で決まる: 春 = 3・4・5 月、夏 = 6・7・8 月、秋 = 9・10・11 月、冬 = 12・1・2 月。
enum class Season { Spring, Summer, Fall, Winter };
inline Season SeasonOfMonth(int month) {
    const int m = ((month - 1) % 12 + 12) % 12 + 1;  // (1 .. 12 の外は回して読む)
    if (m >= 3 && m <= 5) return Season::Spring;
    if (m >= 6 && m <= 8) return Season::Summer;
    if (m >= 9 && m <= 11) return Season::Fall;
    return Season::Winter;
}
// 1 日のうちのどちらか: 季節のテーマは、1 つの季節に朝用と夜用を持てる (夜用はまだ無い: 予定は docs/HOW_TO_TRY.md の
// テーマの章。朝か夜かをどう決めるかも、夜用を作るときに決める。今はいつも朝)。
enum class DayPart { Morning, Night };
// 1 つの季節に使うテーマの id: 朝用と夜用 (空: 朝用を使う)。
struct SeasonThemes {
    std::string morning, night;
};
// ほかのテーマに従うテーマの、従い方 (今は季節だけ: seasons は Season の順)。
struct ThemeFollow {
    std::array<SeasonThemes, 4> seasons;
};
// 季節 `season` の `part` に使うテーマの id の候補: 夜なら夜用、それが無ければ朝用。
inline std::vector<std::string> FollowedIds(const ThemeFollow& f, Season season, DayPart part) {
    const SeasonThemes& s = f.seasons[static_cast<size_t>(season)];
    std::vector<std::string> ids;
    if (part == DayPart::Night && !s.night.empty()) ids.push_back(s.night);
    if (!s.morning.empty()) ids.push_back(s.morning);
    return ids;
}

// 一覧の 1 つのテーマ。その id を hub.json が持つ (options.theme)。
struct ThemeInfo {
    std::string id;
    std::wstring name;               // 日本語 (表示するとき Tr を通す)
    std::optional<Palette> palette;  // none: Windows に合わせる (kDark か kLight)
    int group = 0;                   // 0 = 組み込みの 3 つ、そのあとはパックごとに 1 つの番号 (テーマの画面は
                                     // それぞれ、パックの名前の下で新しい行を始める)
    std::wstring pack;               // パックの名前 (そのプラグインの表示名。表示するとき Tr を通す)
    std::vector<std::string> aliases;           // 前に保存されていた id (そのころの hub.json がこれを持っている)
    const w::OrnamentInfo* ornament = nullptr;  // その絵。パックのコードから
    // ほかのテーマに従う (季節で切り替える)。palette と ornament は ResolveFollowing が従うテーマのものにする。
    std::optional<ThemeFollow> follow;
    std::wstring followed;  // 今従っているテーマの名前 (日本語。表示するとき Tr を通す)。空: 従わない
    // 従うテーマの、季節ごとの朝用の色 (Season の順に 4 つ。テーマの画面の見本を 春｜夏｜秋｜冬 に区切って描く)。
    // 空: 1 つの色で描く (従わない・どれかの季節のテーマが無い)。
    std::vector<Palette> seasons;
};

namespace detail {
inline std::vector<ThemeInfo>& List() {
    static std::vector<ThemeInfo> list{
        {"Dark", TrKey(L"黒"), kDark},
        {"Light", TrKey(L"白"), kLight},
        {"System", TrKey(L"PC の設定に合わせる"), std::nullopt},
    };
    return list;
}
inline constexpr size_t kBuiltIn = 3;
}  // namespace detail

inline std::span<const ThemeInfo> Themes() { return detail::List(); }
// パックのテーマ (プラグインを読み直すたびに読み直す)。組み込みの 3 つのあと。FindTheme が
// 返す参照は、次に呼ぶまで有効。
inline void SetPackThemes(std::vector<ThemeInfo> themes) {
    auto& list = detail::List();
    list.resize(detail::kBuiltIn);
    for (ThemeInfo& t : themes) list.push_back(std::move(t));
}
// ほかのテーマに従うテーマ (follow) に、季節 `season` の `part` のテーマの色・絵・名前を写す (同じ一覧か、組み込みの
// テーマから探す。従う先がまた従うテーマなのは使わない)。従う先が無いものは一覧から外し、その id を `missing` に。
inline void ResolveFollowing(std::vector<ThemeInfo>& themes, Season season, DayPart part, std::vector<std::string>* missing = nullptr) {
    auto find = [&](const std::string& id) -> const ThemeInfo* {
        for (const ThemeInfo& t : themes) {
            if (t.id == id && !t.follow) return &t;
        }
        for (size_t i = 0; i < detail::kBuiltIn && i < detail::List().size(); ++i) {
            if (detail::List()[i].id == id) return &detail::List()[i];
        }
        return nullptr;
    };
    std::vector<std::string> lost;
    for (ThemeInfo& t : themes) {
        if (!t.follow) continue;
        const ThemeInfo* to = nullptr;
        for (const std::string& id : FollowedIds(*t.follow, season, part)) {
            if ((to = find(id)) != nullptr) break;
        }
        if (!to) {
            lost.push_back(t.id);
            continue;
        }
        t.palette = to->palette;
        t.ornament = to->ornament;
        t.followed = to->name;
        // 見本: 4 つの季節の朝用の色 (どれかが無ければ、見本は今の季節の色だけ)。
        t.seasons.clear();
        for (int s = 0; s < 4; ++s) {
            const ThemeInfo* look = nullptr;
            for (const std::string& id : FollowedIds(*t.follow, static_cast<Season>(s), DayPart::Morning)) {
                if ((look = find(id)) != nullptr) break;
            }
            if (!look) {
                t.seasons.clear();
                break;
            }
            t.seasons.push_back(look->palette.value_or(kDark));
        }
    }
    std::erase_if(themes, [&](const ThemeInfo& t) { return std::find(lost.begin(), lost.end(), t.id) != lost.end(); });
    if (missing) *missing = std::move(lost);
}
// 知らない id (入っていないパック、新しいバージョンのテーマ、hub.json の打ち間違い) は、
// 一覧の最初のテーマ (そのあと App がそれを hub.json に書く: パックをもう一度入れても元には戻らない)。
inline const ThemeInfo& FindTheme(std::string_view id) {
    const auto list = Themes();
    for (const ThemeInfo& t : list) {
        if (id == t.id) return t;
    }
    for (const ThemeInfo& t : list) {
        for (const std::string& a : t.aliases) {
            if (id == a) return t;
        }
    }
    return list[0];
}
// テーマが今表すパレット ("PC の設定に合わせる" 用: Windows が使っているもの)。
inline const Palette& PaletteOf(const ThemeInfo& t, bool systemPrefersLight) {
    return t.palette ? *t.palette : systemPrefersLight ? kLight : kDark;
}

inline void Apply(const Palette& p) {
    const bool light = p.light;
    Bg = p.bg, Sidebar = p.sidebar, Panel = p.panel, PanelHeader = p.panelHeader, Border = p.border;
    BorderHover = p.borderHover, Field = p.field, Hover = p.hover, Pressed = p.pressed, Selected = p.selected;
    Accent = p.accent, AccentHover = p.accentHover, AccentPressed = p.accentPressed, Text = p.text;
    TextDim = p.textDim, TextFaint = p.textFaint, Danger = p.danger, Warn = p.warn, Folder = p.folder, Shadow = p.shadow;
    ScrollThumb = p.scrollThumb, ScrollThumbHot = p.scrollThumbHot, Dim = p.dim, CaptionRgb = p.caption;
    IsLight = light;
}
// テーマのパレットとその絵。
inline void ApplyTheme(const ThemeInfo& t, bool systemPrefersLight) {
    Apply(PaletteOf(t, systemPrefersLight));
    CurrentOrnament = t.ornament;
}

inline constexpr float FontSize = 13.f;
inline constexpr float Radius = 4.f;

// --- グラデーション -------------------------------------------------------------------------------
// 上にある単色を上から照らす: 上を少し明るく、下を少し暗く - 同じ色相なので、パレットはそのまま。
// ウィジェットが描く箱はすべてこれを使う。

struct Gradient {
    Color top, bottom;
    Gradient Flipped() const { return {bottom, top}; }  // 押された / へこんだ
};

// 目立つ面 (ボタン、ヘッダー、タブ): ダークテーマでは強め。上からの光がよく見えるため。
inline Gradient Raised(Color c) { return IsLight ? Gradient{Shade(c, 0.55f), Shade(c, -0.05f)} : Gradient{Shade(c, 0.07f), Shade(c, -0.12f)}; }
// 広い面 (パネル、サイドバー): 同じ光で、穏やかに。
inline Gradient Gentle(Color c) { return IsLight ? Gradient{Shade(c, 0.35f), Shade(c, -0.025f)} : Gradient{Shade(c, 0.035f), Shade(c, -0.06f)}; }
// へこんだ面 (テキストフィールド、チェックボックス): 上から照らしたように、上のほうが暗い。
inline Gradient Sunken(Color c) { return IsLight ? Gradient{Shade(c, -0.05f), c} : Gradient{Shade(c, -0.25f), Shade(c, 0.03f)}; }
// アクセント (主なボタン、選択した項目): どちらのテーマでも明るくはっきり。
inline Gradient AccentOf(Color c) { return {Shade(c, 0.12f), Shade(c, -0.14f)}; }

// 浮いた面の上端の内側にある、細くて明るい線 (光が当たる所)。
inline Color Highlight() { return IsLight ? Color{1, 1, 1, 0.75f} : Color{1, 1, 1, 0.06f}; }
// パネルの縁、2 本の線: すぐ外側の暗い線 (ドックしたパネルの間の 1px の継ぎ目も兼ねる) と、
// すぐ内側の細くて明るい線。後者は上ほど明るく、下では薄くなる - それでも背景よりは明るい - ので、
// パネルのどの辺も見える。
inline Color EdgeOuter() { return IsLight ? Color::Hex(0xC4C6CD) : Color::Hex(0x0B0C0E); }
inline Gradient EdgeInner() { return IsLight ? Gradient{Color{1, 1, 1, 1}, Color{1, 1, 1, 0.55f}} : Gradient{Color{1, 1, 1, 0.10f}, Color{1, 1, 1, 0.035f}}; }
inline Color AccentHighlight() { return Color{1, 1, 1, 0.22f}; }
// テーマパックのシルエット: アクセント色で薄く (内容の後ろ。パネルの上にも)。
inline Color OrnamentColor() { return Accent.Alpha(IsLight ? 0.10f : 0.12f); }

// 色の明るさの見え方 (相対輝度。0 = 黒 .. 1 = 白。WCAG の式)。
inline float Luminance(Color c) {
    auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}
// `bg` の上でより目立つほう、白か黒 (コントラスト比が高いほう): Hub のロゴのような
// 単色の絵が、どのテーマでも見えるように。
inline Color InkOn(Color bg) {
    const float l = Luminance(bg);
    const float onWhite = 1.05f / (l + 0.05f), onBlack = (l + 0.05f) / 0.05f;
    return onWhite >= onBlack ? Color{1, 1, 1, 1} : Color{0, 0, 0, 1};
}
// アクセントの上の文字と印 (いつも白: アクセントはどちらのテーマでも同じ赤)。
inline Color OnAccent() { return Color{1, 1, 1, 1}; }
// スイッチ (w::Switch): オフのときのトラックと、丸いつまみ (どのテーマでも白。スマホと同じ)。
inline Color SwitchOff() { return IsLight ? Color::Hex(0xD3D6DC) : Color::Hex(0x3B3F48); }
inline Color SwitchKnob() { return Color{1, 1, 1, 1}; }

// --- 3D ビュー (シーンビュー): 床、軸、シーンの中のもの -------------------------
inline Color SceneBackground() { return IsLight ? Shade(Bg, -0.07f) : Shade(Field, -0.1f); }
inline Color SceneGrid() { return Text.Alpha(IsLight ? 0.14f : 0.09f); }
inline Color SceneGridMajor() { return Text.Alpha(IsLight ? 0.30f : 0.20f); }
inline Color AxisX() { return Color::Hex(0xE5484D); }  // 赤。どの 3D ツールでも同じ
inline Color AxisY() { return Color::Hex(0x5BB974); }  // 緑
inline Color AxisZ() { return Color::Hex(0x4C8DF6); }  // 青
inline Color GizmoHot() { return Color::Hex(0xF2C230); }  // マウスの下にある / ドラッグ中のギズモのハンドル (黄色)
inline Color SceneObject() { return IsLight ? Color::Hex(0xAEB4BF) : Color::Hex(0x98A2B3); }
inline Color SceneObjectSelected() { return Mix(SceneObject(), Accent, 0.55f); }
inline Color SceneObjectInactive() { return Mix(SceneObject(), SceneBackground(), 0.6f); }
// カメラの形 (見えるものの線) と、ライトの形 (向き、届く範囲) と、その印。
inline Color SceneCamera() { return Text.Alpha(IsLight ? 0.70f : 0.75f); }
inline Color SceneLight() { return IsLight ? Color::Hex(0xC08A12) : Color::Hex(0xF2D16B); }
// 自分の形を持たないものの印 (空のエンティティ、パーティクル、音源): カメラの印と同じ。
inline Color SceneMark() { return SceneCamera(); }
// モデルは、自分の色 (マテリアルと絵) に、これを掛けて描く: そのままか、オフのときは薄く
// 透かして。選択中のものは、色を変えず、縁が光る。
inline Color SceneModel() { return Color{1, 1, 1, 1}; }
inline Color SceneModelInactive() { return Color{1, 1, 1, 0.35f}; }
inline Color SceneSelectedGlow() { return Accent.Alpha(0.85f); }
// モデルのサムネイルの後ろ (アセット パネル): 薄い四角。明るいモデルも暗いモデルも、どちらのテーマでも見える。
inline Color ThumbnailBack() { return Text.Alpha(IsLight ? 0.06f : 0.08f); }
// 床に落ちる影 (背景の上に重ねる: ダークテーマでは濃くして、それでも見えるように)。
inline Color SceneShadow() { return Color{0, 0, 0, IsLight ? 0.30f : 0.55f}; }
// シーンの上に描く物理の形 (物理: コライダー、キャラクター): 固いもの、トリガー (ものが通り抜ける -
// 知らせるだけ)、オフにしたもの (薄く)。
inline Color SceneCollider() { return IsLight ? Color::Hex(0x1F9D55) : Color::Hex(0x5EE08F); }
inline Color SceneTrigger() { return IsLight ? Color::Hex(0x8E44C9) : Color::Hex(0xC79BF2); }
inline Color SceneColliderInactive() { return Mix(SceneCollider(), SceneBackground(), 0.6f); }
// シーンの上に描くエリアの枠 (気候エリアなど): 縁と、境目の内側の縁 (薄く)。
inline Color SceneArea() { return IsLight ? Color::Hex(0x2B6FD6) : Color::Hex(0x7DB8FF); }
inline Color SceneAreaInner() { return Mix(SceneArea(), SceneBackground(), 0.55f); }
// 真上から見た地図 (気候のタブ) に塗った所の濃さ (色はユーザーのもの: 下の絵が透けるように)。
inline float MapPaintAlpha() { return IsLight ? 0.50f : 0.45f; }
// 地図に重ねる値の色 (気候のタブの「見る」: 0 .. 1 の値を色の段に)。気温: 寒い青 -> 暑い赤。湿り気: 乾いた
// 黄土 -> 湿った青緑。雲: 晴れの空色 -> 厚い雲の灰色。雨: 淡い水色 -> 嵐の濃い藍。
namespace map_detail {
inline Color Ramp(std::span<const uint32_t> stops, float t) {
    if (stops.empty()) return Color{};
    const float x = (std::isfinite(t) ? std::clamp(t, 0.f, 1.f) : 0.f) * static_cast<float>(stops.size() - 1);
    const size_t i = std::min(static_cast<size_t>(x), stops.size() - 1), j = std::min(i + 1, stops.size() - 1);
    return Mix(Color::Hex(stops[i]), Color::Hex(stops[j]), x - static_cast<float>(i));
}
}  // namespace map_detail
inline Color MapTemperature(float t) {
    static constexpr uint32_t kStops[] = {0x3B4CC0, 0x6F9BEF, 0xC9E3F2, 0xF6D776, 0xF08A4B, 0xB2182B};
    return map_detail::Ramp(kStops, t);
}
inline Color MapHumidity(float t) {
    static constexpr uint32_t kStops[] = {0xB98A4A, 0xE3CD8C, 0xBFE0B0, 0x52B3A6, 0x1F6F8B};
    return map_detail::Ramp(kStops, t);
}
inline Color MapClouds(float t) {
    static constexpr uint32_t kStops[] = {0x9FD3F5, 0xE8EEF2, 0xA9B1B8, 0x5D646B};
    return map_detail::Ramp(kStops, t);
}
inline Color MapRain(float t) {
    static constexpr uint32_t kStops[] = {0xCDE7FA, 0x7DB4EC, 0x3A78D0, 0x24439E, 0x2E1F6E};
    return map_detail::Ramp(kStops, t);
}
// 値の色の濃さ (下の絵が少し透ける)。
inline float MapHeatAlpha() { return IsLight ? 0.62f : 0.58f; }
// 地図に描いた形 (ペンで囲んだ所) の縁と、その点の印。
inline Color MapShape() { return IsLight ? Color::Hex(0x1C2430) : Color::Hex(0xF2F5F8); }
inline Color MapShapeSelected() { return Accent; }

// --- ソフトが動いている (Play 中のゲーム): そのタブの行をアクセント色で染める -----------------
inline Color RunningBar() { return Mix(Bg, Accent, IsLight ? 0.22f : 0.28f); }

}  // namespace rm::editor::theme
