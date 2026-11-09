#pragma once
// The editor's look. The core UI foundation (src/ui) has no colors on purpose:
// a game UI built on the same foundation must not change when the editor theme does.
//
// The palettes (黒 = dark, the default; 白 = light) and the list of themes (kThemes: what オプション >
// 色のテーマ > 「テーマを変更…」 shows). The colors below are the palette in use; Apply switches them.
// Every screen reads them while drawing, so a switch shows on the next frame.
//
// A new theme = a Palette and one line in kThemes (its name in resources/lang/en.json too).

#include "editor/lang/Lang.h"
#include "ui/Types.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace rm::editor::theme {

using ui::Color;

// The pictures a theme puts faintly over the window (the four seasons; shapes: editor/widgets/Ornament.h,
// drawn by w::DrawOrnament and w::DrawFloatOrnament - and the snow of winter by the widgets themselves).
enum class Ornament : uint8_t { None, Spring, Summer, Fall, Winter };

struct Palette {
    Color bg, sidebar, panel, panelHeader, border, borderHover, field, hover, pressed, selected;
    Color accent, accentHover, accentPressed, text, textDim, textFaint, danger, warn, folder, shadow;
    Color scrollThumb, scrollThumbHot, dim;
    uint32_t caption;  // title bar (0xRRGGBB)
    bool light;        // a light theme (the gradients below are made for light or dark)
    Ornament ornament = Ornament::None;
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

// 深い青: dark navy, a blue accent.
inline constexpr Palette kMidnight{
    Color::Hex(0x161B26), Color::Hex(0x111520), Color::Hex(0x1C2230), Color::Hex(0x232A3A), Color::Hex(0x2E3648),
    Color::Hex(0x44506A), Color::Hex(0x10141D), Color::Hex(0x283044), Color::Hex(0x20273A), Color::Hex(0x1E3354),
    Color::Hex(0x3D8BFD), Color::Hex(0x5A9DFF), Color::Hex(0x2A6FD6), Color::Hex(0xE4E8F0), Color::Hex(0x98A3B8),
    Color::Hex(0x667089), Color::Hex(0xFF6B6B), Color::Hex(0xF2C14E), Color::Hex(0xE3B341), Color::Hex(0x000000, 0.40f),
    Color::Hex(0x3A4459), Color::Hex(0x5A6680), Color::Hex(0x000000, 0.55f), 0x111520, false,
};

// 森: dark with a touch of green, a green accent.
inline constexpr Palette kForest{
    Color::Hex(0x171D1A), Color::Hex(0x121714), Color::Hex(0x1D2420), Color::Hex(0x242C27), Color::Hex(0x303A34),
    Color::Hex(0x46544B), Color::Hex(0x111613), Color::Hex(0x29322D), Color::Hex(0x222A25), Color::Hex(0x1F3A2C),
    Color::Hex(0x2F9E6B), Color::Hex(0x3FB27F), Color::Hex(0x247F56), Color::Hex(0xE5EBE7), Color::Hex(0x9CA8A0),
    Color::Hex(0x6A766E), Color::Hex(0xFF6B6B), Color::Hex(0xF2C14E), Color::Hex(0xE3B341), Color::Hex(0x000000, 0.38f),
    Color::Hex(0x3C4741), Color::Hex(0x5C6A62), Color::Hex(0x000000, 0.55f), 0x121714, false,
};

// 紙: light and warm (cream paper), a burnt-orange accent.
inline constexpr Palette kPaper{
    Color::Hex(0xF4EFE6), Color::Hex(0xEAE3D6), Color::Hex(0xFBF8F2), Color::Hex(0xF0EADF), Color::Hex(0xD8CFBE),
    Color::Hex(0xBCB09A), Color::Hex(0xFFFDF8), Color::Hex(0xEAE2D3), Color::Hex(0xDFD6C5), Color::Hex(0xF5DCCB),
    Color::Hex(0xC2571A), Color::Hex(0xD66A2C), Color::Hex(0xA34512), Color::Hex(0x2B2620), Color::Hex(0x655D52),
    Color::Hex(0x978D7F), Color::Hex(0xC83A2E), Color::Hex(0x9C6F00), Color::Hex(0xB7831B), Color::Hex(0x000000, 0.14f),
    Color::Hex(0xCFC5B3), Color::Hex(0xA89D89), Color::Hex(0x000000, 0.32f), 0xEAE3D6, true,
};

// スプリング: light, pale cherry-blossom pink, a deep pink accent.
inline constexpr Palette kSpring{
    Color::Hex(0xFBF1F4), Color::Hex(0xF5E4EA), Color::Hex(0xFFFAFB), Color::Hex(0xF8EBEF), Color::Hex(0xE8CFD8),
    Color::Hex(0xD4AFBD), Color::Hex(0xFFFFFF), Color::Hex(0xF3E1E7), Color::Hex(0xEBD3DC), Color::Hex(0xF9D5E1),
    Color::Hex(0xD94F7E), Color::Hex(0xE6668F), Color::Hex(0xB93D67), Color::Hex(0x2E2226), Color::Hex(0x6E5A61),
    Color::Hex(0xA08C93), Color::Hex(0xC8322E), Color::Hex(0x9C6F00), Color::Hex(0xC08A2A), Color::Hex(0x000000, 0.13f),
    Color::Hex(0xE2C6D0), Color::Hex(0xC6A3B0), Color::Hex(0x000000, 0.30f), 0xF5E4EA, true, Ornament::Spring,
};

// サマー: a deep summer sky (blue), a sea-blue accent.
inline constexpr Palette kSummer{
    Color::Hex(0xB4D9F5), Color::Hex(0xA2CDF0), Color::Hex(0xE3F1FC), Color::Hex(0xD0E7FA), Color::Hex(0x96C1E4),
    Color::Hex(0x6EA4D0), Color::Hex(0xF7FBFF), Color::Hex(0xCBE3F7), Color::Hex(0xBAD8F3), Color::Hex(0xA9D0F2),
    Color::Hex(0x1270C0), Color::Hex(0x2685D2), Color::Hex(0x0D5A9C), Color::Hex(0x0F1F2B), Color::Hex(0x435768),
    Color::Hex(0x728CA0), Color::Hex(0xD03030), Color::Hex(0xA87A06), Color::Hex(0xC9921B), Color::Hex(0x000000, 0.14f),
    Color::Hex(0x9DBFDB), Color::Hex(0x789FC2), Color::Hex(0x000000, 0.32f), 0xA2CDF0, true, Ornament::Summer,
};

// フォール: an autumn wood - yellow and orange, a maple-red accent.
inline constexpr Palette kFall{
    Color::Hex(0xF5E2C0), Color::Hex(0xEFD3A6), Color::Hex(0xFFF6E6), Color::Hex(0xF8E8CC), Color::Hex(0xE0C391),
    Color::Hex(0xC9A062), Color::Hex(0xFFFBF2), Color::Hex(0xF4E0BC), Color::Hex(0xEBD2A8), Color::Hex(0xF6CFA0),
    Color::Hex(0xCF4F1C), Color::Hex(0xDE652F), Color::Hex(0xAA3F13), Color::Hex(0x2F2216), Color::Hex(0x6C5640),
    Color::Hex(0x9D876B), Color::Hex(0xC8322E), Color::Hex(0x9C6F00), Color::Hex(0xC08A2A), Color::Hex(0x000000, 0.14f),
    Color::Hex(0xDEC293), Color::Hex(0xC5A36C), Color::Hex(0x000000, 0.32f), 0xEFD3A6, true, Ornament::Fall,
};

// ウィンター: a cold winter blue (so the white of the snow stands out), a steel-blue accent.
inline constexpr Palette kWinter{
    Color::Hex(0xBFD2E6), Color::Hex(0xB0C6DE), Color::Hex(0xDDE8F3), Color::Hex(0xD0DEEC), Color::Hex(0xA2B9D1),
    Color::Hex(0x839EBB), Color::Hex(0xF1F6FB), Color::Hex(0xCFDCEA), Color::Hex(0xC2D3E4), Color::Hex(0xB9CFE6),
    Color::Hex(0x3A6C99), Color::Hex(0x4E7FAA), Color::Hex(0x2D567B), Color::Hex(0x16202B), Color::Hex(0x47566A),
    Color::Hex(0x7689A0), Color::Hex(0xC8322E), Color::Hex(0x9C6F00), Color::Hex(0xB89040), Color::Hex(0x000000, 0.13f),
    Color::Hex(0xA5B9CE), Color::Hex(0x8399B2), Color::Hex(0x000000, 0.30f), 0xB0C6DE, true, Ornament::Winter,
};

// The palette in use (dark until Apply is called).
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
inline Color Dim = kDark.dim;  // behind a dialog
inline uint32_t CaptionRgb = kDark.caption;
inline bool IsLight = false;
inline Ornament CurrentOrnament = Ornament::None;

// One theme of the list. Its id is what hub.json keeps (options.theme).
struct ThemeInfo {
    const char* id;
    const wchar_t* name;     // Japanese (Tr when shown)
    const Palette* palette;  // nullptr: follows Windows (kDark or kLight)
    int group;               // the theme screen draws a line between groups (0 = the basic three, 2 = the seasons)
};
inline constexpr ThemeInfo kThemes[] = {
    {"Dark", TrKey(L"黒"), &kDark, 0},
    {"Light", TrKey(L"白"), &kLight, 0},
    {"System", TrKey(L"PC の設定に合わせる"), nullptr, 0},
    {"Midnight", TrKey(L"深い青"), &kMidnight, 1},
    {"Forest", TrKey(L"森"), &kForest, 1},
    {"Paper", TrKey(L"紙"), &kPaper, 1},
    {"Spring", TrKey(L"スプリング"), &kSpring, 2},
    {"Summer", TrKey(L"サマー"), &kSummer, 2},
    {"Fall", TrKey(L"フォール"), &kFall, 2},
    {"Winter", TrKey(L"ウィンター"), &kWinter, 2},
};
inline std::span<const ThemeInfo> Themes() { return kThemes; }
// An unknown id (a theme of a newer version, a typo in hub.json) is the first theme.
inline const ThemeInfo& FindTheme(std::string_view id) {
    if (id == "Sakura") id = "Spring";  // its name before (a hub.json of then keeps its theme)
    for (const ThemeInfo& t : kThemes) {
        if (id == t.id) return t;
    }
    return kThemes[0];
}
// The palette a theme stands for now (for "PC の設定に合わせる": what Windows uses).
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
    CurrentOrnament = p.ornament;
}

inline constexpr float FontSize = 13.f;
inline constexpr float Radius = 4.f;

// --- Gradients -------------------------------------------------------------------------------
// The flat colors above, lit from above: a little lighter at the top, a little darker at the
// bottom - the same hue, so the palette stays as it is. Every box the widgets draw uses these.

struct Gradient {
    Color top, bottom;
    Gradient Flipped() const { return {bottom, top}; }  // pressed / sunk in
};

// A surface that stands out (buttons, headers, tabs): stronger on the dark theme, where light
// from above shows more.
inline Gradient Raised(Color c) { return IsLight ? Gradient{Shade(c, 0.55f), Shade(c, -0.05f)} : Gradient{Shade(c, 0.07f), Shade(c, -0.12f)}; }
// A large area (panels, the sidebar): the same light, gentler.
inline Gradient Gentle(Color c) { return IsLight ? Gradient{Shade(c, 0.35f), Shade(c, -0.025f)} : Gradient{Shade(c, 0.035f), Shade(c, -0.06f)}; }
// Sunk in (text fields, check boxes): darker at the top, as if lit from above.
inline Gradient Sunken(Color c) { return IsLight ? Gradient{Shade(c, -0.05f), c} : Gradient{Shade(c, -0.25f), Shade(c, 0.03f)}; }
// The accent (primary buttons, the selected item): bright and clear on both themes.
inline Gradient AccentOf(Color c) { return {Shade(c, 0.12f), Shade(c, -0.14f)}; }

// A thin light line inside the top edge of a raised surface (where the light catches it).
inline Color Highlight() { return IsLight ? Color{1, 1, 1, 0.75f} : Color{1, 1, 1, 0.06f}; }
// The edge of a panel, two lines: a dark one just outside (also the 1px seam between docked panels)
// and a thin light one just inside, brighter at the top and faint - but still lighter than the
// background - at the bottom, so every side of the panel can be seen.
inline Color EdgeOuter() { return IsLight ? Color::Hex(0xC4C6CD) : Color::Hex(0x0B0C0E); }
inline Gradient EdgeInner() { return IsLight ? Gradient{Color{1, 1, 1, 1}, Color{1, 1, 1, 0.55f}} : Gradient{Color{1, 1, 1, 0.10f}, Color{1, 1, 1, 0.035f}}; }
inline Color AccentHighlight() { return Color{1, 1, 1, 0.22f}; }
// The theme's ornament: the accent, faint (it lies over the panels, so the text stays readable).
inline Color OrnamentColor() { return Accent.Alpha(IsLight ? 0.10f : 0.12f); }

// The colors of the seasons' pictures (laid faintly over the window: their alpha is part of them).
namespace season {
inline Color Petal() { return Color::Hex(0xEE8FAE, 0.42f); }          // spring: the flurry
inline Color Sky() { return Color::Hex(0x2F8FE0, 0.12f); }            // summer: a blue wash (the panel windows)
inline Color CloudColor() { return Color::Hex(0xFFFFFF, 0.55f); }
inline Color FoamColor() { return Color::Hex(0xFFFFFF, 0.70f); }
inline Color SandColor() { return Color::Hex(0xEFC96B, 0.42f); }
inline Color ParasolA() { return Color::Hex(0xE2483D, 0.30f); }
inline Color ParasolB() { return Color::Hex(0xFFFFFF, 0.55f); }
inline Color PoleColor() { return Color::Hex(0x8C6D4A, 0.30f); }
inline Color Leaf(int i) {                                            // fall: the canopy, four colors
    constexpr uint32_t k[4] = {0xD8432A, 0xE8832A, 0xE7B43A, 0xB0432A};
    return Color::Hex(k[i & 3], 0.34f);
}
inline Color Snow() { return Color::Hex(0xFFFFFF); }                  // winter: piled up at the bottom of panels
inline Color SnowShade() { return Color::Hex(0x8CA3BD); }             // its outline
inline Color SnowLow() { return Color::Hex(0xE6EEF7); }               // the shaded foot of the drift
inline Color Flake() { return Color::Hex(0xFFFFFF, 0.9f); }           // falling snow
inline Color Ice() { return Color::Hex(0xF3F9FF, 0.95f); }            // icicles
inline Color IceShade() { return Color::Hex(0x7F98B4, 0.8f); }
}  // namespace season
// How bright a color looks (relative luminance, 0 = black .. 1 = white; WCAG's formula).
inline float Luminance(Color c) {
    auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}
// White or black, whichever stands out more on `bg` (the higher contrast ratio): for one-color
// pictures like the Hub's logo, so they can be seen on every theme.
inline Color InkOn(Color bg) {
    const float l = Luminance(bg);
    const float onWhite = 1.05f / (l + 0.05f), onBlack = (l + 0.05f) / 0.05f;
    return onWhite >= onBlack ? Color{1, 1, 1, 1} : Color{0, 0, 0, 1};
}
// Text and marks on the accent (always white: the accent is the same red on both themes).
inline Color OnAccent() { return Color{1, 1, 1, 1}; }
// A switch (w::Switch): the track when off, and the round knob (white in every theme, as on a phone).
inline Color SwitchOff() { return IsLight ? Color::Hex(0xD3D6DC) : Color::Hex(0x3B3F48); }
inline Color SwitchKnob() { return Color{1, 1, 1, 1}; }

// --- 3D views (scene view): the floor, the axes, things in the scene -------------------------
inline Color SceneBackground() { return IsLight ? Shade(Bg, -0.07f) : Shade(Field, -0.1f); }
inline Color SceneGrid() { return Text.Alpha(IsLight ? 0.14f : 0.09f); }
inline Color SceneGridMajor() { return Text.Alpha(IsLight ? 0.30f : 0.20f); }
inline Color AxisX() { return Color::Hex(0xE5484D); }  // red, as in every 3D tool
inline Color AxisY() { return Color::Hex(0x5BB974); }  // green
inline Color AxisZ() { return Color::Hex(0x4C8DF6); }  // blue
inline Color GizmoHot() { return Color::Hex(0xF2C230); }  // the gizmo handle under the mouse / being dragged (yellow)
inline Color SceneObject() { return IsLight ? Color::Hex(0xAEB4BF) : Color::Hex(0x98A2B3); }
inline Color SceneObjectSelected() { return Mix(SceneObject(), Accent, 0.55f); }
inline Color SceneObjectInactive() { return Mix(SceneObject(), SceneBackground(), 0.6f); }

}  // namespace rm::editor::theme
