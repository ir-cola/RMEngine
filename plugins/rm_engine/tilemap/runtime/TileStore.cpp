#include "rm_engine/tilemap/runtime/TileStore.h"

#include "core/paint/Runs.h"

#include <algorithm>
#include <cstdlib>

namespace rm::tilemap {

void TileStore::Load(std::span<const TileChunk> chunks, int chunkSize) {
    chunks_.clear();
    const int size = std::clamp(chunkSize, 1, 1024);
    const size_t count = static_cast<size_t>(size) * static_cast<size_t>(size);
    for (const TileChunk& ch : chunks) {
        const std::vector<TileValue> cells = paint::DecodeRunsU32(ch.cells, count);
        if (size == kChunkSize) {  // (ふつう: そのまま 1 つの区画)
            Chunk& to = chunks_[Key({ch.x, ch.y})];
            to = {};
            for (int i = 0; i < kChunkCells; ++i) {
                to.cells[static_cast<size_t>(i)] = cells[static_cast<size_t>(i)];
                if (!IsEmpty(cells[static_cast<size_t>(i)])) ++to.filled;
            }
            to.version = nextVersion_++;
            if (to.filled == 0) chunks_.erase(Key({ch.x, ch.y}));
            continue;
        }
        // 違う大きさの区画 (将来の形): マスごとに今の区画へ詰め直す。
        for (size_t i = 0; i < count; ++i) {
            if (IsEmpty(cells[i])) continue;
            const int ix = static_cast<int>(i % static_cast<size_t>(size)), iy = static_cast<int>(i / static_cast<size_t>(size));
            const long long x = static_cast<long long>(ch.x) * size + ix, y = static_cast<long long>(ch.y) * size + iy;
            if (std::llabs(x) > 1'000'000'000 || std::llabs(y) > 1'000'000'000) continue;
            Set({static_cast<int>(x), static_cast<int>(y)}, cells[i]);
        }
    }
}

TileValue TileStore::Get(Cell c) const {
    const auto it = chunks_.find(Key(ChunkOf(c)));
    return it == chunks_.end() ? 0 : it->second.cells[static_cast<size_t>(IndexInChunk(c))];
}

bool TileStore::Set(Cell c, TileValue v) {
    if (IsEmpty(v)) v = 0;  // (空のマスはいつも 0: 反転のビットだけのマスを作らない)
    const auto key = Key(ChunkOf(c));
    auto it = chunks_.find(key);
    if (it == chunks_.end()) {
        if (v == 0) return false;
        it = chunks_.emplace(key, Chunk{}).first;
    }
    Chunk& ch = it->second;
    TileValue& at = ch.cells[static_cast<size_t>(IndexInChunk(c))];
    if (at == v) return false;
    if (at == 0) ++ch.filled;
    if (v == 0) --ch.filled;
    at = v;
    ch.version = nextVersion_++;
    if (ch.filled == 0) chunks_.erase(it);
    return true;
}

const TileStore::Chunk* TileStore::FindChunk(Cell chunk) const {
    const auto it = chunks_.find(Key(chunk));
    return it == chunks_.end() ? nullptr : &it->second;
}

size_t TileStore::FilledCount() const {
    size_t n = 0;
    for (const auto& [key, ch] : chunks_) n += static_cast<size_t>(ch.filled);
    return n;
}

std::string TileStore::Encode(Cell chunk) const {
    const Chunk* ch = FindChunk(chunk);
    return ch ? paint::EncodeRuns(std::span<const TileValue>(ch->cells)) : std::string();
}

void SetChunkCells(std::vector<TileChunk>& chunks, int x, int y, std::string cells) {
    const auto at = std::find_if(chunks.begin(), chunks.end(), [&](const TileChunk& c) { return c.x == x && c.y == y; });
    if (cells.empty()) {
        if (at != chunks.end()) chunks.erase(at);
        return;
    }
    if (at != chunks.end()) {
        at->cells = std::move(cells);
        return;
    }
    // 新しい区画: 下の行から、左から右の順の場所に入れる (ファイルの差分が読みやすいように)。
    const auto before = std::find_if(chunks.begin(), chunks.end(), [&](const TileChunk& c) { return c.y > y || (c.y == y && c.x > x); });
    chunks.insert(before, TileChunk{x, y, std::move(cells)});
}

std::string ChunkCells(std::span<const TileChunk> chunks, int x, int y) {
    for (const TileChunk& c : chunks) {
        if (c.x == x && c.y == y) return c.cells;
    }
    return {};
}

void TileStore::WriteTo(std::vector<TileChunk>& chunks, std::span<const Cell> which) const {
    for (const Cell& k : which) SetChunkCells(chunks, k.x, k.y, Encode(k));
}

std::vector<TileChunk> TileStore::Save() const {
    std::vector<TileChunk> out;
    out.reserve(chunks_.size());
    for (const auto& [key, ch] : chunks_) out.push_back({key.second, key.first, paint::EncodeRuns(std::span<const TileValue>(ch.cells))});
    return out;
}

}  // namespace rm::tilemap
