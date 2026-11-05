#pragma once
// The editor's look. The core UI foundation (src/ui) has no colors on purpose:
// a game UI built on the same foundation must not change when the editor theme does.
//
// Two palettes, 黒 (dark, the default) and 白 (light). The colors below are the palette in use;
// Apply switches them (オプション > 色のテーマ, or Windows' own setting when "PC の設定に合わせる").
// Every screen reads them while drawing, so a switch shows on the next frame.

#include "ui/Types.h"

#include <cstdint>

namespace rm::editor::theme {

using ui::Color;

struct Palette {
    Color bg, sidebar, panel, panelHeader, border, borderHover, field, hover, pressed, selected;
    Color accent, accentHover, accentPressed, text, textDim, textFaint, danger, warn, folder, shadow;
    Color scrollThumb, scrollThumbHot, dim;
    uint32_t caption;  // title bar (0xRRGGBB)
};

inline constexpr Palette kDark{
    Color::Hex(0x1B1C1F), Color::Hex(0x151618), Color::Hex(0x232428), Color::Hex(0x2A2B30), Color::Hex(0x34363C),
    Color::Hex(0x4A4D55), Color::Hex(0x141518), Color::Hex(0x2D2F35), Color::Hex(0x26282D), Color::Hex(0x3B2326),
    Color::Hex(0xD62828), Color::Hex(0xE84444), Color::Hex(0xB31F1F), Color::Hex(0xE8E8EA), Color::Hex(0x9EA1A9),
    Color::Hex(0x6B6E76), Color::Hex(0xFF6B6B), Color::Hex(0xF2C14E), Color::Hex(0xE3B341), Color::Hex(0x000000, 0.35f),
    Color::Hex(0x45474E), Color::Hex(0x6A6D75), Color::Hex(0x000000, 0.55f), 0x151618,
};

inline constexpr Palette kLight{
    Color::Hex(0xF3F3F5), Color::Hex(0xE8E8EC), Color::Hex(0xFFFFFF), Color::Hex(0xEDEDF1), Color::Hex(0xD3D5DB),
    Color::Hex(0xB2B5BE), Color::Hex(0xFFFFFF), Color::Hex(0xE3E4E9), Color::Hex(0xD8D9DF), Color::Hex(0xF8DADC),
    Color::Hex(0xD62828), Color::Hex(0xE84444), Color::Hex(0xB31F1F), Color::Hex(0x1D1E23), Color::Hex(0x585B64),
    Color::Hex(0x8C8F98), Color::Hex(0xD03030), Color::Hex(0xA87A06), Color::Hex(0xC9921B), Color::Hex(0x000000, 0.14f),
    Color::Hex(0xC2C4CB), Color::Hex(0x9EA1AA), Color::Hex(0x000000, 0.35f), 0xE8E8EC,
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

inline void Apply(bool light) {
    const Palette& p = light ? kLight : kDark;
    Bg = p.bg, Sidebar = p.sidebar, Panel = p.panel, PanelHeader = p.panelHeader, Border = p.border;
    BorderHover = p.borderHover, Field = p.field, Hover = p.hover, Pressed = p.pressed, Selected = p.selected;
    Accent = p.accent, AccentHover = p.accentHover, AccentPressed = p.accentPressed, Text = p.text;
    TextDim = p.textDim, TextFaint = p.textFaint, Danger = p.danger, Warn = p.warn, Folder = p.folder, Shadow = p.shadow;
    ScrollThumb = p.scrollThumb, ScrollThumbHot = p.scrollThumbHot, Dim = p.dim, CaptionRgb = p.caption;
    IsLight = light;
}

inline constexpr float FontSize = 13.f;
inline constexpr float Radius = 4.f;

}  // namespace rm::editor::theme
