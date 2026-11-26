#include "editor/widgets/OrnamentPicture.h"

#include "editor/widgets/Theme.h"
#include "ui/Context.h"

namespace rm::editor::w {

namespace {

bool Same(const ui::Rect& a, const ui::Rect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

const OrnamentPicture& Cached(MakeOrnament make, int kind, const ui::Rect& w, bool floating, const ui::Rect& screen) {
    struct Entry {
        MakeOrnament make;
        int kind;
        ui::Rect w;
        bool floating;
        ui::Rect screen;
        ui::Color accent;  // 影の色はテーマから来る
        OrnamentPicture picture;
    };
    static std::vector<Entry> cache;
    for (const Entry& e : cache) {
        if (e.make == make && e.kind == kind && e.floating == floating && e.accent == theme::Accent && Same(e.w, w) && Same(e.screen, screen)) {
            return e.picture;
        }
    }
    if (cache.size() >= 4) cache.erase(cache.begin());
    cache.push_back({make, kind, w, floating, screen, theme::Accent, make(kind, w, floating, screen)});
    return cache.back().picture;
}

}  // namespace

void DrawOrnamentPicture(ui::Context& c, MakeOrnament make, int kind, const ui::Rect& window, bool floating, const ui::Rect& screen) {
    for (const OrnamentLayer& l : Cached(make, kind, window, floating, screen)) c.P().FillPath(l.path, l.color);
}

}  // namespace rm::editor::w
