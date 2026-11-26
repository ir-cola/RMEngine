#include "rm_engine/navigation/runtime/Bake.h"

#pragma warning(push, 0)
#include "DetourNavMeshBuilder.h"
#include "Recast.h"
#pragma warning(pop)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rm::navigation {

namespace {

constexpr int kVertsPerPoly = 6;      // 多角形の角の数の上限 (Detour の決まりの中で一番多く)
constexpr unsigned short kWalk = 1;   // 多角形の印: 歩ける (Query.h が通れないエリアに 2 を足す)

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
};

// 1 つの区画 (x, z) を焼く。歩ける所が無ければ true で `out.data` は空。
bool BakeTile(const Prepared& in, const std::vector<NavAreaVolume>& volumes, const rcConfig& base, const NavBounds& bounds, float tileSize, int tx,
              int tz, const NavAgent& agent, NavTile& out, int& polys, std::string* error) {
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
        rcMarkConvexPolyArea(&ctx, pts.data(), static_cast<int>(v.outline.size()), v.minY, v.maxY, v.area, *chf.p);
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
    for (int i = 0; i < pmesh.p->npolys; ++i) pmesh.p->flags[i] = pmesh.p->areas[i] != RC_NULL_AREA ? kWalk : 0;

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
    std::atomic<int> next{0}, done{0};
    std::atomic<bool> failed{false};
    std::mutex errorLock;
    std::string firstError;
    auto work = [&] {
        for (;;) {
            if (stop.stop_requested() || failed.load()) return;
            const int i = next.fetch_add(1);
            if (i >= total) return;
            std::string why;
            if (!BakeTile(prep, input.volumes, cfg, b, tileSize, i % tilesX, i / tilesX, agent, tiles[static_cast<size_t>(i)], polys[static_cast<size_t>(i)],
                          &why)) {
                std::lock_guard lock(errorLock);
                if (!failed.exchange(true)) firstError = why;
                return;
            }
            const int d = done.fetch_add(1) + 1;
            if (progress) progress(d, total);
        }
    };
    int threads = options.threads;
    if (threads <= 0) threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 8);
    threads = std::clamp(threads, 1, std::max(1, total));
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
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (tiles[i].data.empty()) continue;
        out.maxPolysPerTile = std::max(out.maxPolysPerTile, polys[i]);
        out.tiles.push_back(std::move(tiles[i]));
    }
    return true;
}

}  // namespace rm::navigation
