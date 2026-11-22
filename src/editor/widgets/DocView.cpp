#include "editor/widgets/DocView.h"

#include "base/Utf.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"

#include <algorithm>
#include <format>
#include <utility>

namespace rm::editor::w {

using help::Block;
using help::Span;
using ui::Rect;

namespace {

constexpr float kBody = 13.5f;
constexpr float kGap = 8.f;  // ブロックの間

// 行の中の書式を、1 本の文字と書式の範囲にする。リンクは (範囲, リンク先) も。
struct Rich {
    std::wstring text;
    std::vector<ui::TextRun> runs;
    std::vector<std::pair<ui::TextRun, std::string>> links;
};

Rich Build(const std::vector<Span>& spans, bool allBold) {
    Rich r;
    for (const Span& s : spans) {
        ui::TextRun run;
        run.start = static_cast<uint32_t>(r.text.size());
        run.length = static_cast<uint32_t>(s.text.size());
        run.bold = allBold || s.kind == Span::Kind::Bold;
        r.text += s.text;
        if (s.kind == Span::Kind::Code) {
            run.mono = true;
            run.color = theme::Accent.Alpha(0.9f);
        } else if (s.kind == Span::Kind::Link) {
            run.underline = true;
            run.color = theme::Accent;
            r.links.push_back({run, s.target});
        }
        if (run.bold || run.mono || run.underline || run.color) r.runs.push_back(run);
    }
    return r;
}

struct Ctx {
    ui::Context& c;
    std::string_view id;
    const Rect& visible;
    std::string* clicked;
};

bool Visible(const Ctx& k, float y, float h) { return y + h >= k.visible.y && y <= k.visible.Bottom(); }

// 折り返す文 1 つ: 描いて (見えていれば)、リンクを押したかを見て、高さを返す。
float Text(Ctx& k, const Rect& r, const Rich& rich, float size, ui::Color color) {
    auto& p = k.c.P();
    const float h = p.RichText(r, rich.text, size, color, rich.runs, false);
    if (!Visible(k, r.y, h)) return h;
    p.RichText(r, rich.text, size, color, rich.runs, true);
    if (!rich.links.empty() && k.c.IsHovered({r.x, r.y, r.w, h})) {
        const ui::Input& in = k.c.In();
        if (const auto at = p.RichHitTest(r, rich.text, size, rich.runs, in.mouseX, in.mouseY)) {
            for (const auto& [run, target] : rich.links) {
                if (*at >= run.start && *at < run.start + run.length) {
                    k.c.SetCursor(ui::Cursor::Hand);
                    if (k.c.MousePressed() && k.clicked) *k.clicked = target;
                }
            }
        }
    }
    return h;
}

float HeadingSize(int level) { return level <= 1 ? 22.f : level == 2 ? 17.f : 14.5f; }

}  // namespace

float DocView(ui::Context& c, std::string_view id, float x, float y, float width, const Rect& visible, const help::Doc& doc,
              const std::function<DocImage(const std::string&)>& image, std::string* clicked) {
    auto& p = c.P();
    Ctx k{c, id, visible, clicked};
    const float top = y;
    width = std::max(width, 40.f);
    for (size_t bi = 0; bi < doc.blocks.size(); ++bi) {
        const Block& b = doc.blocks[bi];
        switch (b.kind) {
            case Block::Kind::Heading: {
                const float size = HeadingSize(b.level);
                if (bi > 0) y += b.level <= 2 ? 14.f : 8.f;
                y += Text(k, {x, y, width, 0}, Build(b.spans, true), size, theme::Text);
                if (b.level <= 2) {  // (大きな見出しの下に線)
                    y += 4;
                    if (Visible(k, y, 1)) p.Line(x, y, x + width, y, theme::Border, 1.f);
                    y += 1;
                }
                y += kGap;
                break;
            }
            case Block::Kind::Paragraph:
                y += Text(k, {x, y, width, 0}, Build(b.spans, false), kBody, theme::Text) + kGap;
                break;
            case Block::Kind::Bullet:
            case Block::Kind::Number: {
                const float indent = 6.f + static_cast<float>(b.level) * 18.f;
                const std::wstring mark = b.kind == Block::Kind::Bullet ? (b.level == 0 ? L"•" : L"◦") : std::format(L"{}.", b.number);
                const float markW = b.kind == Block::Kind::Bullet ? 14.f : 22.f;
                const float h = Text(k, {x + indent + markW, y, width - indent - markW, 0}, Build(b.spans, false), kBody, theme::Text);
                if (Visible(k, y, h)) {
                    ui::TextStyle ts;
                    ts.size = kBody;
                    ts.color = theme::TextDim;
                    ts.v = ui::VAlign::Top;
                    p.Text({x + indent, y, markW, h}, mark, ts);
                }
                // (続く項目の間は狭く、箇条書きが終わる所は段落と同じ)
                const bool more = bi + 1 < doc.blocks.size() &&
                                  (doc.blocks[bi + 1].kind == Block::Kind::Bullet || doc.blocks[bi + 1].kind == Block::Kind::Number);
                y += h + (more ? 3.f : kGap);
                break;
            }
            case Block::Kind::Note: {
                const Rich rich = Build(b.spans, false);
                const float pad = 10.f;
                const float th = p.RichText({x + pad + 4, y, width - pad * 2 - 4, 0}, rich.text, kBody, theme::Text, rich.runs, false);
                const Rect box{x, y, width, th + pad * 2};
                if (Visible(k, box.y, box.h)) {
                    DrawSurface(c, box, Surface::Panel);
                    p.FillRect({box.x, box.y, 3, box.h}, theme::Accent);
                }
                Text(k, {x + pad + 4, y + pad, width - pad * 2 - 4, 0}, rich, kBody, theme::Text);
                y += box.h + kGap;
                break;
            }
            case Block::Kind::Code: {
                const float pad = 8.f;
                ui::TextRun mono;
                mono.length = static_cast<uint32_t>(b.code.size());
                mono.mono = true;
                const ui::TextRun runs[] = {mono};
                const float th = std::max(p.RichText({x + pad, y, width - pad * 2, 0}, b.code, 12.5f, theme::Text, runs, false), 14.f);
                const Rect box{x, y, width, th + pad * 2};
                if (Visible(k, box.y, box.h)) {
                    DrawSurface(c, box, Surface::Sunken);
                    p.RichText({x + pad, y + pad, width - pad * 2, 0}, b.code, 12.5f, theme::Text, runs, true);
                }
                y += box.h + kGap;
                break;
            }
            case Block::Kind::Table: {
                size_t cols = 0;
                for (const auto& row : b.rows) cols = std::max(cols, row.size());
                if (cols == 0) break;
                const float pad = 6.f, colW = width / static_cast<float>(cols);
                std::vector<float> heights;
                for (size_t ri = 0; ri < b.rows.size(); ++ri) {
                    float h = 0;
                    for (const auto& cell : b.rows[ri]) {
                        const Rich rich = Build(cell, ri == 0);
                        h = std::max(h, p.RichText({0, 0, colW - pad * 2, 0}, rich.text, kBody - 0.5f, theme::Text, rich.runs, false));
                    }
                    heights.push_back(std::max(h, 16.f) + pad * 2);
                }
                float total = 0;
                for (const float h : heights) total += h;
                const Rect box{x, y, width, total};
                if (Visible(k, box.y, box.h)) {
                    DrawSurface(c, box, Surface::Panel);
                    DrawSurface(c, {x, y, width, heights[0]}, Surface::Raised, 0.f);  // (見出しの行)
                }
                float ry = y;
                for (size_t ri = 0; ri < b.rows.size(); ++ri) {
                    for (size_t ci = 0; ci < b.rows[ri].size(); ++ci) {
                        const Rich rich = Build(b.rows[ri][ci], ri == 0);
                        Text(k, {x + static_cast<float>(ci) * colW + pad, ry + pad, colW - pad * 2, 0}, rich, kBody - 0.5f, theme::Text);
                    }
                    ry += heights[ri];
                    if (ri + 1 < b.rows.size() && Visible(k, ry, 1)) p.Line(x, ry, x + width, ry, theme::Border, 1.f);
                }
                if (Visible(k, box.y, box.h)) {
                    for (size_t ci = 1; ci < cols; ++ci) {
                        const float lx = x + static_cast<float>(ci) * colW;
                        p.Line(lx, y, lx, y + total, theme::Border, 1.f);
                    }
                }
                y += total + kGap;
                break;
            }
            case Block::Kind::Image: {
                const DocImage img = image ? image(b.image) : DocImage{};
                ui::TextStyle ts;
                ts.size = 12;
                ts.color = theme::TextDim;
                ts.wrap = true;
                ts.v = ui::VAlign::Top;
                if (img.bitmap && img.width > 0 && img.height > 0) {
                    const float scale = std::min(1.f, width / img.width);
                    const Rect dst{x, y, img.width * scale, img.height * scale};
                    if (Visible(k, dst.y, dst.h)) p.Image(img.bitmap, dst);
                    y += dst.h + 4;
                } else {
                    const Rect box{x, y, width, 36};
                    if (Visible(k, box.y, box.h)) {
                        DrawSurface(c, box, Surface::Sunken);
                        ts.v = ui::VAlign::Center;
                        ts.wrap = false;
                        p.Text(box.Inset(10, 0, 10, 0), Utf8ToWide(b.image), ts);
                    }
                    y += box.h + 4;
                }
                if (!b.alt.empty()) {
                    ts.v = ui::VAlign::Top;
                    ts.wrap = true;
                    const float h = p.TextHeight(b.alt, ts.size, width);
                    if (Visible(k, y, h)) p.Text({x, y, width, h + 2}, b.alt, ts);
                    y += h;
                }
                y += kGap;
                break;
            }
            case Block::Kind::Rule:
                y += 6;
                if (Visible(k, y, 1)) p.Line(x, y, x + width, y, theme::Border, 1.f);
                y += 7 + kGap;
                break;
        }
    }
    return y - top;
}

}  // namespace rm::editor::w
