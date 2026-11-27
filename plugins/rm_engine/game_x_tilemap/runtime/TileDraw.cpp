#include "rm_engine/game_x_tilemap/runtime/TileDraw.h"

#include "base/Log.h"
#include "core/paint/Runs.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"
#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/View2D.h"
#include "rm_engine/tilemap/runtime/Tileset.h"

#include <d2d1_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::game_x_tilemap {

namespace {

using game_x_2d::Drawn2D;
using game_x_2d::Placed2D;
using game_x_2d::WorldDraw2DFrame;
using tilemap::Cell;
using tilemap::TileValue;

// 区画の番号と絵の元の番号 (束 1 つ)。
using BatchKey = std::tuple<int, int, int>;  // (区画の y, 区画の x, 絵の元)

struct Batch {
    Microsoft::WRL::ComPtr<ID2D1SpriteBatch> batch;
    // 作ったときのもの (違えば作り直す)。
    std::string cells;
    ID2D1Bitmap* bitmap = nullptr;
    space2d::GridCut cut;
    uint32_t fileW = 0, fileH = 0;
    Vec2 cellSize{1, 1};
    Color color;
    bool used = false;
};

// 区画の文字を広げたもの (クリックで選ぶ・束を作る)。
struct Decoded {
    std::string cells;
    std::vector<TileValue> values;
};

struct LayerCache {
    std::map<BatchKey, Batch> batches;
    std::map<std::pair<int, int>, Decoded> decoded;  // (区画の x, 区画の y)
    bool used = false;
};

struct TilesetCache {
    std::string bytes;
    std::optional<tilemap::Tileset> set;
    bool told = false;
    bool looked = false;
    double lookedAt = 0;
    bool readOnce = false;  // (読めたことを 1 度だけコンソールに)
};

constexpr double kTilesetLookSeconds = 0.25;  // タイルセットの中身を読み直す間 (パネルで切り方を変えるとすぐ付いてくる)

bool SameCut(const space2d::GridCut& a, const space2d::GridCut& b) {
    return a.cellWidth == b.cellWidth && a.cellHeight == b.cellHeight && a.margin == b.margin && a.spacing == b.spacing;
}

// 層の中の平面 (Y が上) を、束の四角の平面 (Y が下: 画像と同じ向き) にする。
const space2d::Mat3 kFlipY{1, 0, 0, -1, 0, 0};

D2D1_MATRIX_3X2_F FlipsAbout(TileValue flips, float cx, float cy) {
    // 斜め (x と y を入れ替える) → 左右 → 上下 の順 (Tiled と同じ)。Direct2D は行ベクトル (p' = p * M)。
    D2D1::Matrix3x2F m = D2D1::Matrix3x2F::Identity();
    if (flips & tilemap::kFlipDiagonal) m = m * D2D1::Matrix3x2F(0, 1, 1, 0, cx - cy, cy - cx);
    if (flips & tilemap::kFlipX) m = m * D2D1::Matrix3x2F::Scale(-1, 1, {cx, cy});
    if (flips & tilemap::kFlipY) m = m * D2D1::Matrix3x2F::Scale(1, -1, {cx, cy});
    return m;
}

class TileDraw final : public game_x_2d::WorldDrawer2D {
public:
    void Collect(const WorldDraw2DFrame& f, std::vector<Drawn2D>& out) override {
        if (f.generation != generation_ || f.dc != dc_) {  // GPU のデバイスが作り直された: 束は捨てる
            generation_ = f.generation;
            dc_ = f.dc;
            dc3_.Reset();
            if (dc_) dc_->QueryInterface(IID_PPV_ARGS(&dc3_));
            for (auto& [id, layer] : layers_) layer.batches.clear();
        }
        for (auto& [id, layer] : layers_) layer.used = false;
        for (const Placed2D& p : f.placed) {
            const TileLayer* layer = LayerOf(f.scene, p.id);
            if (!layer || (f.game && !p.active)) continue;  // オフ: ゲームには出ない
            LayerCache& cache = layers_[p.id];
            cache.used = true;
            DrawLayer(f, p, *layer, cache, out);
        }
        std::erase_if(layers_, [](const auto& kv) { return !kv.second.used; });
    }

    bool Covers(const game_production::Scene& scene, std::span<const Placed2D> placed, int entity, Vec2 world) override {
        const TileLayer* layer = LayerOf(scene, entity);
        const Placed2D* p = game_x_2d::FindPlaced(placed, entity);
        if (!layer || !p) return false;
        const auto inv = space2d::Inverse(p->world);
        if (!inv) return false;
        const Cell cell = tilemap::CellAt(space2d::TransformPoint(*inv, world), GridCellSize(scene, entity));
        const Cell k = tilemap::ChunkOf(cell);
        for (const tilemap::TileChunk& ch : layer->chunks) {
            if (ch.x != k.x || ch.y != k.y) continue;
            const Decoded& d = Decode(layers_[entity], ch);
            return !tilemap::IsEmpty(d.values[static_cast<size_t>(tilemap::IndexInChunk(cell))]);
        }
        return false;
    }

private:
    const Decoded& Decode(LayerCache& cache, const tilemap::TileChunk& ch) {
        Decoded& d = cache.decoded[{ch.x, ch.y}];
        if (d.values.empty() || d.cells != ch.cells) {
            d.cells = ch.cells;
            d.values = paint::DecodeRunsU32(ch.cells, tilemap::kChunkCells);
        }
        return d;
    }

    const tilemap::Tileset* TilesetOf(const WorldDraw2DFrame& f, const AssetRef& ref) {
        if (ref.Empty() || !f.asset) return nullptr;
        TilesetCache& t = tilesets_[ref.guid];
        if (t.looked && f.time - t.lookedAt < kTilesetLookSeconds && f.time >= t.lookedAt) return t.set ? &*t.set : nullptr;
        t.looked = true;
        t.lookedAt = f.time;
        const auto bytes = f.asset(ref);
        if (!bytes) {
            if (!t.told) log::Warn("タイルの層: タイルセット ({}) が見つからないので、その層は描きません", ref.guid);
            t.told = true;
            t.set.reset();
            t.bytes.clear();
            return nullptr;
        }
        if (*bytes != t.bytes || (!t.set && !t.told)) {
            t.bytes = *bytes;
            std::string err;
            t.set = tilemap::ReadTileset(*bytes, &err);
            if (!t.set && !t.told) log::Warn("タイルの層: タイルセット ({}) を読めません: {}", ref.guid, err);
            if (t.set && !t.readOnce) log::Info("タイルの層: タイルセット ({}) を読みました (絵の元 {} 枚)", ref.guid, t.set->sources.size());
            t.readOnce = t.readOnce || t.set.has_value();
            t.told = !t.set;
        }
        return t.set ? &*t.set : nullptr;
    }

    void DrawLayer(const WorldDraw2DFrame& f, const Placed2D& p, const TileLayer& layer, LayerCache& cache, std::vector<Drawn2D>& out) {
        for (auto& [key, b] : cache.batches) b.used = false;
        const tilemap::Tileset* set = TilesetOf(f, layer.tileset);
        const Vec2 cellSize = GridCellSize(f.scene, p.id);
        // 見えている区画: 画面の 4 隅を層の中へ戻した外接の四角 (回した層でも)。
        std::optional<tilemap::CellRange> visible;
        if (const auto inv = space2d::Inverse(p.world)) {
            const space2d::ScreenRect& v = f.viewport;
            const Vec2 corners[4] = {{v.x, v.y}, {v.x + v.w, v.y}, {v.x, v.y + v.h}, {v.x + v.w, v.y + v.h}};
            Vec2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
            for (const Vec2& s : corners) {
                const Vec2 q = space2d::TransformPoint(*inv, space2d::ToWorld(f.camera, v, s));
                mn = {std::min(mn.x, q.x), std::min(mn.y, q.y)};
                mx = {std::max(mx.x, q.x), std::max(mx.y, q.y)};
            }
            visible = tilemap::CellsIn(mn, mx, cellSize);
        }
        const Color color = p.active || f.game ? layer.color : layer.color.Alpha(layer.color.a * 0.35f);  // オフ: シーンビューでは薄く
        if (set && dc3_ && visible) {
            const Cell k0 = tilemap::ChunkOf(visible->min), k1 = tilemap::ChunkOf(visible->max);
            for (const tilemap::TileChunk& ch : layer.chunks) {
                if (ch.x < k0.x || ch.x > k1.x || ch.y < k0.y || ch.y > k1.y) continue;
                const Decoded& d = Decode(cache, ch);
                std::set<int> sources;
                for (const TileValue v : d.values) {
                    if (!tilemap::IsEmpty(v)) sources.insert(tilemap::SourceOf(v));
                }
                for (const int sourceId : sources) {
                    const tilemap::TileSource* src = tilemap::FindSource(*set, sourceId);
                    const game_x_2d::Image2D* img = src ? f.images(src->image) : nullptr;
                    if (!img || !img->bitmap) continue;  // (絵が無い・まだ: 描かない)
                    Batch& b = cache.batches[{ch.y, ch.x, sourceId}];
                    b.used = true;
                    const space2d::GridCut cut = tilemap::CutOf(*src);
                    const bool same = b.batch && b.cells == ch.cells && b.bitmap == img->bitmap && SameCut(b.cut, cut) && b.fileW == img->width &&
                                      b.fileH == img->height && b.cellSize == cellSize && b.color == color;
                    if (!same && !Build(b, d.values, {ch.x, ch.y}, sourceId, *img, cut, cellSize, color)) continue;
                    b.cells = ch.cells;
                    b.bitmap = img->bitmap;
                    b.cut = cut;
                    b.fileW = img->width;
                    b.fileH = img->height;
                    b.cellSize = cellSize;
                    b.color = color;
                    Drawn2D item;
                    item.draw.batch = b.batch.Get();
                    item.draw.image = img->bitmap;
                    item.draw.world = p.world * kFlipY;
                    item.draw.crisp = src->crisp || img->sharp;
                    item.key = {p.order, layer.order, p.tree};
                    item.entity = p.id;
                    out.push_back(item);
                }
            }
        }
        std::erase_if(cache.batches, [](const auto& kv) { return !kv.second.used; });
        // (区画の文字の写しは、層にもう無い区画のものを捨てる)
        std::erase_if(cache.decoded, [&](const auto& kv) {
            return std::none_of(layer.chunks.begin(), layer.chunks.end(), [&](const tilemap::TileChunk& c) { return c.x == kv.first.first && c.y == kv.first.second; });
        });
    }

    // 区画 `chunk` の、絵の元 `sourceId` のマスの束を作り直す。
    bool Build(Batch& b, const std::vector<TileValue>& values, Cell chunk, int sourceId, const game_x_2d::Image2D& img, const space2d::GridCut& cut,
               Vec2 cellSize, Color color) {
        if (!b.batch && FAILED(dc3_->CreateSpriteBatch(&b.batch))) {
            b.batch.Reset();
            return false;
        }
        b.batch->Clear();
        const D2D1_SIZE_U px = img.bitmap->GetPixelSize();
        const uint32_t fileW = img.width > 0 ? img.width : px.width, fileH = img.height > 0 ? img.height : px.height;
        const double kx = fileW > 0 ? static_cast<double>(px.width) / fileW : 1.0, ky = fileH > 0 ? static_cast<double>(px.height) / fileH : 1.0;
        std::vector<D2D1_RECT_F> dest;
        std::vector<D2D1_RECT_U> source;
        std::vector<D2D1_COLOR_F> colors;
        std::vector<D2D1_MATRIX_3X2_F> transforms;
        bool anyFlip = false;
        for (int i = 0; i < tilemap::kChunkCells; ++i) {
            const TileValue v = values[static_cast<size_t>(i)];
            if (tilemap::IsEmpty(v) || tilemap::SourceOf(v) != sourceId) continue;
            const space2d::PixelRect r = space2d::GridCellOf(fileW, fileH, cut, tilemap::ColumnOf(v), tilemap::RowOf(v));
            if (r.w == 0 || r.h == 0) continue;  // (切り方の外のタイル: 描かない)
            const Cell c = tilemap::CellOfIndex(chunk, i);
            const Vec2 lo = tilemap::CellMin(c, cellSize), hi = tilemap::CellMax(c, cellSize);
            dest.push_back(D2D1::RectF(lo.x, -hi.y, hi.x, -lo.y));  // (Y を下に: kFlipY)
            auto at = [](double v) { return static_cast<UINT32>(std::lround(v)); };
            source.push_back({at(r.x * kx), at(r.y * ky), at((r.x + r.w) * kx), at((r.y + r.h) * ky)});
            colors.push_back({color.r, color.g, color.b, color.a});
            const TileValue flips = tilemap::FlipsOf(v);
            anyFlip = anyFlip || flips != 0;
            transforms.push_back(FlipsAbout(flips, (lo.x + hi.x) * 0.5f, -(lo.y + hi.y) * 0.5f));
        }
        if (dest.empty()) return false;
        const HRESULT hr = b.batch->AddSprites(static_cast<UINT32>(dest.size()), dest.data(), source.data(), colors.data(),
                                               anyFlip ? transforms.data() : nullptr, sizeof(D2D1_RECT_F), sizeof(D2D1_RECT_U), sizeof(D2D1_COLOR_F),
                                               sizeof(D2D1_MATRIX_3X2_F));
        return SUCCEEDED(hr);
    }

    uint64_t generation_ = 0;
    ID2D1DeviceContext* dc_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext3> dc3_;
    std::unordered_map<int, LayerCache> layers_;  // 層のエンティティごと
    std::unordered_map<std::string, TilesetCache> tilesets_;
};

}  // namespace

std::unique_ptr<game_x_2d::WorldDrawer2D> CreateTileDraw() { return std::make_unique<TileDraw>(); }

}  // namespace rm::game_x_tilemap
