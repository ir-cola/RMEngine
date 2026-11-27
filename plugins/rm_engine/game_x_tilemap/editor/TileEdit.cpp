#include "rm_engine/game_x_tilemap/editor/TileEdit.h"

#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"

#include <algorithm>

namespace rm::game_x_tilemap {

TileEditCommand::TileEditCommand(std::function<reflect::EditTarget()> resolve, std::vector<ChunkChange> changes, std::string label)
    : resolve_(std::move(resolve)), changes_(std::move(changes)), label_(std::move(label)) {}

bool TileEditCommand::IsNoOp() const {
    return std::all_of(changes_.begin(), changes_.end(), [](const ChunkChange& c) { return c.before == c.after; });
}

size_t TileEditCommand::Bytes() const {
    size_t n = 0;
    for (const ChunkChange& c : changes_) n += c.before.size() + c.after.size() + 16;
    return n;
}

void TileEditCommand::Apply(bool after) {
    const reflect::EditTarget t = resolve_ ? resolve_() : reflect::EditTarget{};
    if (!t.object) return;  // (層が消えた)
    auto& layer = *static_cast<TileLayer*>(t.object);
    for (const ChunkChange& c : changes_) tilemap::SetChunkCells(layer.chunks, c.x, c.y, after ? c.after : c.before);
}

}  // namespace rm::game_x_tilemap
