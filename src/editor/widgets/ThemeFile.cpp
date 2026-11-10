#include "editor/widgets/ThemeFile.h"

#include "base/Utf.h"
#include "core/reflect/Data.h"

#include <array>
#include <utility>

namespace rm::editor::theme {

std::optional<Color> ParseHexColor(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8) return std::nullopt;
    uint32_t v = 0;
    for (const char ch : text) {
        int d = -1;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        if (d < 0) return std::nullopt;
        v = v * 16 + static_cast<uint32_t>(d);
    }
    if (text.size() == 6) return Color::Hex(v);
    return Color::Hex(v >> 8, static_cast<float>(v & 0xFF) / 255.f);
}

std::optional<ThemeFromFile> ParseThemeFile(const json::Value& v, std::string* error) {
    auto fail = [&](std::string why) -> std::optional<ThemeFromFile> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    if (!v.IsObject()) return fail("JSON のオブジェクトではありません");
    ThemeFile file;
    reflect::Load(file, v);
    if (file.kind != kThemeFileKind) return fail("\"kind\" が \"rm_engine.theme\" ではありません");
    if (file.id.empty()) return fail("\"id\" がありません");
    if (file.name.empty()) return fail("\"name\" がありません");

    // Each color: the file's, or the built-in theme's of the same kind (light / dark).
    Palette p = file.light ? kLight : kDark;
    const ThemeFileColors& c = file.colors;
    const std::array<std::pair<const std::string*, Color*>, 23> colors{{
        {&c.bg, &p.bg}, {&c.sidebar, &p.sidebar}, {&c.panel, &p.panel}, {&c.panelHeader, &p.panelHeader},
        {&c.border, &p.border}, {&c.borderHover, &p.borderHover}, {&c.field, &p.field}, {&c.hover, &p.hover},
        {&c.pressed, &p.pressed}, {&c.selected, &p.selected}, {&c.accent, &p.accent}, {&c.accentHover, &p.accentHover},
        {&c.accentPressed, &p.accentPressed}, {&c.text, &p.text}, {&c.textDim, &p.textDim}, {&c.textFaint, &p.textFaint},
        {&c.danger, &p.danger}, {&c.warn, &p.warn}, {&c.folder, &p.folder}, {&c.shadow, &p.shadow},
        {&c.scrollThumb, &p.scrollThumb}, {&c.scrollThumbHot, &p.scrollThumbHot}, {&c.dim, &p.dim},
    }};
    for (const auto& [text, color] : colors) {
        if (text->empty()) continue;
        const auto parsed = ParseHexColor(*text);
        if (!parsed) return fail("色を読めません: \"" + *text + "\"");
        *color = *parsed;
    }
    if (!file.caption.empty()) {
        const auto caption = ParseHexColor(file.caption);
        if (!caption || caption->a < 1.f) return fail("\"caption\" を読めません: \"" + file.caption + "\"");
        auto byte = [](float f) { return static_cast<uint32_t>(f * 255.f + 0.5f); };
        p.caption = byte(caption->r) << 16 | byte(caption->g) << 8 | byte(caption->b);
    }
    p.light = file.light;

    ThemeFromFile out;
    out.info.id = file.id;
    out.info.name = Utf8ToWide(file.name);
    out.info.palette = p;
    out.info.aliases = file.aliases;
    out.order = file.order;
    out.ornament = file.ornament;
    return out;
}

}  // namespace rm::editor::theme
