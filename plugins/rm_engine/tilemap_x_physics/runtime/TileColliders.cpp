#include "rm_engine/tilemap_x_physics/runtime/TileColliders.h"

#include "base/Log.h"
#include "core/paint/Runs.h"

#include <algorithm>

namespace rm::tilemap_x_physics {

namespace {

constexpr double kTableLookSeconds = 0.5;  // エディターでタイルセットを読み直す間

}  // namespace

uint64_t ChunkKey(int x, int y) { return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y); }

const TileColliders::Table* TileColliders::TableOf(const AssetRef& ref, const AssetBytes& bytes, double now) {
    if (ref.Empty() || !bytes) return nullptr;
    Table& t = tables_[ref.guid];
    const bool look = t.readAt < 0 || (reread_ && (now - t.readAt >= kTableLookSeconds || now < t.readAt));
    if (look) {
        t.readAt = now;
        const auto got = bytes(ref);
        if (!got) {
            if (!t.told) log::Warn("タイルの当たり: タイルセット ({}) が見つからないので、その層は当たりません", ref.guid);
            t.told = true;
            t.ok = false;
            t.bytes.clear();
        } else if (*got != t.bytes || !t.ok) {
            t.bytes = *got;
            std::string err;
            const auto set = tilemap::ReadTileset(*got, &err);
            t.ok = set.has_value();
            if (set) t.table = tilemap::CollisionTable(*set);
            if (!set && !t.told) log::Warn("タイルの当たり: タイルセット ({}) を読めません: {}", ref.guid, err);
            t.told = !set;
        }
    }
    return t.ok ? &t : nullptr;
}

std::vector<physics::StaticPiece> TileColliders::Pieces(const game_production::Scene& scene, int entity, const game_x_tilemap::TileLayer& layer,
                                                        const AssetBytes& bytes, double now) {
    std::vector<physics::StaticPiece> out;
    const Table* table = TableOf(layer.tileset, bytes, now);
    auto& chunks = layers_[entity];
    if (!table || table->table.Empty() || layer.chunk != tilemap::kChunkSize) {
        chunks.clear();
        return out;
    }
    const Vec2 cellSize = game_x_tilemap::GridCellSize(scene, entity);
    const uint64_t fp = table->table.Fingerprint();
    std::map<std::pair<int, int>, Chunk> now2;
    for (const tilemap::TileChunk& ch : layer.chunks) {
        auto it = chunks.find({ch.x, ch.y});
        Chunk c = it != chunks.end() ? std::move(it->second) : Chunk{};
        if (!c.geometry || c.cells != ch.cells || c.table != fp || !(c.cellSize == cellSize)) {
            const std::vector<tilemap::TileValue> cells = paint::DecodeRunsU32(ch.cells, tilemap::kChunkCells);
            const tilemap::ChunkCollision col = tilemap::CollideChunk(cells, {ch.x, ch.y}, cellSize, table->table);
            auto g = std::make_shared<physics::StaticGeometry>();
            for (const tilemap::CollisionBox& b : col.boxes) g->boxes2D.push_back({b.min, b.max});
            g->polygons2D = col.polygons;
            c.cells = ch.cells;
            c.table = fp;
            c.cellSize = cellSize;
            c.version = nextVersion_++;
            c.geometry = std::move(g);
        }
        if (!c.geometry->Empty()) out.push_back({ChunkKey(ch.x, ch.y), c.version, c.geometry});
        now2.emplace(std::pair{ch.x, ch.y}, std::move(c));
    }
    chunks = std::move(now2);  // (層から消えた区画は忘れる)
    return out;
}

void TileColliders::Forget(const std::vector<int>& alive) {
    std::erase_if(layers_, [&](const auto& kv) { return std::find(alive.begin(), alive.end(), kv.first) == alive.end(); });
}

}  // namespace rm::tilemap_x_physics
