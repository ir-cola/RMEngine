#include "rm_engine/game_x_tilemap/editor/TilePanel.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "editor/assets/AssetContentCommand.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_x_2d/editor/EditorImages2D.h"
#include "rm_engine/game_x_tilemap/editor/TileBrush.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"
#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/tilemap/runtime/TileCollision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rm::game_x_tilemap {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
namespace inspector = editor::inspector;
using editor::Tr;
using editor::TrF;
using editor::TrU8;
using ui::Rect;

constexpr float kPad = 8, kRow = 28, kGap = 6;
constexpr size_t kMaxSourceTabs = 8;  // 絵の元をこれより多く持つと、タブの代わりに ◀ ▶ で選ぶ

void Note(ui::Context& c, const Rect& r, std::wstring_view text, ui::Color color = theme::TextDim) {
    ui::TextStyle ts;
    ts.wrap = true;
    ts.size = 13;
    ts.color = color;
    ts.v = ui::VAlign::Top;
    c.P().Text(r, text, ts);
}

void Label(ui::Context& c, const Rect& r, std::wstring_view text, ui::Color color = theme::TextDim, ui::HAlign h = ui::HAlign::Left) {
    ui::TextStyle ts;
    ts.size = 13;
    ts.color = color;
    ts.h = h;
    ts.v = ui::VAlign::Center;
    c.P().Text(r, text, ts);
}

std::wstring SourceName(const tilemap::TileSource& s) {
    return s.name.empty() ? TrF(L"絵の元 {}", s.id) : Utf8ToWide(s.name);
}

class TilePanel final : public game_production::SceneView {
public:
    void Draw(ui::Context& c, const Rect& r, game_production::SceneViewHost& host) override {
        if (r.w < 60 || r.h < 60) return;
        now_ = c.Time();
        images_.Begin(now_, host.host.Graphics().Generation(), "パネル「タイル」");
        TileBrush& brush = Brush();
        float y = r.y + kPad;
        const float x = r.x + kPad, wd = r.w - 2 * kPad;
        // パレット | タイルセット
        const std::wstring palette = Tr(L"パレット"), tileset = Tr(L"タイルセット");
        const std::array<std::wstring_view, 2> pages{palette, tileset};
        const float segW = std::min(wd, w::SegmentedWidth(c, pages));
        if (const int i = w::Segmented(c, "tiles_page", {x, y, segW, kRow}, pages, brush.state.page); i >= 0) brush.state.page = i;
        const int layerId = TargetLayer(host);
        const TileLayer* layer = layerId ? LayerOf(host.scene, layerId) : nullptr;
        if (layer && !layer->tileset.Empty()) {
            const std::filesystem::path file = host.host.AssetFile(layer->tileset);
            Label(c, {x + segW + kGap, y, wd - segW - kGap, kRow}, file.empty() ? Tr(L"(見つからないタイルセット)") : file.filename().wstring(), theme::TextDim,
                  ui::HAlign::Right);
        }
        y += kRow + kGap * 2;
        const Rect body{x, y, wd, r.Bottom() - kPad - y};
        if (!layer) {
            Note(c, body, Tr(L"タイルの層を選んでください。階層の右クリック「追加 ▸ 2D オブジェクト ▸ タイルマップ」で、グリッドと層 1 つを作れます。"));
            return;
        }
        if (layer->tileset.Empty()) {
            Note(c, body, Tr(L"この層にはタイルセットがありません。アセットのパネルの右クリック「作成 ▸ タイルセット」で作り、インスペクターの「タイルセット」の欄に入れてください。"));
            return;
        }
        const tilemap::Tileset* set = reader_.Get(host.host, layer->tileset, now_);
        if (!set) {
            Note(c, body, Tr(L"タイルセットを読めません (コンソールに理由が出ています)。"), theme::Warn);
            return;
        }
        if (brush.state.page == 1) {
            DrawTileset(c, body, host, layer->tileset, *set);
        } else {
            DrawPalette(c, body, host, layer->tileset, *set);
        }
    }

    void LoadState(const json::Value& s) override {
        Brush().state = {};
        unknowns_ = {};
        reflect::Warnings warnings;
        if (s.IsObject()) reflect::Load(Brush().state, s, &unknowns_, &warnings);
        for (const auto& wtext : warnings) log::Warn("パネル「タイル」の見え方: {}", wtext);
    }
    json::Value SaveState() const override { return reflect::Save(Brush().state, &unknowns_); }

private:
    // 絵の元を選ぶ帯 (タブ、多ければ ◀ 名前 ▶)。下の y を返す。`chosen` は選んだ絵の元の番号。
    float SourceBar(ui::Context& c, const Rect& r, std::string_view id, const tilemap::Tileset& set, int& chosen) {
        if (set.sources.empty()) return r.y;
        if (!tilemap::FindSource(set, chosen)) chosen = set.sources.front().id;
        size_t at = 0;
        for (size_t i = 0; i < set.sources.size(); ++i) {
            if (set.sources[i].id == chosen) at = i;
        }
        if (set.sources.size() <= kMaxSourceTabs) {
            std::vector<std::wstring> names;
            for (const auto& s : set.sources) names.push_back(SourceName(s));
            std::vector<std::wstring_view> views(names.begin(), names.end());
            const float segW = std::min(r.w, w::SegmentedWidth(c, views));
            if (const int i = w::Segmented(c, id, {r.x, r.y, segW, kRow}, views, static_cast<int>(at)); i >= 0) chosen = set.sources[static_cast<size_t>(i)].id;
        } else {
            const std::string prev = std::string(id) + "_prev", next = std::string(id) + "_next";
            if (w::IconButton(c, prev, {r.x, r.y, kRow, kRow}, w::Icon::ChevronLeft)) chosen = set.sources[(at + set.sources.size() - 1) % set.sources.size()].id;
            Label(c, {r.x + kRow + 4, r.y, std::min(r.w - 2 * kRow - 8, 220.f), kRow}, TrF(L"{} ({} / {})", SourceName(set.sources[at]), at + 1, set.sources.size()), theme::Text);
            if (w::IconButton(c, next, {r.x + std::min(r.w - kRow, 2 * kRow + 228.f), r.y, kRow, kRow}, w::Icon::ChevronRight)) {
                chosen = set.sources[(at + 1) % set.sources.size()].id;
            }
        }
        return r.y + kRow + kGap;
    }

    // 絵の元の画像を、切った格子とその選択とともに、スクロールできる四角に描く。選んだら true。
    bool ImageGrid(ui::Context& c, const Rect& view, std::string_view id, game_production::SceneViewHost& host, const tilemap::TileSource& src, float zoom,
                   w::TileSelection& sel, bool pick, std::span<const w::TileMark> marks = {}) {
        const game_x_2d::Image2D* img = images_.Get(c, src.image, host.host);
        if (src.image.Empty()) {
            Note(c, view, Tr(L"この絵の元には画像がありません。「タイルセット」で画像を入れてください。"));
            return false;
        }
        if (!img || !img->bitmap) {
            Note(c, view, Tr(L"画像を読んでいます…"));
            return false;
        }
        const space2d::GridCut cut = tilemap::CutOf(src);
        const space2d::GridCount n = space2d::GridCountOf(img->width, img->height, cut);
        w::TileGridCut g;
        g.columns = n.columns;
        g.rows = n.rows;
        g.cellW = static_cast<float>(cut.cellWidth);
        g.cellH = static_cast<float>(cut.cellHeight);
        g.margin = static_cast<float>(cut.margin);
        g.spacing = static_cast<float>(cut.spacing);
        g.zoom = zoom;
        const float iw = static_cast<float>(img->width) * zoom, ih = static_cast<float>(img->height) * zoom;
        const std::string scroll = std::string(id) + "_scroll";
        const float off = w::BeginScroll(c, scroll, view, ih + 8);
        c.P().PushClip(view);
        const bool picked = w::TilePalette(c, id, {view.x, view.y - off, iw, ih}, img->bitmap, src.crisp || img->sharp, g, sel, pick, marks);
        c.P().PopClip();
        w::EndScroll(c, scroll, view, ih + 8);
        if (n.columns == 0 || n.rows == 0) Note(c, {view.x, view.y + std::min(ih, view.h - 40) + 4, view.w, 40}, Tr(L"この切り方では、画像にタイルが 1 つも入りません。"), theme::Warn);
        return picked;
    }

    // --- パレット ---------------------------------------------------------------------------
    void DrawPalette(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const AssetRef& ref, const tilemap::Tileset& set) {
        TileBrush& brush = Brush();
        if (set.sources.empty()) {
            Note(c, r, Tr(L"このタイルセットには絵の元がありません。「タイルセット」で絵の元を足して、画像を入れてください。"));
            return;
        }
        int chosen = brush.state.tileset == ref.guid ? brush.state.source : 0;
        float y = SourceBar(c, {r.x, r.y, r.w - 3 * kRow - 12, kRow}, "tiles_source", set, chosen);
        // 拡大 − / +
        const float zx = r.Right() - 3 * kRow - 4;
        if (w::IconButton(c, "tiles_zoom_out", {zx, r.y, kRow, kRow}, w::Icon::Dash)) brush.state.zoom = std::max(0.25f, brush.state.zoom / 2);
        Label(c, {zx + kRow, r.y, kRow, kRow}, std::format(L"{}x", brush.state.zoom), theme::TextDim, ui::HAlign::Center);
        if (w::IconButton(c, "tiles_zoom_in", {zx + 2 * kRow, r.y, kRow, kRow}, w::Icon::Plus)) brush.state.zoom = std::min(16.f, brush.state.zoom * 2);
        if (y == r.y) y += kRow + kGap;
        const tilemap::TileSource* src = tilemap::FindSource(set, chosen);
        if (!src) return;
        const bool mine = brush.state.tileset == ref.guid && brush.state.source == src->id && brush.picked.Empty();
        w::TileSelection sel = mine ? w::TileSelection{brush.state.column, brush.state.row, brush.state.width, brush.state.height} : w::TileSelection{};
        if (chosen != brush.state.source || brush.state.tileset != ref.guid) {  // (別の絵の元に切り替えた: そちらを選ぶ準備)
            brush.state.tileset = ref.guid;
            brush.state.source = chosen;
            brush.state.width = brush.state.height = 0;
        }
        const float noteH = 22;
        const Rect view{r.x, y, r.w, r.Bottom() - y - noteH};
        if (ImageGrid(c, view, "tiles_palette", host, *src, brush.state.zoom, sel, true)) {
            brush.state.tileset = ref.guid;
            brush.state.source = src->id;
            brush.state.column = sel.column;
            brush.state.row = sel.row;
            brush.state.width = sel.width;
            brush.state.height = sel.height;
            brush.picked = {};
        }
        std::wstring line;
        if (!brush.picked.Empty() && brush.state.tileset == ref.guid) {
            line = TrF(L"スポイトで取ったスタンプ ({} x {}) で塗ります。パレットをクリックすると、そちらに戻ります", brush.picked.width, brush.picked.height);
        } else if (brush.state.width > 0) {
            line = TrF(L"選んだタイル: {} 列 {} 行から {} x {}。シーンビューの筆 (B) か四角 (U) で塗ります", brush.state.column, brush.state.row, brush.state.width, brush.state.height);
        } else {
            line = Tr(L"塗るタイルをクリック (ドラッグで四角に選ぶとスタンプ)");
        }
        Label(c, {r.x, r.Bottom() - noteH, r.w, noteH}, line);
    }

    // --- タイルセット: 切り方 | タイル | 独自データの欄 --------------------------------------------------
    void DrawTileset(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const AssetRef& ref, const tilemap::Tileset& set) {
        TileBrush& brush = Brush();
        const std::wstring cut = Tr(L"切り方"), tiles = Tr(L"タイル"), fields = Tr(L"独自データの欄");
        const std::array<std::wstring_view, 3> pages{cut, tiles, fields};
        const std::wstring cutTip = Tr(L"絵の元 (画像) を足す・消す、タイルの大きさ・余白・間"), tilesTip = Tr(L"タイルごとの当たりの形 (絵の上に塗る)・名前・タグ・独自データ"),
                           fieldsTip = Tr(L"タイルに持たせる値の名前と種類 (かたさ・移動コスト など)");
        const std::array<std::wstring_view, 3> tips{cutTip, tilesTip, fieldsTip};
        const float segW = std::min(r.w, w::SegmentedWidth(c, pages));
        if (const int i = w::Segmented(c, "tiles_tileset_page", {r.x, r.y, segW, kRow}, pages, brush.state.tilesetPage, tips); i >= 0) brush.state.tilesetPage = i;
        const Rect body{r.x, r.y + kRow + kGap, r.w, r.h - kRow - kGap};
        if (brush.state.tilesetPage == 1) {
            DrawTiles(c, body, host, ref, set);
        } else if (brush.state.tilesetPage == 2) {
            DrawFields(c, body, host, ref, set);
        } else {
            DrawCut(c, body, host, ref, set);
        }
    }

    // 切り方: 絵の元を足す・消す、選んだ絵の元の欄、絵の上の格子。
    void DrawCut(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const AssetRef& ref, const tilemap::Tileset& set) {
        const bool canEdit = !host.playing;
        float y = r.y;
        // 絵の元を足す / 消す
        const std::wstring add = Tr(L"絵の元を足す"), remove = Tr(L"この絵の元を消す");
        const float addW = w::ButtonWidth(c, add), removeW = w::ButtonWidth(c, remove);
        if (w::Button(c, "tiles_source_add", {r.x, y, addW, kRow}, add, w::ButtonKind::Secondary, canEdit && tilemap::NewSourceId(set) != 0)) {
            int made = 0;
            EditTileset(host, ref, "絵の元を足す", 0, [&](tilemap::Tileset& t) {
                tilemap::TileSource s;
                s.id = made = tilemap::NewSourceId(t);
                s.name = TrU8("絵の元") + " " + std::to_string(s.id);
                t.sources.push_back(std::move(s));
            });
            if (made) editSource_ = made;
        }
        const bool any = !set.sources.empty();
        if (w::Button(c, "tiles_source_remove", {r.x + addW + kGap, y, removeW, kRow}, remove, w::ButtonKind::Secondary, canEdit && any)) {
            const int id = editSource_;
            EditTileset(host, ref, "絵の元を消す", 0, [id](tilemap::Tileset& t) { std::erase_if(t.sources, [id](const tilemap::TileSource& s) { return s.id == id; }); });
        }
        y += kRow + kGap;
        if (!any) {
            Note(c, {r.x, y, r.w, r.Bottom() - y}, Tr(L"「絵の元を足す」で、タイルを並べた画像を 1 つ足してください。"));
            return;
        }
        y = SourceBar(c, {r.x, y, r.w, kRow}, "tiles_edit_source", set, editSource_);
        const tilemap::TileSource* src = tilemap::FindSource(set, editSource_);
        if (!src) return;
        // 選んだ絵の元の欄: インスペクターと同じ描き方 (変更は Edit で返る: 中身の Undo の 1 件ずつ)。
        sourceCopy_ = *src;
        inspector::Block b;
        b.type = &reflect::TypeOf<tilemap::TileSource>();
        b.object = &sourceCopy_;
        b.title = TrF(L"絵の元「{}」", SourceName(*src));
        b.readOnly = !canEdit;
        std::vector<inspector::Block> blocks;
        blocks.push_back(std::move(b));
        inspector::Refs refs;
        host.host.FillInspectorRefs(refs);
        const float inspH = std::min(r.Bottom() - y, 270.f);
        if (const auto edit = inspector::Draw(c, inspector_, {r.x, y, r.w, inspH}, blocks, {}, {}, &refs); edit && canEdit) {
            const int id = src->id;
            EditTileset(host, ref, edit->label, edit->merge, [&](tilemap::Tileset& t) {
                for (tilemap::TileSource& s : t.sources) {
                    if (s.id != id) continue;
                    const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<tilemap::TileSource>(), &s, nullptr, edit->path);
                    if (pt.field) reflect::LoadField(*pt.field, pt.owner, edit->after, nullptr, nullptr);
                    break;
                }
            });
            c.RequestRedraw();
        }
        y += inspH + kGap;
        // 切り方を見る: 絵の上の格子 (選ばない)。
        if (r.Bottom() - y > 40) {
            w::TileSelection none;
            ImageGrid(c, {r.x, y, r.w, r.Bottom() - y}, "tiles_cut_preview", host, *src, Brush().state.zoom, none, false);
        }
    }

    // タイル: 当たりの形を絵の上に塗る (案 B) か、選んで名前・当たり・タグ・独自データ・自分で描く形を変える。
    void DrawTiles(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const AssetRef& ref, const tilemap::Tileset& set) {
        TileBrush& brush = Brush();
        const bool canEdit = !host.playing;
        if (set.sources.empty()) {
            Note(c, r, Tr(L"「切り方」で絵の元を足して、画像を入れてください。"));
            return;
        }
        float y = SourceBar(c, {r.x, r.y, r.w, kRow}, "tiles_shape_source", set, editSource_);
        const tilemap::TileSource* src = tilemap::FindSource(set, editSource_);
        if (!src) return;
        if (editSource_ != tileSource_) {  // (別の絵の元: 選んだタイルは外す)
            tileSource_ = editSource_;
            tileSel_ = {};
        }
        // 選ぶ | なし 全部 上 下 左 右 ◣ ◢ ◤ ◥ 描く (入らなければ 2 行)
        const std::array<std::wstring, 12> names{Tr(L"選ぶ"), Tr(L"なし"), Tr(L"全部"), Tr(L"上"), Tr(L"下"), Tr(L"左"), Tr(L"右"),
                                                 L"\u25E3", L"\u25E2", L"\u25E4", L"\u25E5", Tr(L"描く")};
        const std::array<std::wstring, 12> tips{Tr(L"タイルを選んで、下の欄で名前・当たり・タグ・独自データを変える (ドラッグで四角に選ぶ)"),
                                                Tr(L"当たりを消す (クリック・ドラッグで塗る)"), Tr(L"マスいっぱいの四角"), Tr(L"上半分"), Tr(L"下半分"), Tr(L"左半分"),
                                                Tr(L"右半分"), Tr(L"斜め: 直角が左下"), Tr(L"斜め: 直角が右下"), Tr(L"斜め: 直角が左上"), Tr(L"斜め: 直角が右上"),
                                                Tr(L"自分で描く: 塗ってから「選ぶ」で点を置く")};
        std::vector<std::wstring_view> all(names.begin(), names.end()), allTips(tips.begin(), tips.end());
        const int chosen = std::clamp(brush.state.paint, 0, 11);
        if (w::SegmentedWidth(c, all) <= r.w) {
            if (const int i = w::Segmented(c, "tiles_paint", {r.x, y, w::SegmentedWidth(c, all), kRow}, all, chosen, allTips); i >= 0) brush.state.paint = i;
            y += kRow + kGap;
        } else {
            const std::span<const std::wstring_view> a(all.data(), 7), b(all.data() + 7, 5), ta(allTips.data(), 7), tb(allTips.data() + 7, 5);
            if (const int i = w::Segmented(c, "tiles_paint_a", {r.x, y, std::min(r.w, w::SegmentedWidth(c, a)), kRow}, a, chosen < 7 ? chosen : -1, ta); i >= 0) {
                brush.state.paint = i;
            }
            y += kRow + 4;
            if (const int i = w::Segmented(c, "tiles_paint_b", {r.x, y, std::min(r.w, w::SegmentedWidth(c, b)), kRow}, b, chosen >= 7 ? chosen - 7 : -1, tb); i >= 0) {
                brush.state.paint = i + 7;
            }
            y += kRow + kGap;
        }
        // 絵と、その上の当たりの形 (この絵の元のタイル)。
        std::vector<w::TileMark> marks;
        for (const tilemap::TileInfo& info : set.tiles) {
            if (info.source != src->id || info.shape == tilemap::TileShape::None) continue;
            marks.push_back({info.column, info.row, tilemap::ShapePolygon(info, 0)});
        }
        const bool selecting = brush.state.paint == 0;
        const float below = selecting && tileSel_.width > 0 ? std::min(std::max(r.Bottom() - y - 120.f, 0.f), 330.f) : 0.f;
        w::TileSelection sel = selecting ? tileSel_ : w::TileSelection{};
        const Rect view{r.x, y, r.w, r.Bottom() - y - below};
        if (ImageGrid(c, view, "tiles_shapes", host, *src, brush.state.zoom, sel, canEdit, marks)) {
            if (selecting) {
                tileSel_ = sel;
                pointsGrab_ = false;
            } else {
                const auto shape = static_cast<tilemap::TileShape>(brush.state.paint - 1);
                const int sourceId = src->id;
                EditTileset(host, ref, "タイルの当たりを塗る", 0, [&](tilemap::Tileset& t) {
                    for (int row = sel.row; row < sel.row + sel.height; ++row) {
                        for (int col = sel.column; col < sel.column + sel.width; ++col) {
                            tilemap::TileInfo info = tilemap::TileOrDefault(t, sourceId, col, row);
                            info.shape = shape;
                            if (shape != tilemap::TileShape::Custom) {
                                info.points.clear();
                            } else if (info.points.empty()) {
                                info.points = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};  // (始めはマスいっぱい。「選ぶ」で点を動かす)
                            }
                            tilemap::SetTile(t, std::move(info));
                        }
                    }
                });
            }
        }
        if (below > 0) DrawTileFields(c, {r.x, r.Bottom() - below + kGap, r.w, below - kGap}, host, ref, set, *src);
    }

    // 選んだタイル (tileSel_) の欄: 名前・当たり・タグ (インスペクターと同じ描き方)・独自データ・自分で描く形の点。
    // 何枚も選んでいるときは、最初のタイルの値を見せ、変えた欄だけを全部に当てる。
    void DrawTileFields(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const AssetRef& ref, const tilemap::Tileset& set,
                        const tilemap::TileSource& src) {
        const bool canEdit = !host.playing;
        const w::TileSelection sel = tileSel_;
        const int sourceId = src.id;
        auto forEach = [sel, sourceId](tilemap::Tileset& t, const std::function<void(tilemap::TileInfo&)>& change) {
            for (int row = sel.row; row < sel.row + sel.height; ++row) {
                for (int col = sel.column; col < sel.column + sel.width; ++col) {
                    tilemap::TileInfo info = tilemap::TileOrDefault(t, sourceId, col, row);
                    change(info);
                    tilemap::SetTile(t, std::move(info));
                }
            }
        };
        tileCopy_ = tilemap::TileOrDefault(set, sourceId, sel.column, sel.row);
        const bool custom = tileCopy_.shape == tilemap::TileShape::Custom;
        const float pointsW = custom ? std::min(150.f, r.h - 8) : 0.f;
        const Rect left{r.x, r.y, r.w - (pointsW > 0 ? pointsW + kGap : 0), r.h};
        // 名前・当たり・タグ。
        inspector::Block b;
        b.type = &reflect::TypeOf<tilemap::TileInfo>();
        b.object = &tileCopy_;
        b.title = sel.width * sel.height > 1 ? TrF(L"タイル {} 個 ({} 列 {} 行から)", sel.width * sel.height, sel.column, sel.row)
                                            : TrF(L"タイル ({} 列 {} 行)", sel.column, sel.row);
        b.readOnly = !canEdit;
        std::vector<inspector::Block> blocks;
        blocks.push_back(std::move(b));
        const float fieldsH = static_cast<float>(set.fields.size()) * (kRow + 4);
        const float inspH = std::max(left.h - fieldsH, 60.f);
        if (const auto edit = inspector::Draw(c, tileInspector_, {left.x, left.y, left.w, inspH}, blocks); edit && canEdit) {
            EditTileset(host, ref, edit->label, edit->merge, [&](tilemap::Tileset& t) {
                forEach(t, [&](tilemap::TileInfo& info) {
                    const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<tilemap::TileInfo>(), &info, nullptr, edit->path);
                    if (pt.field) reflect::LoadField(*pt.field, pt.owner, edit->after, nullptr, nullptr);
                    if (info.shape != tilemap::TileShape::Custom) info.points.clear();
                    if (info.shape == tilemap::TileShape::Custom && info.points.empty()) info.points = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                });
            });
            c.RequestRedraw();
        }
        // 独自データ: 欄の種類ごとの入れ物。
        float y = left.y + inspH + 4;
        const float labelW = std::floor(left.w * 0.4f);
        for (const tilemap::TileField& f : set.fields) {
            if (y + kRow > left.Bottom() + 1) break;
            Label(c, {left.x + 4, y, labelW - 8, kRow}, f.name.empty() ? Tr(L"(名前の無い欄)") : Utf8ToWide(f.name));
            const Rect box{left.x + labelW, y, left.w - labelW, kRow};
            const std::string value = tilemap::TileData(set, tileCopy_, f.name);
            const std::string id = "tiles_data_" + f.name;
            std::optional<std::string> changed;
            switch (f.kind) {
                case tilemap::TileFieldKind::Bool: {
                    bool on = value == "true" || value == "1";
                    if (w::Checkbox(c, id, box, on, {}) && canEdit) changed = on ? "true" : "false";
                    break;
                }
                case tilemap::TileFieldKind::Int:
                case tilemap::TileFieldKind::Number: {
                    float v = 0;
                    try {
                        v = value.empty() ? 0.f : std::stof(value);
                    } catch (...) {
                        v = 0;
                    }
                    if (w::NumberField(c, id, box, v, -1e9f, 1e9f, dataText_[id]) && canEdit) {
                        changed = f.kind == tilemap::TileFieldKind::Int ? std::to_string(static_cast<long long>(std::llround(v))) : std::format("{}", v);
                    }
                    break;
                }
                case tilemap::TileFieldKind::Text: {
                    std::string text = value;
                    if (w::TextField(c, id, box, text, {}, canEdit) && canEdit) changed = text;
                    break;
                }
            }
            if (changed) {
                const std::string name = f.name, v = *changed;
                EditTileset(host, ref, "タイルの独自データを変更", editor::AssetContentCommand::MergeKey(id), [&](tilemap::Tileset& t) {
                    forEach(t, [&](tilemap::TileInfo& info) { tilemap::SetTileData(t, info, name, v); });
                });
            }
            y += kRow + 4;
        }
        // 自分で描く形: 拡大したタイルの上に点を置く。
        if (pointsW > 0) {
            const game_x_2d::Image2D* img = images_.Get(c, src.image, host.host);
            const Rect square{r.Right() - pointsW, r.y, pointsW, pointsW};
            if (!pointsGrab_) points_ = tileCopy_.points;
            Rect part{};
            if (img && img->bitmap) {
                const space2d::PixelRect px = space2d::GridCellOf(img->width, img->height, tilemap::CutOf(src), sel.column, sel.row);
                const D2D1_SIZE_U bpx = img->bitmap->GetPixelSize();
                const float kx = img->width > 0 ? static_cast<float>(bpx.width) / static_cast<float>(img->width) : 1.f;
                const float ky = img->height > 0 ? static_cast<float>(bpx.height) / static_cast<float>(img->height) : 1.f;
                part = {static_cast<float>(px.x) * kx, static_cast<float>(px.y) * ky, static_cast<float>(px.w) * kx, static_cast<float>(px.h) * ky};
            }
            const std::vector<Vec2> outline = tilemap::ConvexHull(points_);
            const bool done = w::ShapePoints(c, "tiles_points", square, img ? img->bitmap : nullptr, part, src.crisp || (img && img->sharp), outline, points_);
            pointsGrab_ = c.In().down[0] && (pointsGrab_ || c.IsHovered(square)) && !done;
            if (done && canEdit) {
                std::vector<Vec2> pts = points_;
                EditTileset(host, ref, "当たりの形の点を変更", 0, [&](tilemap::Tileset& t) {
                    forEach(t, [&](tilemap::TileInfo& info) {
                        if (info.shape == tilemap::TileShape::Custom) info.points = pts;
                    });
                });
                pointsGrab_ = false;
            }
            Label(c, {square.x, square.Bottom() + 2, square.w, 18}, Tr(L"左: 点を足す・動かす / 右: 消す"), theme::TextFaint);
        }
    }

    // 独自データの欄 (名前・種類・既定値) の一覧。インスペクターと同じ描き方 (足す・消す・並べ替え)。
    void DrawFields(ui::Context& c, const Rect& r, game_production::SceneViewHost& host, const AssetRef& ref, const tilemap::Tileset& set) {
        const bool canEdit = !host.playing;
        setCopy_ = set;
        inspector::Block b;
        b.type = &reflect::TypeOf<tilemap::Tileset>();
        b.object = &setCopy_;
        b.title = Tr(L"独自データの欄");
        b.note = Tr(L"欄を足すと、「タイル」で選んだタイルにその値を入れられます。欄を消しても、タイルの値はファイルに残ります");
        b.readOnly = !canEdit;
        b.fields = {"fields"};
        std::vector<inspector::Block> blocks;
        blocks.push_back(std::move(b));
        if (const auto edit = inspector::Draw(c, fieldsInspector_, r, blocks); edit && canEdit) {
            EditTileset(host, ref, edit->label, edit->merge, [&](tilemap::Tileset& t) {
                const reflect::PathTarget pt = reflect::Resolve(reflect::TypeOf<tilemap::Tileset>(), &t, nullptr, edit->path);
                if (pt.field) reflect::LoadField(*pt.field, pt.owner, edit->after, nullptr, nullptr);
            });
            c.RequestRedraw();
        }
    }

    // タイルセット `ref` (エディターが今持っている中身) を `change` で変えて、履歴に積む (Undo の 1 件)。
    void EditTileset(game_production::SceneViewHost& host, const AssetRef& ref, const std::string& label, uint64_t merge,
                     const std::function<void(tilemap::Tileset&)>& change) {
        const auto bytes = host.host.AssetContent(ref);
        if (!bytes) return;
        reflect::Unknowns unknowns;
        std::string err;
        auto t = tilemap::ReadTileset(*bytes, &err, &unknowns);
        if (!t) {
            log::Warn("タイル: タイルセットを読めないので変えられません: {}", err);
            return;
        }
        change(*t);
        tilemap::NormalizeIds(*t);
        std::string after = tilemap::WriteTileset(*t, &unknowns);
        if (after == *bytes) return;
        const uint64_t key = merge != 0 ? merge ^ editor::AssetContentCommand::MergeKey(ref.guid) : 0;
        host.host.History().Push(std::make_unique<editor::AssetContentCommand>(host.host, ref, *bytes, std::move(after), label, key), false);
        reader_ = {};  // (すぐに読み直す)
    }

    double now_ = 0;
    game_x_2d::EditorImages2D images_;
    TilesetReader reader_;
    reflect::Unknowns unknowns_;  // 見え方の知らないキー (書き戻す)
    inspector::State inspector_;
    tilemap::TileSource sourceCopy_;
    int editSource_ = 0;
    // タイル (当たり・タグ・独自データ)
    int tileSource_ = 0;
    w::TileSelection tileSel_;
    tilemap::TileInfo tileCopy_;
    inspector::State tileInspector_, fieldsInspector_;
    std::map<std::string, std::string> dataText_;  // 数の欄の打っている文字
    std::vector<Vec2> points_;  // 自分で描く形の点 (動かしている間の写し)
    bool pointsGrab_ = false;
    tilemap::Tileset setCopy_;
};

}  // namespace

std::unique_ptr<game_production::SceneView> CreateTilePanel() { return std::make_unique<TilePanel>(); }

}  // namespace rm::game_x_tilemap
