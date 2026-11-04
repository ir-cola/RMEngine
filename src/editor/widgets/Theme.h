#pragma once
// The editor's look. The core UI foundation (src/ui) has no colors on purpose:
// a game UI built on the same foundation must not change when the editor theme does.

#include "ui/Types.h"

namespace rm::editor::theme {

using ui::Color;

inline constexpr Color Bg = Color::Hex(0x1B1C1F);
inline constexpr Color Sidebar = Color::Hex(0x151618);
inline constexpr Color Panel = Color::Hex(0x232428);
inline constexpr Color PanelHeader = Color::Hex(0x2A2B30);
inline constexpr Color Border = Color::Hex(0x34363C);
inline constexpr Color BorderHover = Color::Hex(0x4A4D55);
inline constexpr Color Field = Color::Hex(0x141518);
inline constexpr Color Hover = Color::Hex(0x2D2F35);
inline constexpr Color Pressed = Color::Hex(0x26282D);
inline constexpr Color Selected = Color::Hex(0x3B2326);
inline constexpr Color Accent = Color::Hex(0xD62828);
inline constexpr Color AccentHover = Color::Hex(0xE84444);
inline constexpr Color AccentPressed = Color::Hex(0xB31F1F);
inline constexpr Color Text = Color::Hex(0xE8E8EA);
inline constexpr Color TextDim = Color::Hex(0x9EA1A9);
inline constexpr Color TextFaint = Color::Hex(0x6B6E76);
inline constexpr Color Danger = Color::Hex(0xFF6B6B);
inline constexpr Color Warn = Color::Hex(0xF2C14E);
inline constexpr Color Folder = Color::Hex(0xE3B341);
inline constexpr Color Shadow = Color::Hex(0x000000, 0.35f);

inline constexpr float FontSize = 13.f;
inline constexpr float Radius = 4.f;

}  // namespace rm::editor::theme
