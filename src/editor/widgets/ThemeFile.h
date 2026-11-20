#pragma once
// テーマファイル: テーマパックの 1 つのテーマ (plugins/<author>/<name>/themes/*.json)。ここで 1 度だけ書く:
// 読み込みは記述から来る (憲法 9)。色は "#RRGGBB" か "#RRGGBBAA"。省略した (または空の) 色は、
// 組み込みのダーク / ライトテーマのもの (`light` で決まる)。純粋 - tests/TestTheme.cpp でテストする。
//
//   {
//     "kind": "rm_engine.theme", "format": 1,
//     "id": "Spring",                 選んだとき hub.json に残る (全テーマの中で一意)
//     "name": "スプリング",            日本語。英語はパックの lang/en.json に
//     "aliases": ["Sakura"],          前に保存されていた id (省略可)
//     "order": 1,                     パックの中での位置 (省略可)
//     "light": true,                  ライトテーマ (グラデーションはライト用かダーク用に作る)
//     "ornament": "spring",           パックのコードがこの名前で出す絵 (省略可)
//     "caption": "#F5E4EA",           タイトルバー
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

// ファイルから読んだテーマ: 一覧が要るもの (そのグループ、パック、絵は、パックを読んだ側が
// 埋める)、パックの中での位置、絵の名前。
struct ThemeFromFile {
    ThemeInfo info;
    int order = 0;
    std::string ornament;
};
// テーマファイルでない、id か name がない、色が読めない、のどれかのとき nullopt (+ 理由を *error に)。
std::optional<ThemeFromFile> ParseThemeFile(const json::Value& v, std::string* error);
// "#RRGGBB" / "#RRGGBBAA" (# は付けなくてもよい)。
std::optional<Color> ParseHexColor(std::string_view text);

}  // namespace rm::editor::theme
