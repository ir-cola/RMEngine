#include "rm_engine/tilemap/runtime/Tileset.h"

#include "base/Json.h"

#include <algorithm>
#include <format>
#include <tuple>

namespace rm::tilemap {

std::optional<Tileset> ReadTileset(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns) {
    json::ParseError pe;
    const auto v = json::Parse(bytes, &pe);
    if (!v) {
        if (error) *error = std::format("JSON として読めません ({} 行目: {})", pe.line, pe.message);
        return std::nullopt;
    }
    if (!v->IsObject() || v->Get("kind").AsString() != kTilesetKind) {
        if (error) *error = "タイルセットのファイルではありません";
        return std::nullopt;
    }
    Tileset t;
    reflect::Unknowns u;
    reflect::Load(t, *v, &u);
    NormalizeIds(t);
    if (unknowns) *unknowns = std::move(u);
    return t;
}

std::string WriteTileset(const Tileset& t, const reflect::Unknowns* unknowns) {
    Tileset out = t;
    out.kind = kTilesetKind;
    return json::Write(reflect::Save(out, unknowns));
}

bool NormalizeIds(Tileset& t) {
    bool changed = false;
    std::vector<bool> used(kMaxSourceId + 1, false);
    std::vector<size_t> again;  // 番号を付け直すもの
    for (size_t i = 0; i < t.sources.size(); ++i) {
        const int id = t.sources[i].id;
        if (id < 1 || id > kMaxSourceId || used[static_cast<size_t>(id)]) {
            again.push_back(i);
        } else {
            used[static_cast<size_t>(id)] = true;
        }
    }
    int next = 1;
    for (const size_t i : again) {
        while (next <= kMaxSourceId && used[static_cast<size_t>(next)]) ++next;
        if (next > kMaxSourceId) break;  // (256 枚目から先は番号が無い: そのまま。描かれない)
        t.sources[i].id = next;
        used[static_cast<size_t>(next)] = true;
        changed = true;
    }
    return changed;
}

int NewSourceId(const Tileset& t) {
    for (int id = 1; id <= kMaxSourceId; ++id) {
        if (!FindSource(t, id)) return id;
    }
    return 0;
}

const TileSource* FindSource(const Tileset& t, int id) {
    for (const TileSource& s : t.sources) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const TileInfo* FindTile(const Tileset& t, int source, int column, int row) {
    for (const TileInfo& i : t.tiles) {
        if (i.source == source && i.column == column && i.row == row) return &i;
    }
    return nullptr;
}

TileInfo TileOrDefault(const Tileset& t, int source, int column, int row) {
    if (const TileInfo* i = FindTile(t, source, column, row)) return *i;
    TileInfo out;
    out.source = source;
    out.column = column;
    out.row = row;
    return out;
}

bool IsDefaultTile(const TileInfo& i) {
    return i.name.empty() && i.shape == TileShape::None && i.points.empty() && i.tags.empty() && i.data.empty();
}

void SetTile(Tileset& t, TileInfo info) {
    auto same = [&](const TileInfo& i) { return i.source == info.source && i.column == info.column && i.row == info.row; };
    const auto at = std::find_if(t.tiles.begin(), t.tiles.end(), same);
    if (IsDefaultTile(info)) {
        if (at != t.tiles.end()) t.tiles.erase(at);
        return;
    }
    if (at != t.tiles.end()) {
        *at = std::move(info);
        return;
    }
    auto key = [](const TileInfo& i) { return std::tuple(i.source, i.row, i.column); };
    const auto before = std::find_if(t.tiles.begin(), t.tiles.end(), [&](const TileInfo& i) { return key(i) > key(info); });
    t.tiles.insert(before, std::move(info));
}

const TileField* FindField(const Tileset& t, std::string_view name) {
    for (const TileField& f : t.fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

std::string TileData(const Tileset& t, const TileInfo& info, std::string_view field) {
    for (const TileDataValue& v : info.data) {
        if (v.name == field) return v.value;
    }
    const TileField* f = FindField(t, field);
    return f ? f->defaultValue : std::string();
}

void SetTileData(const Tileset& t, TileInfo& info, std::string_view field, std::string value) {
    std::erase_if(info.data, [&](const TileDataValue& v) { return v.name == field; });
    const TileField* f = FindField(t, field);
    if (f && f->defaultValue == value) return;  // (既定値: 持たない)
    info.data.push_back({std::string(field), std::move(value)});
}

space2d::GridCut CutOf(const TileSource& s) {
    auto px = [](int v, int least) { return static_cast<uint32_t>(std::max(v, least)); };
    return {px(s.tileWidth, 1), px(s.tileHeight, 1), px(s.margin, 0), px(s.spacing, 0)};
}

}  // namespace rm::tilemap
