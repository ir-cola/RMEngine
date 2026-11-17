#pragma once
// The editor's look. The core UI foundation (src/ui) has no colors on purpose:
// a game UI built on the same foundation must not change when the editor theme does.
//
// The built-in palettes (黒 = dark, the default; 白 = light) and the list of themes (Themes(): what
// オプション > 色のテーマ > 「テーマを変更…」 shows). The colors below are the palette in use; ApplyTheme
// switches them. Every screen reads them while drawing, so a switch shows on the next frame.
//
// Built in: 黒, 白 and PC の設定に合わせる. Every other theme comes from a theme pack: a plugin with
// theme files (plugins/<author>/<name>/themes/*.json, read by ThemeFile.h when the plugins are read;
// a pack's pictures are its code, offered at w::kOrnamentPoint). A new theme = a theme file in a pack.

#include "editor/lang/Lang.h"
#include "ui/Types.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {
struct OrnamentInfo;  // a theme pack's pictures (Widgets.h)
}

namespace rm::editor::theme {

using ui::Color;

struct Palette {
    Color bg, sidebar, panel, panelHeader, border, borderHover, field, hover, pressed, selected;
    Color accent, accentHover, accentPressed, text, textDim, textFaint, danger, warn, folder, shadow;
    Color scrollThumb, scrollThumbHot, dim;
    uint32_t caption;  // title bar (0xRRGGBB)
    bool light;        // a light theme (the gradients below are made for light or dark)
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
inline const w::OrnamentInfo* CurrentOrnament = nullptr;  // the theme's pictures (none for most)

// One theme of the list. Its id is what hub.json keeps (options.theme).
struct ThemeInfo {
    std::string id;
    std::wstring name;               // Japanese (Tr when shown)
    std::optional<Palette> palette;  // none: follows Windows (kDark or kLight)
    int group = 0;                   // 0 = the built-in three, then one number per pack (the theme screen
                                     // starts a new row for each, under the pack's name)
    std::wstring pack;               // the pack's name (its plugin's display name; Tr when shown)
    std::vector<std::string> aliases;           // ids it was saved under before (a hub.json of then keeps it)
    const w::OrnamentInfo* ornament = nullptr;  // its pictures, from the pack's code
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
// The themes of the packs (read again whenever the plugins are), after the built-in three. A
// reference from FindTheme is good until the next call.
inline void SetPackThemes(std::vector<ThemeInfo> themes) {
    auto& list = detail::List();
    list.resize(detail::kBuiltIn);
    for (ThemeInfo& t : themes) list.push_back(std::move(t));
}
// An unknown id (a pack that is not installed, a theme of a newer version, a typo in hub.json) is the
// first theme (App then writes that one to hub.json: installing the pack again does not bring it back).
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
}
// The theme's palette and its pictures.
inline void ApplyTheme(const ThemeInfo& t, bool systemPrefersLight) {
    Apply(PaletteOf(t, systemPrefersLight));
    CurrentOrnament = t.ornament;
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
// A theme pack's silhouettes: the accent, faint (behind the content, on the panels too).
inline Color OrnamentColor() { return Accent.Alpha(IsLight ? 0.10f : 0.12f); }

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
// A camera's shape (the lines of what it sees) and a light's (its direction, its reach) and their marks.
inline Color SceneCamera() { return Text.Alpha(IsLight ? 0.70f : 0.75f); }
inline Color SceneLight() { return IsLight ? Color::Hex(0xC08A12) : Color::Hex(0xF2D16B); }
// The mark of a thing with no shape of its own (an empty entity, パーティクル, 音源): as a camera's.
inline Color SceneMark() { return SceneCamera(); }
// A model is drawn in its own colours (materials and pictures) times this: as it is, or faint and
// see-through when switched off. The selected one glows at its edges instead of being recoloured.
inline Color SceneModel() { return Color{1, 1, 1, 1}; }
inline Color SceneModelInactive() { return Color{1, 1, 1, 0.35f}; }
inline Color SceneSelectedGlow() { return Accent.Alpha(0.85f); }
// Behind a model's thumbnail (アセット panel): a faint square so a light or dark model shows on both themes.
inline Color ThumbnailBack() { return Text.Alpha(IsLight ? 0.06f : 0.08f); }
// Shadows falling on the floor (laid over the background: darker on the dark theme to still show).
inline Color SceneShadow() { return Color{0, 0, 0, IsLight ? 0.30f : 0.55f}; }
// Physics shapes drawn over the scene (物理: colliders, characters): a solid one, a trigger (things pass
// through it - it only tells), and one switched off (faint).
inline Color SceneCollider() { return IsLight ? Color::Hex(0x1F9D55) : Color::Hex(0x5EE08F); }
inline Color SceneTrigger() { return IsLight ? Color::Hex(0x8E44C9) : Color::Hex(0xC79BF2); }
inline Color SceneColliderInactive() { return Mix(SceneCollider(), SceneBackground(), 0.6f); }

// --- A software running (a game in Play): its tab row is tinted with the accent -----------------
inline Color RunningBar() { return Mix(Bg, Accent, IsLight ? 0.22f : 0.28f); }

}  // namespace rm::editor::theme
