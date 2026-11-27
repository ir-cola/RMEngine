#include "rm_engine/navigation/runtime/Bake.h"

#include "rm_engine/navigation/runtime/DetourShared.h"

#pragma warning(push, 0)
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourTileCacheBuilder.h"
#include "Recast.h"
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rm::navigation {

namespace {

constexpr int kVertsPerPoly = 6;      // 多角形の角の数の上限 (Detour の決まりの中で一番多く)
using detail::kWalk;  // 多角形の印: 歩ける (Query.h が通れないエリアに kBlocked を足す)

float Finite(float v, float def) { return std::isfinite(v) ? v : def; }

// Recast の確保したものを、手放し忘れないように。
struct Heightfield {
    rcHeightfield* p = rcAllocHeightfield();
    ~Heightfield() { rcFreeHeightField(p); }
};
struct Compact {
    rcCompactHeightfield* p = rcAllocCompactHeightfield();
    ~Compact() { rcFreeCompactHeightfield(p); }
};
struct Contours {
    rcContourSet* p = rcAllocContourSet();
    ~Contours() { rcFreeContourSet(p); }
};
struct PolyMesh {
    rcPolyMesh* p = rcAllocPolyMesh();
    ~PolyMesh() { rcFreePolyMesh(p); }
};
struct Detail {
    rcPolyMeshDetail* p = rcAllocPolyMeshDetail();
    ~Detail() { rcFreePolyMeshDetail(p); }
};

struct Prepared {
    std::vector<float> verts;        // x, y, z
    std::vector<int> tris;           // 三角形ごとに 3 つ (表はそのまま)
    std::vector<unsigned char> areas;
    std::vector<float> triMin, triMax;  // 三角形ごとの x, z の囲み (区画に入るかを速く見る)
    detail::LinkArrays links;  // つなぎの線 (DetourShared.h)
};

// 区画の指紋: その区画 (まわりの border の分も) に触れる三角形・エリアの形・始めが区画にあるつなぎの線・体の形。
uint64_t TilePrint(const Prepared& in, const NavInput& input, const rcConfig& cfg, const NavAgent& agent, int tileCells, bool obstacles, int tx, int tz,
                   float x0, float z0, float x1, float z1) {
    Hasher h;
    h.Int(tx), h.Int(tz), h.Int(tileCells), h.Int(obstacles ? 1 : 0);
    for (const float f : {agent.radius, agent.height, agent.maxSlope, agent.maxClimb, agent.cellSize, agent.cellHeight}) h.Float(f);
    const float border = static_cast<float>(cfg.borderSize) * cfg.cs;
    const float bx0 = x0 - border, bz0 = z0 - border, bx1 = x1 + border, bz1 = z1 + border;
    for (size_t i = 0; i < in.areas.size(); ++i) {
        if (in.triMax[i * 2] < bx0 || in.triMin[i * 2] > bx1 || in.triMax[i * 2 + 1] < bz0 || in.triMin[i * 2 + 1] > bz1) continue;
        for (int k = 0; k < 3; ++k) {
            const float* v = &in.verts[static_cast<size_t>(in.tris[i * 3 + static_cast<size_t>(k)]) * 3];
            h.Float(v[0]), h.Float(v[1]), h.Float(v[2]);
        }
        h.Int(in.areas[i]);
    }
    for (const NavAreaVolume& v : input.volumes) {
        float vx0 = 1e30f, vz0 = 1e30f, vx1 = -1e30f, vz1 = -1e30f;
        for (const Vec2& p : v.outline) vx0 = std::min(vx0, p.x), vz0 = std::min(vz0, p.y), vx1 = std::max(vx1, p.x), vz1 = std::max(vz1, p.y);
        if (vx1 < bx0 || vx0 > bx1 || vz1 < bz0 || vz0 > bz1) continue;
        for (const Vec2& p : v.outline) h.Float(p.x), h.Float(p.y);
        h.Float(v.minY), h.Float(v.maxY), h.Int(v.area);
    }
    for (size_t i = 0; i < in.links.ids.size(); ++i) {
        const float* v = &in.links.verts[i * 6];
        auto inside = [&](const float* p) { return p[0] >= x0 && p[0] < x1 && p[2] >= z0 && p[2] < z1; };
        if (!inside(v) && !(in.links.dirs[i] && inside(v + 3))) continue;
        for (int k = 0; k < 6; ++k) h.Float(v[k]);
        h.Float(in.links.radius[i]), h.Int(in.links.areas[i]), h.Int(in.links.dirs[i]), h.Int(in.links.ids[i]);
    }
    return h.Value();
}

// 1 つの区画 (x, z) を焼く。歩ける所が無ければ true で `out.data` は空。
bool BakeTile(const Prepared& in, const std::vector<NavAreaVolume>& volumes, const rcConfig& base, const NavBounds& bounds, float tileSize, int tx,
              int tz, const NavAgent& agent, bool obstacles, NavTile& out, int& polys, std::string* error) {
    rcConfig cfg = base;
    cfg.bmin[0] = bounds.min.x + static_cast<float>(tx) * tileSize;
    cfg.bmin[1] = bounds.min.y;
    cfg.bmin[2] = bounds.min.z + static_cast<float>(tz) * tileSize;
    cfg.bmax[0] = cfg.bmin[0] + tileSize;
    cfg.bmax[1] = bounds.max.y;
    cfg.bmax[2] = cfg.bmin[2] + tileSize;
    const float border = static_cast<float>(cfg.borderSize) * cfg.cs;
    cfg.bmin[0] -= border, cfg.bmin[2] -= border;
    cfg.bmax[0] += border, cfg.bmax[2] += border;
    // 区画のふちの外に border のマスを足した分 (隣の区画とつながるように、まわりの形も見る)。
    out.x = tx, out.z = tz;
    out.data.clear();
    out.layers.clear();
    polys = 0;

    std::vector<int> tris;
    std::vector<unsigned char> areas;
    const size_t n = in.areas.size();
    for (size_t i = 0; i < n; ++i) {
        if (in.triMax[i * 2] < cfg.bmin[0] || in.triMin[i * 2] > cfg.bmax[0] || in.triMax[i * 2 + 1] < cfg.bmin[2] || in.triMin[i * 2 + 1] > cfg.bmax[2]) {
            continue;
        }
        tris.insert(tris.end(), in.tris.begin() + static_cast<std::ptrdiff_t>(i * 3), in.tris.begin() + static_cast<std::ptrdiff_t>(i * 3 + 3));
        areas.push_back(in.areas[i]);
    }
    if (tris.empty()) return true;

    rcContext ctx(false);
    Heightfield hf;
    if (!hf.p || !rcCreateHeightfield(&ctx, *hf.p, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        if (error) *error = "ナビメッシュを焼けません (箱の場所が作れません)";
        return false;
    }
    const int ntris = static_cast<int>(areas.size());
    const int nverts = static_cast<int>(in.verts.size() / 3);
    // 2: 急すぎる坂は歩けない (エリアの番号はそのまま。歩けない三角形は 0 に)。
    rcClearUnwalkableTriangles(&ctx, cfg.walkableSlopeAngle, in.verts.data(), nverts, tris.data(), ntris, areas.data());
    // 1: 箱に塗る。
    if (!rcRasterizeTriangles(&ctx, in.verts.data(), nverts, tris.data(), areas.data(), ntris, *hf.p, cfg.walkableClimb)) {
        if (error) *error = "ナビメッシュを焼けません (三角形を箱に塗れません)";
        return false;
    }
    // 2: 段差の上の低い物・崖のふち・天井の低い所。
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf.p);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf.p);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf.p);
    Compact chf;
    if (!chf.p || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf.p, *chf.p)) {
        if (error) *error = "ナビメッシュを焼けません (歩ける箱をまとめられません)";
        return false;
    }
    // 3: 壁から半径の分だけ削る。
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf.p)) {
        if (error) *error = "ナビメッシュを焼けません (壁から離せません)";
        return false;
    }
    // エリアの形の中の床に番号を塗る (領域に分ける前に)。
    for (const NavAreaVolume& v : volumes) {
        if (v.outline.size() < 3 || !(v.maxY > v.minY)) continue;
        std::vector<float> pts;
        pts.reserve(v.outline.size() * 3);
        for (const Vec2& p : v.outline) pts.insert(pts.end(), {p.x, v.minY, p.y});
        rcMarkConvexPolyArea(&ctx, pts.data(), static_cast<int>(v.outline.size()), v.minY, v.maxY, static_cast<unsigned char>(v.area | kAreaApart), *chf.p);
    }
    // 動く障害物を使う: ここで高さの層にして持つ (4 から後は、ゲームの中で DetourTileCache が層から作る)。
    if (obstacles) {
        rcHeightfieldLayerSet* lset = rcAllocHeightfieldLayerSet();
        const bool built = lset && rcBuildHeightfieldLayers(&ctx, *chf.p, cfg.borderSize, cfg.walkableHeight, *lset);
        if (!built) {
            rcFreeHeightfieldLayerSet(lset);
            if (error) *error = "ナビメッシュを焼けません (動く障害物の層を作れません)";
            return false;
        }
        detail::RawCompressor comp;
        for (int i = 0; i < lset->nlayers; ++i) {
            const rcHeightfieldLayer& layer = lset->layers[i];
            dtTileCacheLayerHeader header{};
            header.magic = DT_TILECACHE_MAGIC;
            header.version = DT_TILECACHE_VERSION;
            header.tx = tx, header.ty = tz, header.tlayer = i;
            rcVcopy(header.bmin, layer.bmin);
            rcVcopy(header.bmax, layer.bmax);
            header.width = static_cast<unsigned char>(layer.width), header.height = static_cast<unsigned char>(layer.height);
            header.minx = static_cast<unsigned char>(layer.minx), header.maxx = static_cast<unsigned char>(layer.maxx);
            header.miny = static_cast<unsigned char>(layer.miny), header.maxy = static_cast<unsigned char>(layer.maxy);
            header.hmin = static_cast<unsigned short>(layer.hmin), header.hmax = static_cast<unsigned short>(layer.hmax);
            unsigned char* data = nullptr;
            int size = 0;
            if (dtStatusFailed(dtBuildTileCacheLayer(&comp, &header, layer.heights, layer.areas, layer.cons, &data, &size)) || !data) {
                rcFreeHeightfieldLayerSet(lset);
                if (error) *error = "ナビメッシュを焼けません (動く障害物の層を作れません)";
                return false;
            }
            out.layers.emplace_back(reinterpret_cast<const char*>(data), static_cast<size_t>(size));
            dtFree(data);
        }
        rcFreeHeightfieldLayerSet(lset);
        return true;
    }
    // 4: 領域 (分水嶺: 一番きれいに分ける)。
    if (!rcBuildDistanceField(&ctx, *chf.p) || !rcBuildRegions(&ctx, *chf.p, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) {
        if (error) *error = "ナビメッシュを焼けません (床を領域に分けられません)";
        return false;
    }
    // 5: 輪郭。
    Contours cset;
    if (!cset.p || !rcBuildContours(&ctx, *chf.p, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset.p)) {
        if (error) *error = "ナビメッシュを焼けません (縁をたどれません)";
        return false;
    }
    if (cset.p->nconts == 0) return true;
    // 6: 凸の多角形。
    PolyMesh pmesh;
    if (!pmesh.p || !rcBuildPolyMesh(&ctx, *cset.p, cfg.maxVertsPerPoly, *pmesh.p)) {
        if (error) *error = "ナビメッシュを焼けません (多角形を作れません)";
        return false;
    }
    // 7: 細かい高さ。
    Detail dmesh;
    if (!dmesh.p || !rcBuildPolyMeshDetail(&ctx, *pmesh.p, *chf.p, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh.p)) {
        if (error) *error = "ナビメッシュを焼けません (細かい高さを作れません)";
        return false;
    }
    if (pmesh.p->npolys == 0) return true;
    for (int i = 0; i < pmesh.p->npolys; ++i) {
        pmesh.p->areas[i] = static_cast<unsigned char>(pmesh.p->areas[i] & ~kAreaApart);
        pmesh.p->flags[i] = pmesh.p->areas[i] != RC_NULL_AREA ? kWalk : 0;
    }

    dtNavMeshCreateParams params{};
    params.verts = pmesh.p->verts;
    params.vertCount = pmesh.p->nverts;
    params.polys = pmesh.p->polys;
    params.polyAreas = pmesh.p->areas;
    params.polyFlags = pmesh.p->flags;
    params.polyCount = pmesh.p->npolys;
    params.nvp = pmesh.p->nvp;
    params.detailMeshes = dmesh.p->meshes;
    params.detailVerts = dmesh.p->verts;
    params.detailVertsCount = dmesh.p->nverts;
    params.detailTris = dmesh.p->tris;
    params.detailTriCount = dmesh.p->ntris;
    params.walkableHeight = agent.height;
    params.walkableRadius = agent.radius;
    params.walkableClimb = agent.maxClimb;
    params.tileX = tx;
    params.tileY = tz;
    params.tileLayer = 0;
    rcVcopy(params.bmin, pmesh.p->bmin);
    rcVcopy(params.bmax, pmesh.p->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;
    in.links.Attach(params);
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &data, &size) || !data) {
        if (error) *error = "ナビメッシュを焼けません (Detour のデータを作れません。区画が細かすぎるかもしれません)";
        return false;
    }
    out.data.assign(reinterpret_cast<const char*>(data), static_cast<size_t>(size));
    dtFree(data);
    polys = pmesh.p->npolys;
    return true;
}

}  // namespace

NavAgent Sanitized(const NavAgent& a) {
    NavAgent s;
    s.radius = std::clamp(Finite(a.radius, 0.3f), 0.f, 100.f);
    s.height = std::clamp(Finite(a.height, 1.8f), 0.05f, 1000.f);
    s.maxSlope = std::clamp(Finite(a.maxSlope, 45.f), 0.f, 89.f);
    s.maxClimb = std::clamp(Finite(a.maxClimb, 0.3f), 0.f, 100.f);
    s.cellSize = std::clamp(Finite(a.cellSize, 0.1f), 0.01f, 100.f);
    s.cellHeight = std::clamp(Finite(a.cellHeight, s.cellSize * 0.5f), 0.005f, 100.f);
    return s;
}

bool Bake(const NavInput& input, const BakeOptions& options, std::stop_token stop, const std::function<void(int, int)>& progress, NavData& out,
          std::string* error) {
    out = {};
    const NavAgent agent = Sanitized(options.agent);
    const NavBounds b = options.bounds;
    if (!b.Valid() || !std::isfinite(b.min.x + b.min.y + b.min.z + b.max.x + b.max.y + b.max.z)) {
        if (error) *error = "ナビメッシュの範囲の大きさが 0 です";
        return false;
    }
    const int tileCells = std::clamp(options.tileCells, 16, 512);
    const float tileSize = static_cast<float>(tileCells) * agent.cellSize;
    const int tilesX = static_cast<int>(std::ceil((b.max.x - b.min.x) / tileSize));
    const int tilesZ = static_cast<int>(std::ceil((b.max.z - b.min.z) / tileSize));
    if (tilesX < 1 || tilesZ < 1 || tilesX > kMaxTilesPerSide || tilesZ > kMaxTilesPerSide) {
        if (error) *error = "ナビメッシュの範囲が細かさに比べて広すぎます (範囲を小さくするか、細かさを荒くしてください)";
        return false;
    }

    // 三角形を Recast の形に。表の向きはそのまま: エンジンの表 (3D 空間の Primitives.cpp の FaceOut) も Recast も、
    // (b - a) x (c - a) が外 (床なら上) を向く側を表とする。
    Prepared prep;
    const size_t ntris = std::min(input.indices.size() / 3, input.areas.size());
    prep.verts.reserve(input.points.size() * 3);
    for (const Vec3& p : input.points) prep.verts.insert(prep.verts.end(), {p.x, p.y, p.z});
    for (size_t i = 0; i < ntris; ++i) {
        const uint32_t a = input.indices[i * 3], c = input.indices[i * 3 + 1], d = input.indices[i * 3 + 2];
        if (a >= input.points.size() || c >= input.points.size() || d >= input.points.size()) continue;
        const Vec3 pa = input.points[a], pc = input.points[c], pd = input.points[d];
        if (!std::isfinite(pa.x + pa.y + pa.z + pc.x + pc.y + pc.z + pd.x + pd.y + pd.z)) continue;
        prep.tris.insert(prep.tris.end(), {static_cast<int>(a), static_cast<int>(c), static_cast<int>(d)});
        prep.areas.push_back(input.areas[i] > kAreaNormal ? kAreaNormal : input.areas[i]);
        prep.triMin.insert(prep.triMin.end(), {std::min({pa.x, pc.x, pd.x}), std::min({pa.z, pc.z, pd.z})});
        prep.triMax.insert(prep.triMax.end(), {std::max({pa.x, pc.x, pd.x}), std::max({pa.z, pc.z, pd.z})});
    }
    prep.links.Build(input.links);

    rcConfig cfg{};
    cfg.cs = agent.cellSize;
    cfg.ch = agent.cellHeight;
    cfg.walkableSlopeAngle = agent.maxSlope;
    cfg.walkableHeight = std::max(3, static_cast<int>(std::ceil(agent.height / cfg.ch)));
    cfg.walkableClimb = static_cast<int>(std::floor(agent.maxClimb / cfg.ch));
    cfg.walkableRadius = static_cast<int>(std::ceil(agent.radius / cfg.cs));
    cfg.maxEdgeLen = static_cast<int>(12.f / cfg.cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea = 8 * 8;
    cfg.mergeRegionArea = 20 * 20;
    cfg.maxVertsPerPoly = kVertsPerPoly;
    cfg.tileSize = tileCells;
    cfg.borderSize = cfg.walkableRadius + 3;
    cfg.width = cfg.tileSize + cfg.borderSize * 2;
    cfg.height = cfg.tileSize + cfg.borderSize * 2;
    cfg.detailSampleDist = cfg.cs * 6.f;
    cfg.detailSampleMaxError = cfg.ch * 1.f;

    const int total = tilesX * tilesZ;
    std::vector<NavTile> tiles(static_cast<size_t>(total));
    std::vector<int> polys(static_cast<size_t>(total), 0);
    // 区画ごとの指紋。前に焼いた物と形が同じなら、指紋の同じ区画は前の物をそのまま使う。
    std::vector<uint64_t> prints(static_cast<size_t>(total));
    std::vector<char> reuse(static_cast<size_t>(total), 0);
    for (int i = 0; i < total; ++i) {
        const int tx = i % tilesX, tz = i / tilesX;
        const float x0 = b.min.x + static_cast<float>(tx) * tileSize, z0 = b.min.z + static_cast<float>(tz) * tileSize;
        prints[static_cast<size_t>(i)] = TilePrint(prep, input, cfg, agent, tileCells, options.obstacles, tx, tz, x0, z0, x0 + tileSize, z0 + tileSize);
    }
    const NavData* prev = options.previous;
    if (prev && prev->tilesX == tilesX && prev->tilesZ == tilesZ && prev->tileSize == tileSize && prev->tileCells == tileCells &&
        prev->obstacles == options.obstacles && prev->agent == agent && prev->bounds.min == b.min && prev->bounds.max == b.max &&
        prev->prints.size() == prints.size()) {
        for (int i = 0; i < total; ++i) {
            if (prev->prints[static_cast<size_t>(i)] != prints[static_cast<size_t>(i)]) continue;
            reuse[static_cast<size_t>(i)] = 1;
            tiles[static_cast<size_t>(i)].x = i % tilesX, tiles[static_cast<size_t>(i)].z = i / tilesX;
            for (const NavTile& t : prev->tiles) {
                if (t.x == i % tilesX && t.z == i / tilesX) tiles[static_cast<size_t>(i)] = t;
            }
        }
    }
    std::vector<int> todo;
    for (int i = 0; i < total; ++i) {
        if (!reuse[static_cast<size_t>(i)]) todo.push_back(i);
    }
    const int count = static_cast<int>(todo.size());
    if (options.bakedTiles) *options.bakedTiles = count;
    std::atomic<int> next{0}, done{0};
    std::atomic<bool> failed{false};
    std::mutex errorLock;
    std::string firstError;
    auto work = [&] {
        for (;;) {
            if (stop.stop_requested() || failed.load()) return;
            const int k = next.fetch_add(1);
            if (k >= count) return;
            const int i = todo[static_cast<size_t>(k)];
            std::string why;
            if (!BakeTile(prep, input.volumes, cfg, b, tileSize, i % tilesX, i / tilesX, agent, options.obstacles, tiles[static_cast<size_t>(i)],
                          polys[static_cast<size_t>(i)], &why)) {
                std::lock_guard lock(errorLock);
                if (!failed.exchange(true)) firstError = why;
                return;
            }
            const int d = done.fetch_add(1) + 1;
            if (progress) progress(d, count);
        }
    };
    int threads = options.threads;
    if (threads <= 0) threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 8);
    threads = std::clamp(threads, 1, std::max(1, count));
    {
        std::vector<std::jthread> pool;
        for (int t = 1; t < threads; ++t) pool.emplace_back(work);
        work();
    }
    if (stop.stop_requested()) {
        if (error) error->clear();
        return false;
    }
    if (failed.load()) {
        if (error) *error = firstError;
        return false;
    }
    out.bounds = b;
    out.agent = agent;
    out.tileSize = tileSize;
    out.tilesX = tilesX;
    out.tilesZ = tilesZ;
    out.tileCells = tileCells;
    out.obstacles = options.obstacles;
    out.links = input.links;
    out.prints = std::move(prints);
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (tiles[i].data.empty() && tiles[i].layers.empty()) continue;
        if (tiles[i].data.size() >= sizeof(dtMeshHeader)) {  // (前の物を使った区画も、区画のデータの頭から数える)
            dtMeshHeader h;
            std::memcpy(&h, tiles[i].data.data(), sizeof h);
            out.maxPolysPerTile = std::max(out.maxPolysPerTile, h.polyCount);
        }
        out.tiles.push_back(std::move(tiles[i]));
    }
    if (out.obstacles) out.maxPolysPerTile = std::max(out.maxPolysPerTile, 1 << 12);  // (層から作る多角形の数は読むまで分からない)
    (void)polys;
    return true;
}

}  // namespace rm::navigation
