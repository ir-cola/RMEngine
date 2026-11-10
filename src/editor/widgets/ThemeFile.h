#pragma once
// A theme file: one theme of a theme pack (plugins/<author>/<name>/themes/*.json). Written once here:
// reading comes from the description (rule 9). The colors are "#RRGGBB" or "#RRGGBBAA"; one left out
// (or empty) is the built-in dark / light theme's (by `light`). Pure - tested in tests/TestTheme.cpp.
//
//   {
//     "kind": "rm_engine.theme", "format": 1,
//     "id": "Spring",                 kept in hub.json when chosen (unique among all themes)
//     "name": "スプリング",            Japanese; English in the pack's lang/en.json
//     "aliases": ["Sakura"],          ids it was saved under before (optional)
//     "order": 1,                     its place in the pack (optional)
//     "light": true,                  a light theme (the gradients are made for light or dark)
//     "ornament": "spring",           pictures the pack's code offers under this name (optional)
//     "caption": "#F5E4EA",           the title bar
//     "colors": { "bg": "#FBF1F4", "sidebar": ..., "shadow": "#00000021", ... }
//   }

#include "base/Json.h"
#include "core/reflect/Reflect.h"
#include "editor/widgets/Theme.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::theme {

RM_STRUCT()
struct ThemeFileColors {
    RM_PROPERTY() std::string bg;
    RM_PROPERTY() std::string sidebar;
    RM_PROPERTY() std::string panel;
    RM_PROPERTY() std::string panelHeader;
    RM_PROPERTY() std::string border;
    RM_PROPERTY() std::string borderHover;
    RM_PROPERTY() std::string field;
    RM_PROPERTY() std::string hover;
    RM_PROPERTY() std::string pressed;
    RM_PROPERTY() std::string selected;
    RM_PROPERTY() std::string accent;
    RM_PROPERTY() std::string accentHover;
    RM_PROPERTY() std::string accentPressed;
    RM_PROPERTY() std::string text;
    RM_PROPERTY() std::string textDim;
    RM_PROPERTY() std::string textFaint;
    RM_PROPERTY() std::string danger;
    RM_PROPERTY() std::string warn;
    RM_PROPERTY() std::string folder;
    RM_PROPERTY() std::string shadow;
    RM_PROPERTY() std::string scrollThumb;
    RM_PROPERTY() std::string scrollThumbHot;
    RM_PROPERTY() std::string dim;
};

RM_STRUCT()
struct ThemeFile {
    RM_PROPERTY() std::string kind;
    RM_PROPERTY() int format = 1;
    RM_PROPERTY() std::string id;
    RM_PROPERTY() std::string name;
    RM_PROPERTY() std::vector<std::string> aliases;
    RM_PROPERTY() int order = 0;
    RM_PROPERTY() bool light = false;
    RM_PROPERTY() std::string ornament;
    RM_PROPERTY() std::string caption;
    RM_PROPERTY() ThemeFileColors colors;
};

inline constexpr char kThemeFileKind[] = "rm_engine.theme";

// A theme read from a file: what the list needs (its group, pack and pictures are filled in by the
// one who read the pack), its place in the pack and the name of its pictures.
struct ThemeFromFile {
    ThemeInfo info;
    int order = 0;
    std::string ornament;
};
// nullopt (+ why in *error) when it is not a theme file, has no id or name, or a color cannot be read.
std::optional<ThemeFromFile> ParseThemeFile(const json::Value& v, std::string* error);
// "#RRGGBB" / "#RRGGBBAA" (the # may be left out).
std::optional<Color> ParseHexColor(std::string_view text);

}  // namespace rm::editor::theme
