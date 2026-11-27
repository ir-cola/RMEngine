#include "rm_engine/game_x_tilemap/editor/TileBrush.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "rm_engine/game_x_tilemap/runtime/TileLayer.h"

namespace rm::game_x_tilemap {

namespace {

constexpr double kLookSeconds = 0.25;

}  // namespace

tilemap::Stamp TileBrush::Current(const std::string& tileset) const {
    if (tileset.empty() || tileset != state.tileset) return {};
    if (!picked.Empty()) return picked;
    if (state.source <= 0 || state.width <= 0 || state.height <= 0) return {};
    return tilemap::StampOfTiles(state.source, state.column, state.row, state.width, state.height);
}

TileBrush& Brush() {
    static TileBrush brush;
    return brush;
}

const tilemap::Tileset* TilesetReader::Get(editor::SoftwareHost& host, const AssetRef& ref, double now) {
    if (ref.Empty()) return nullptr;
    Entry& e = entries_[ref.guid];
    if (e.lookedAt >= 0 && now - e.lookedAt < kLookSeconds && now >= e.lookedAt) return e.set ? &*e.set : nullptr;
    e.lookedAt = now;
    const auto bytes = host.AssetContent(ref);
    if (!bytes) {
        if (!e.told) log::Warn("タイル: タイルセット ({}) がプロジェクトに見つかりません", ref.guid);
        e.told = true;
        e.set.reset();
        e.bytes.clear();
        return nullptr;
    }
    if (*bytes != e.bytes || !e.set) {
        e.bytes = *bytes;
        std::string err;
        e.set = tilemap::ReadTileset(*bytes, &err);
        if (!e.set && !e.told) log::Warn("タイル: タイルセット「{}」を読めません: {}", PathToUtf8(host.AssetFile(ref).filename()), err);
        e.told = !e.set;
    }
    return e.set ? &*e.set : nullptr;
}

const std::string* TilesetReader::Bytes(const AssetRef& ref) const {
    const auto it = entries_.find(ref.guid);
    return it == entries_.end() || !it->second.set ? nullptr : &it->second.bytes;
}

int TargetLayer(const game_production::SceneViewHost& host) {
    for (auto it = host.selection.rbegin(); it != host.selection.rend(); ++it) {
        if (LayerOf(host.scene, *it)) return *it;
    }
    return 0;
}

}  // namespace rm::game_x_tilemap
