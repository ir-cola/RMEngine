#include "rm_engine/game_x_tilemap/editor/TileTool.h"

#include "base/Log.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "rm_engine/game_x_tilemap/editor/TileBrush.h"
#include "rm_engine/game_x_tilemap/editor/TileEdit.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"
#include "rm_engine/space2d/runtime/Atlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace rm::game_x_tilemap {

namespace {

namespace theme = editor::theme;
namespace w = editor::w;
using game_x_2d::SceneTool2DButton;
using game_x_2d::SceneTool2DFrame;
using tilemap::Cell;

enum Mode { kPen = 0, kRect = 1, kEraser = 2, kPicker = 3 };
enum class Action { Paint, Erase, Rect, Pick };

constexpr size_t kMaxGhostCells = 4096;  // 置く前の絵を描くマスの数の上限 (それより大きい四角は枠だけ)

const std::array<SceneTool2DButton, 4> kButtons{{
    {w::Icon::Brush, editor::TrKey(L"筆 (B): クリック・ドラッグで、パネル「タイル」で選んだタイルを塗る。Shift を押しながらで消す・Alt でスポイト"), ui::Key::B},
    {w::Icon::TileRect, editor::TrKey(L"四角 (U): ドラッグした四角を、選んだタイルで敷き詰める。Shift を押しながらで枠だけ"), ui::Key::U},
    {w::Icon::Eraser, editor::TrKey(L"消しゴム: クリック・ドラッグでマスを空にする"), ui::Key::None},
    {w::Icon::Picker, editor::TrKey(L"スポイト (I): マスのタイルを取る。ドラッグして四角に取るとスタンプ"), ui::Key::I},
}};

space2d::ScreenRect ScreenOf(const ui::Rect& r) { return {r.x, r.y, r.w, r.h}; }

class TileTool final : public game_x_2d::SceneTool2D {
public:
    bool Available(const game_production::SceneViewHost& host) const override { return !host.playing && TargetLayer(host) != 0; }
    std::span<const SceneTool2DButton> Buttons() const override { return kButtons; }
    bool Busy() const override { return drag_; }

    void Input(const SceneTool2DFrame& f, int mode) override {
        game_production::SceneViewHost& host = f.host;
        const ui::Input& in = f.c.In();
        if (!Locate(f)) {
            if (drag_) Cancel(host);
            return;
        }
        if (!drag_) {
            if (f.hovered && f.c.MousePressed()) Begin(f, mode, in.shift, in.alt);
            return;
        }
        if (history_ != std::pair{host.host.History().UndoCount(), host.host.History().RedoCount()}) {
            Abandon();  // (ボタンを押したまま Ctrl+Z した)
            return;
        }
        if (f.c.KeyPressed(ui::Key::Escape)) {
            Cancel(host);
            return;
        }
        if (!in.down[0]) {
            End(f);
            return;
        }
        if (action_ == Action::Paint || action_ == Action::Erase) Stroke(last_, hover_);
        last_ = hover_;
        f.c.RequestRedraw();
    }

    void Draw(const SceneTool2DFrame& f, int mode, std::vector<space2d::SpriteDraw>& sprites, std::vector<space2d::Line2D>& lines) override {
        if (!located_ || layerEntity_ != TargetLayer(f.host)) return;
        const ui::Input& in = f.c.In();
        const Action action = drag_ ? action_ : ActionOf(mode, in.shift, in.alt);
        const TileLayer* layer = LayerOf(f.host.scene, layerEntity_);
        if (!layer) return;
        const tilemap::Stamp stamp = Brush().Current(layer->tileset.guid);
        Cell a = hover_, b = hover_;
        if (action == Action::Paint && !stamp.Empty()) {
            b = {hover_.x + stamp.width - 1, hover_.y - stamp.height + 1};
        } else if (drag_ && (action == Action::Rect || action == Action::Pick)) {
            a = start_;
        }
        // 置く前の絵 (半透明): 筆のスタンプ・四角の敷き詰め。
        if ((action == Action::Paint || (action == Action::Rect && drag_)) && !stamp.Empty() && paint::RectCount(a, b) <= static_cast<long long>(kMaxGhostCells)) {
            const tilemap::Tileset* set = reader_.Get(f.host.host, layer->tileset, f.c.Time());
            const int left = std::min(a.x, b.x), top = std::max(a.y, b.y);
            for (const Cell& c : paint::RectCells(a, b, action == Action::Rect && rectOutline_)) {
                const tilemap::TileValue v = stamp.At(c.x - left, top - c.y);
                if (set && !tilemap::IsEmpty(v)) Ghost(f, *set, v, c, sprites);
            }
        }
        // マスの枠。
        const Vec2 lo = tilemap::CellMin({std::min(a.x, b.x), std::min(a.y, b.y)}, cellSize_);
        const Vec2 hi = tilemap::CellMax({std::max(a.x, b.x), std::max(a.y, b.y)}, cellSize_);
        const Vec2 k[4] = {space2d::TransformPoint(world_, lo), space2d::TransformPoint(world_, {hi.x, lo.y}), space2d::TransformPoint(world_, hi),
                           space2d::TransformPoint(world_, {lo.x, hi.y})};
        const Color col = action == Action::Erase ? theme::Danger : theme::Accent;
        for (int i = 0; i < 4; ++i) lines.push_back({k[i], k[(i + 1) % 4], col, 1.5f});
    }

    void Cancel(game_production::SceneViewHost& host) override {
        (void)host;
        if (!drag_) return;
        if (const reflect::EditTarget t = resolve_ ? resolve_() : reflect::EditTarget{}; t.object) {
            auto& layer = *static_cast<TileLayer*>(t.object);
            for (const auto& [key, text] : written_) tilemap::SetChunkCells(layer.chunks, key.first, key.second, tilemap::ChunkCells(before_, key.first, key.second));
        }
        Reset();
    }

private:
    static Action ActionOf(int mode, bool shift, bool alt) {
        if (alt || mode == kPicker) return Action::Pick;
        if (mode == kEraser || (mode == kPen && shift)) return Action::Erase;
        return mode == kRect ? Action::Rect : Action::Paint;
    }

    // マウスの下のマス (塗る層の中)。層が無い・置けない: false。
    bool Locate(const SceneTool2DFrame& f) {
        located_ = false;
        const int id = drag_ ? layerEntity_ : TargetLayer(f.host);
        const game_x_2d::Placed2D* p = game_x_2d::FindPlaced(f.placed, id);
        if (!p) return false;
        const auto inv = space2d::Inverse(p->world);
        if (!inv) return false;
        cellSize_ = GridCellSize(f.host.scene, id);
        world_ = p->world;
        const Vec2 mouse{f.c.In().mouseX, f.c.In().mouseY};
        hover_ = tilemap::CellAt(space2d::TransformPoint(*inv, space2d::ToWorld(f.camera, ScreenOf(f.view), mouse)), cellSize_);
        layerEntity_ = id;
        located_ = true;
        return true;
    }

    void Begin(const SceneTool2DFrame& f, int mode, bool shift, bool alt) {
        game_production::SceneViewHost& host = f.host;
        if (!host.component) return;
        resolve_ = host.component(layerEntity_, reflect::TypeOf<TileLayer>().name);
        const reflect::EditTarget t = resolve_ ? resolve_() : reflect::EditTarget{};
        if (!t.object) return;
        const auto& layer = *static_cast<const TileLayer*>(t.object);
        if (layer.chunk != tilemap::kChunkSize) {
            log::Warn("タイル: この層の区画の大きさ ({}) は、この版では塗れません (読んで描くことはできます)", layer.chunk);
            return;
        }
        action_ = ActionOf(mode, shift, alt);
        rectOutline_ = shift;
        stamp_ = Brush().Current(layer.tileset.guid);
        if ((action_ == Action::Paint || action_ == Action::Rect) && stamp_.Empty()) {
            if (!toldNoTile_) log::Info("タイル: パネル「タイル」のパレットで、塗るタイルを選んでください");
            toldNoTile_ = true;
            return;
        }
        tileset_ = layer.tileset.guid;
        before_ = layer.chunks;
        store_.Load(layer.chunks, layer.chunk);
        touched_.clear();
        written_.clear();
        start_ = last_ = hover_;
        history_ = {host.host.History().UndoCount(), host.host.History().RedoCount()};
        drag_ = true;
        if (action_ == Action::Paint || action_ == Action::Erase) Stroke(hover_, hover_);
    }

    // 前のマスから今のマスまで、すき間なく塗る / 消す。
    void Stroke(Cell from, Cell to) {
        bool changed = false;
        for (const Cell& c : paint::LineCells(from, to)) {
            changed = (action_ == Action::Paint ? tilemap::PlaceStamp(store_, c, stamp_, touched_) : tilemap::EraseAt(store_, c, 1, 1, touched_)) || changed;
        }
        if (changed) Write();
    }

    // 触った区画の文字を層に書く (インスペクター・描く物がすぐ付いてくる)。
    void Write() {
        const reflect::EditTarget t = resolve_ ? resolve_() : reflect::EditTarget{};
        if (!t.object) return;
        auto& layer = *static_cast<TileLayer*>(t.object);
        for (const Cell& k : tilemap::ChunksOf(touched_)) {
            std::string text = store_.Encode(k);
            written_[{k.x, k.y}] = text;
            tilemap::SetChunkCells(layer.chunks, k.x, k.y, std::move(text));
        }
    }

    void End(const SceneTool2DFrame& f) {
        if (action_ == Action::Rect) {
            if (tilemap::FillRect(store_, start_, hover_, stamp_, rectOutline_, touched_)) {
                Write();
            } else if (paint::RectCount(start_, hover_) > static_cast<long long>(paint::kMaxPaintCells)) {
                log::Warn("タイル: 四角が大きすぎるので塗りませんでした ({} マスより多い)", paint::kMaxPaintCells);
            }
        }
        if (action_ == Action::Pick) {
            TileBrush& brush = Brush();
            tilemap::Stamp picked = tilemap::PickStamp(store_, start_, hover_);
            const bool any = std::any_of(picked.tiles.begin(), picked.tiles.end(), [](tilemap::TileValue v) { return !tilemap::IsEmpty(v); });
            if (any) {
                brush.picked = std::move(picked);
                brush.state.tileset = tileset_;
                const tilemap::TileValue first = brush.picked.At(0, 0);
                if (brush.picked.width == 1 && brush.picked.height == 1) {  // (1 つならパレットの選択もそれに)
                    brush.picked = {};
                    brush.state.source = tilemap::SourceOf(first);
                    brush.state.column = tilemap::ColumnOf(first);
                    brush.state.row = tilemap::RowOf(first);
                    brush.state.width = brush.state.height = 1;
                }
            }
            Reset();
            return;
        }
        std::vector<ChunkChange> changes;
        for (const auto& [key, text] : written_) {
            std::string before = tilemap::ChunkCells(before_, key.first, key.second);
            if (before != text) changes.push_back({key.first, key.second, std::move(before), text});
        }
        if (!changes.empty()) {
            const char* label = action_ == Action::Erase ? "タイルを消す" : action_ == Action::Rect ? "タイルを四角に塗る" : "タイルを塗る";
            f.host.host.History().Push(std::make_unique<TileEditCommand>(resolve_, std::move(changes), label));  // (もう反映済み)
        }
        Reset();
    }

    // 履歴が動いたあとでドラッグをやめる: 自分が書いたままの区画だけを、始めの文字に戻す。何も積まない。
    void Abandon() {
        if (const reflect::EditTarget t = resolve_ ? resolve_() : reflect::EditTarget{}; t.object) {
            auto& layer = *static_cast<TileLayer*>(t.object);
            for (const auto& [key, text] : written_) {
                if (tilemap::ChunkCells(layer.chunks, key.first, key.second) == text) {
                    tilemap::SetChunkCells(layer.chunks, key.first, key.second, tilemap::ChunkCells(before_, key.first, key.second));
                }
            }
        }
        log::Info("ドラッグ中に元に戻す / やり直しがあったので、タイルを塗るのをやめました");
        Reset();
    }

    void Reset() {
        drag_ = false;
        resolve_ = {};
        before_.clear();
        touched_.clear();
        written_.clear();
        store_.Clear();
    }

    // タイル `v` をマス `c` に半透明で (置く前の絵)。
    void Ghost(const SceneTool2DFrame& f, const tilemap::Tileset& set, tilemap::TileValue v, Cell c, std::vector<space2d::SpriteDraw>& out) {
        const tilemap::TileSource* src = tilemap::FindSource(set, tilemap::SourceOf(v));
        const game_x_2d::Image2D* img = src ? f.images(src->image) : nullptr;
        if (!img || !img->bitmap) return;
        const D2D1_SIZE_U px = img->bitmap->GetPixelSize();
        const uint32_t fw = img->width > 0 ? img->width : px.width, fh = img->height > 0 ? img->height : px.height;
        const space2d::PixelRect r = space2d::GridCellOf(fw, fh, tilemap::CutOf(*src), tilemap::ColumnOf(v), tilemap::RowOf(v));
        if (r.w == 0 || r.h == 0) return;
        const double kx = fw > 0 ? static_cast<double>(px.width) / fw : 1.0, ky = fh > 0 ? static_cast<double>(px.height) / fh : 1.0;
        space2d::SpriteDraw d;
        d.image = img->bitmap;
        d.part = {static_cast<uint32_t>(std::lround(r.x * kx)), static_cast<uint32_t>(std::lround(r.y * ky)),
                  std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(r.w * kx))), std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(r.h * ky)))};
        d.world = world_ * space2d::Translation(tilemap::CellCenter(c, cellSize_));
        d.size = cellSize_;
        d.color = {1, 1, 1, 0.6f};
        d.crisp = src->crisp || img->sharp;
        out.push_back(d);
    }

    // マウスの下
    bool located_ = false;
    int layerEntity_ = 0;
    Cell hover_;
    Vec2 cellSize_{1, 1};
    space2d::Mat3 world_;
    // ドラッグ
    bool drag_ = false;
    Action action_ = Action::Paint;
    bool rectOutline_ = false;
    Cell start_, last_;
    tilemap::Stamp stamp_;
    std::string tileset_;
    std::function<reflect::EditTarget()> resolve_;
    std::vector<tilemap::TileChunk> before_;
    tilemap::TileStore store_;
    tilemap::ChunkSet touched_;
    std::map<std::pair<int, int>, std::string> written_;  // 区画ごとに、最後に書いた文字
    std::pair<size_t, size_t> history_{0, 0};
    bool toldNoTile_ = false;
    TilesetReader reader_;
};

}  // namespace

std::unique_ptr<game_x_2d::SceneTool2D> CreateTileTool() { return std::make_unique<TileTool>(); }

}  // namespace rm::game_x_tilemap
