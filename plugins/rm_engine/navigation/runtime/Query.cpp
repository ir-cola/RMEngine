#include "rm_engine/navigation/runtime/Query.h"

#include "rm_engine/navigation/runtime/DetourShared.h"

#pragma warning(push, 0)
#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourTileCache.h"
#pragma warning(pop)

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <random>

namespace rm::navigation {

namespace {

using detail::kBlocked;
using detail::kWalk;
constexpr int kMaxNodes = 4096;         // A* が一度に覚える多角形の数
constexpr int kMaxPath = 1024;          // 通路の多角形の数の上限
constexpr int kMaxCorners = 512;        // 角の点の数の上限

struct Deleter {
    void operator()(dtNavMesh* p) const { dtFreeNavMesh(p); }
    void operator()(dtNavMeshQuery* p) const { dtFreeNavMeshQuery(p); }
    void operator()(dtTileCache* p) const { dtFreeTileCache(p); }
};

// 層から作る区画の多角形の印とつなぎの線 (DetourTileCache が区画を作るたびに呼ぶ)。印は作った後に ApplyBlocking でかけ直す。
struct MeshProcess final : dtTileCacheMeshProcess {
    detail::LinkArrays links;
    void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override {
        for (int i = 0; i < params->polyCount; ++i) {
            polyAreas[i] = static_cast<unsigned char>(polyAreas[i] & ~kAreaApart);
            polyFlags[i] = polyAreas[i] != DT_TILECACHE_NULL_AREA ? kWalk : 0;
        }
        links.Attach(*params);
    }
};

bool Inside(const NavAreaVolume& v, Vec3 p) {
    if (p.y < v.minY - 0.5f || p.y > v.maxY + 0.5f || v.outline.size() < 3) return false;
    bool in = false;  // (偶奇の規則)
    for (size_t i = 0, j = v.outline.size() - 1; i < v.outline.size(); j = i++) {
        const Vec2 a = v.outline[i], b = v.outline[j];
        if ((a.y > p.z) != (b.y > p.z) && p.x < (b.x - a.x) * (p.z - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

void Set(float* f, Vec3 v) { f[0] = v.x, f[1] = v.y, f[2] = v.z; }
Vec3 Get(const float* f) { return {f[0], f[1], f[2]}; }
float Dist(Vec3 a, Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

// でたらめな点の種 (Detour は状態の無い関数を求める): このスレッドの 1 回の問いの間だけ。
thread_local std::mt19937 tRandom;
float Frand() { return std::uniform_real_distribution<float>(0.f, 1.f)(tRandom) * 0.99999f; }

}  // namespace

struct NavMesh::Impl {
    std::unique_ptr<dtNavMesh, Deleter> mesh;
    std::unique_ptr<dtNavMeshQuery, Deleter> query;
    // 動く障害物 (層から作る)。順番: tileCache は alloc / comp / proc を指すので、それより後に消える物を先に書く。
    dtTileCacheAlloc alloc;
    detail::RawCompressor comp;
    MeshProcess proc;
    std::unique_ptr<dtTileCache, Deleter> tileCache;
    float agentRadius = 0;
    bool pending = false;  // 足した・外した障害物の区画を、まだ作り直していない
    std::bitset<64> blocked;
    std::vector<NavAreaVolume> closed;
    std::vector<uint32_t> disabledLinks;
    std::vector<NavLinkInput> links;
    size_t tiles = 0, polys = 0;

    void Count() {
        polys = 0;
        tiles = 0;
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            const dtMeshTile* t = static_cast<const dtNavMesh*>(mesh.get())->getTile(i);
            if (!t || !t->header) continue;
            ++tiles;
            polys += static_cast<size_t>(t->header->polyCount - t->header->offMeshConCount);
        }
    }
    // 多角形の印をかけ直す: 歩ける + (通れないエリア・閉じた扉・切ったつなぎの線なら) 通れない。
    void ApplyBlocking() {
        if (!mesh) return;
        dtNavMesh* m = mesh.get();
        EachPoly([&](const dtMeshTile& t, int p) {
            const dtPoly& poly = t.polys[p];
            bool off = blocked[poly.getArea()];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                for (int k = 0; k < t.header->offMeshConCount && !off; ++k) {
                    const dtOffMeshConnection& c = t.offMeshCons[k];
                    if (c.poly == p && std::find(disabledLinks.begin(), disabledLinks.end(), c.userId) != disabledLinks.end()) off = true;
                }
            } else if (!closed.empty()) {
                Vec3 mid{0, 0, 0};
                for (int j = 0; j < poly.vertCount; ++j) {
                    const float* v = &t.verts[poly.verts[j] * 3];
                    mid.x += v[0], mid.y += v[1], mid.z += v[2];
                }
                const float n = static_cast<float>(std::max<int>(1, poly.vertCount));
                mid = {mid.x / n, mid.y / n, mid.z / n};
                for (const NavAreaVolume& v : closed) off = off || Inside(v, mid);
            }
            m->setPolyFlags(m->getPolyRefBase(&t) | static_cast<dtPolyRef>(p), static_cast<unsigned short>(kWalk | (off ? kBlocked : 0)));
        });
    }

    dtQueryFilter Filter(const NavFilter& f) const {
        dtQueryFilter q;
        detail::ApplyFilter(f, q);
        return q;
    }

    template <class Fn>
    void EachPoly(Fn&& fn) const {
        const dtNavMesh* m = mesh.get();
        for (int i = 0; i < m->getMaxTiles(); ++i) {
            const dtMeshTile* t = m->getTile(i);
            if (!t || !t->header) continue;
            for (int p = 0; p < t->header->polyCount; ++p) fn(*t, p);
        }
    }
};

NavMesh::NavMesh() : impl_(std::make_unique<Impl>()) {}
NavMesh::~NavMesh() = default;

bool NavMesh::Load(const NavData& data, std::string* error) {
    impl_ = std::make_unique<Impl>();
    impl_->links = data.links;
    impl_->agentRadius = data.agent.radius;
    if (data.tiles.empty()) return true;
    if (data.obstacles) return LoadLayers(data, error);
    dtNavMeshParams params{};
    params.orig[0] = data.bounds.min.x, params.orig[1] = data.bounds.min.y, params.orig[2] = data.bounds.min.z;
    params.tileWidth = data.tileSize;
    params.tileHeight = data.tileSize;
    params.maxTiles = std::max(1, std::max(data.tilesX * data.tilesZ, static_cast<int>(data.tiles.size())));
    params.maxPolys = std::max(1, data.maxPolysPerTile);
    std::unique_ptr<dtNavMesh, Deleter> mesh(dtAllocNavMesh());
    if (!mesh || !(data.tileSize > 0) || dtStatusFailed(mesh->init(&params))) {
        if (error) *error = "ナビメッシュを読めません (Detour が受け付けません)";
        return false;
    }
    size_t polys = 0;
    for (const NavTile& t : data.tiles) {
        if (t.data.empty()) continue;
        auto* copy = static_cast<unsigned char*>(dtAlloc(static_cast<int>(t.data.size()), DT_ALLOC_PERM));
        if (!copy) {
            if (error) *error = "ナビメッシュを読めません (メモリが足りません)";
            return false;
        }
        std::memcpy(copy, t.data.data(), t.data.size());
        dtTileRef ref = 0;
        if (dtStatusFailed(mesh->addTile(copy, static_cast<int>(t.data.size()), DT_TILE_FREE_DATA, 0, &ref))) {
            dtFree(copy);
            if (error) *error = "ナビメッシュを読めません (区画が壊れています)";
            return false;
        }
        if (const dtMeshTile* tile = mesh->getTileByRef(ref); tile && tile->header) polys += static_cast<size_t>(tile->header->polyCount);
    }
    std::unique_ptr<dtNavMeshQuery, Deleter> query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(mesh.get(), kMaxNodes))) {
        if (error) *error = "ナビメッシュを読めません (道を探す準備ができません)";
        return false;
    }
    impl_->mesh = std::move(mesh);
    impl_->query = std::move(query);
    impl_->Count();
    (void)polys;
    return true;
}

// 動く障害物を使うナビメッシュ: 層を DetourTileCache に入れ、区画ごとに層から作る (Detour の見本 TempObstacles と同じ流れ)。
bool NavMesh::LoadLayers(const NavData& data, std::string* error) {
    int layers = 0;
    for (const NavTile& t : data.tiles) layers += static_cast<int>(t.layers.size());
    auto fail = [&](const char* why) {
        impl_ = std::make_unique<Impl>();
        if (error) *error = why;
        return false;
    };
    if (layers == 0) return true;
    impl_->proc.links.Build(data.links);
    dtTileCacheParams tcp{};
    tcp.orig[0] = data.bounds.min.x, tcp.orig[1] = data.bounds.min.y, tcp.orig[2] = data.bounds.min.z;
    tcp.cs = data.agent.cellSize;
    tcp.ch = data.agent.cellHeight;
    tcp.width = data.tileCells;
    tcp.height = data.tileCells;
    tcp.walkableHeight = data.agent.height;
    tcp.walkableRadius = data.agent.radius;
    tcp.walkableClimb = data.agent.maxClimb;
    tcp.maxSimplificationError = 1.3f;
    tcp.maxTiles = layers * 2;
    tcp.maxObstacles = 256;
    std::unique_ptr<dtTileCache, Deleter> tc(dtAllocTileCache());
    if (!tc || dtStatusFailed(tc->init(&tcp, &impl_->alloc, &impl_->comp, &impl_->proc))) return fail("ナビメッシュを読めません (動く障害物の準備ができません)");
    for (const NavTile& t : data.tiles) {
        for (const std::string& layer : t.layers) {
            auto* copy = static_cast<unsigned char*>(dtAlloc(static_cast<int>(layer.size()), DT_ALLOC_PERM));
            if (!copy) return fail("ナビメッシュを読めません (メモリが足りません)");
            std::memcpy(copy, layer.data(), layer.size());
            dtCompressedTileRef ref = 0;
            if (dtStatusFailed(tc->addTile(copy, static_cast<int>(layer.size()), DT_COMPRESSEDTILE_FREE_DATA, &ref))) {
                dtFree(copy);
                return fail("ナビメッシュを読めません (層が壊れています)");
            }
        }
    }
    dtNavMeshParams params{};
    params.orig[0] = data.bounds.min.x, params.orig[1] = data.bounds.min.y, params.orig[2] = data.bounds.min.z;
    params.tileWidth = data.tileSize;
    params.tileHeight = data.tileSize;
    params.maxTiles = layers * 2;
    params.maxPolys = 1 << 14;
    std::unique_ptr<dtNavMesh, Deleter> mesh(dtAllocNavMesh());
    if (!mesh || dtStatusFailed(mesh->init(&params))) return fail("ナビメッシュを読めません (Detour が受け付けません)");
    for (const NavTile& t : data.tiles) {
        if (!t.layers.empty() && dtStatusFailed(tc->buildNavMeshTilesAt(t.x, t.z, mesh.get()))) return fail("ナビメッシュを読めません (層から区画を作れません)");
    }
    std::unique_ptr<dtNavMeshQuery, Deleter> query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(mesh.get(), kMaxNodes))) return fail("ナビメッシュを読めません (道を探す準備ができません)");
    impl_->mesh = std::move(mesh);
    impl_->query = std::move(query);
    impl_->tileCache = std::move(tc);
    impl_->Count();
    impl_->ApplyBlocking();
    return true;
}

bool NavMesh::HasObstacles() const { return impl_->tileCache != nullptr; }

uint32_t NavMesh::AddObstacle(const Obstacle& o) {
    if (!impl_->tileCache) return 0;
    const float r = impl_->agentRadius;
    dtObstacleRef ref = 0;
    dtStatus st;
    if (o.box) {
        const float c[3] = {o.center.x, o.center.y, o.center.z}, h[3] = {o.half.x + r, o.half.y, o.half.z + r};
        st = impl_->tileCache->addBoxObstacle(c, h, o.yawRadians, &ref);
    } else {
        const float p[3] = {o.center.x, o.center.y, o.center.z};
        st = impl_->tileCache->addObstacle(p, o.radius + r, o.height, &ref);
    }
    if (dtStatusFailed(st)) return 0;
    impl_->pending = true;
    return ref;
}

void NavMesh::RemoveObstacle(uint32_t id) {
    if (impl_->tileCache && id && dtStatusSucceed(impl_->tileCache->removeObstacle(id))) impl_->pending = true;
}

bool NavMesh::UpdateObstacles(float dt, int maxTiles) {
    if (!impl_->tileCache || !impl_->pending) return false;
    // (DetourTileCache の update は 1 回に区画を 1 つ作り直す)
    for (int i = 0; i < std::max(1, maxTiles); ++i) {
        bool upToDate = true;
        impl_->tileCache->update(i == 0 ? dt : 0.f, impl_->mesh.get(), &upToDate);
        if (upToDate) {
            impl_->pending = false;
            break;
        }
    }
    impl_->Count();
    impl_->ApplyBlocking();
    return true;
}

void NavMesh::SetClosedVolumes(std::vector<NavAreaVolume> closed) {
    impl_->closed = std::move(closed);
    impl_->ApplyBlocking();
}

void NavMesh::SetLinkEnabled(uint32_t id, bool enabled) {
    auto& d = impl_->disabledLinks;
    const auto it = std::find(d.begin(), d.end(), id);
    if (enabled && it != d.end()) d.erase(it);
    if (!enabled && it == d.end()) d.push_back(id);
    impl_->ApplyBlocking();
}

const std::vector<NavLinkInput>& NavMesh::Links() const { return impl_->links; }

bool NavMesh::Empty() const { return !impl_->mesh || impl_->polys == 0; }
size_t NavMesh::Tiles() const { return impl_->tiles; }
size_t NavMesh::Polygons() const { return impl_->polys; }
void* NavMesh::Native() { return impl_->mesh.get(); }

NavPath NavMesh::FindPath(Vec3 start, Vec3 end, const NavFilter& filter, Vec3 searchHalf) const {
    NavPath out;
    if (Empty()) return out;
    const dtQueryFilter f = impl_->Filter(filter);
    float half[3], sp[3], ep[3], sNear[3], eNear[3];
    Set(half, searchHalf), Set(sp, start), Set(ep, end);
    dtPolyRef sRef = 0, eRef = 0;
    impl_->query->findNearestPoly(sp, half, &f, &sRef, sNear);
    if (!sRef) {
        out.status = NavPath::Status::StartOff;
        return out;
    }
    impl_->query->findNearestPoly(ep, half, &f, &eRef, eNear);
    if (!eRef) {
        out.status = NavPath::Status::EndOff;
        return out;
    }
    std::vector<dtPolyRef> path(kMaxPath);
    int n = 0;
    const dtStatus st = impl_->query->findPath(sRef, eRef, sNear, eNear, &f, path.data(), &n, kMaxPath);
    if (dtStatusFailed(st) || n <= 0) {
        out.status = NavPath::Status::StartOff;
        return out;
    }
    float target[3];
    dtVcopy(target, eNear);
    const bool partial = (st & DT_PARTIAL_RESULT) != 0 || path[static_cast<size_t>(n - 1)] != eRef;
    if (partial) {
        bool over = false;
        impl_->query->closestPointOnPoly(path[static_cast<size_t>(n - 1)], eNear, target, &over);
    }
    std::vector<float> corners(static_cast<size_t>(kMaxCorners) * 3);
    int nc = 0;
    impl_->query->findStraightPath(sNear, target, path.data(), n, corners.data(), nullptr, nullptr, &nc, kMaxCorners);
    out.status = partial ? NavPath::Status::Partial : NavPath::Status::Found;
    for (int i = 0; i < nc; ++i) out.points.push_back(Get(&corners[static_cast<size_t>(i) * 3]));
    if (out.points.empty()) out.points = {Get(sNear), Get(target)};
    for (size_t i = 1; i < out.points.size(); ++i) out.length += Dist(out.points[i - 1], out.points[i]);
    return out;
}

std::optional<Vec3> NavMesh::ClosestPoint(Vec3 p, Vec3 searchHalf, const NavFilter& filter) const {
    if (Empty()) return std::nullopt;
    const dtQueryFilter f = impl_->Filter(filter);
    float half[3], c[3], nearest[3];
    Set(half, searchHalf), Set(c, p);
    dtPolyRef ref = 0;
    impl_->query->findNearestPoly(c, half, &f, &ref, nearest);
    if (!ref) return std::nullopt;
    return Get(nearest);
}

bool NavMesh::Raycast(Vec3 a, Vec3 b, const NavFilter& filter, float* hit) const {
    if (hit) *hit = 0;
    if (Empty()) return false;
    const dtQueryFilter f = impl_->Filter(filter);
    float half[3] = {0.5f, 2.f, 0.5f}, sp[3], ep[3], nearest[3];
    Set(sp, a), Set(ep, b);
    dtPolyRef ref = 0;
    impl_->query->findNearestPoly(sp, half, &f, &ref, nearest);
    if (!ref) return false;
    float t = 0, normal[3];
    dtPolyRef visited[64];
    int nv = 0;
    if (dtStatusFailed(impl_->query->raycast(ref, nearest, ep, &f, &t, normal, visited, &nv, 64))) return false;
    if (t == FLT_MAX) return true;
    if (hit) *hit = std::clamp(t, 0.f, 1.f);
    return false;
}

std::optional<Vec3> NavMesh::RandomPoint(Vec3 center, float radius, uint32_t seed, const NavFilter& filter) const {
    if (Empty() || !(radius > 0)) return std::nullopt;
    const dtQueryFilter f = impl_->Filter(filter);
    float half[3] = {radius, 4.f, radius}, c[3], nearest[3], pt[3];
    Set(c, center);
    dtPolyRef ref = 0;
    impl_->query->findNearestPoly(c, half, &f, &ref, nearest);
    if (!ref) return std::nullopt;
    tRandom.seed(seed);
    dtPolyRef got = 0;
    if (dtStatusFailed(impl_->query->findRandomPointAroundCircle(ref, nearest, radius, &f, &Frand, &got, pt)) || !got) return std::nullopt;
    return Get(pt);
}

void NavMesh::SetBlockedAreas(const std::bitset<64>& blocked) {
    impl_->blocked = blocked;
    impl_->ApplyBlocking();
}

void NavMesh::Edges(std::vector<NavEdge>& out) const {
    if (!impl_->mesh) return;
    impl_->EachPoly([&](const dtMeshTile& t, int p) {
        const dtPoly& poly = t.polys[p];
        if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) return;
        for (int j = 0; j < poly.vertCount; ++j) {
            const unsigned short nei = poly.neis[j];
            bool border = nei == 0;
            if (nei & DT_EXT_LINK) {  // 区画の境目: 隣の区画へのつながりがあれば内
                border = true;
                for (unsigned int k = poly.firstLink; k != DT_NULL_LINK; k = t.links[k].next) {
                    if (t.links[k].edge == j) {
                        border = false;
                        break;
                    }
                }
            } else if (!border && static_cast<int>(nei) - 1 < p) {
                continue;  // (内の縁は 1 回だけ)
            }
            const int k = (j + 1) % poly.vertCount;
            NavEdge e;
            e.a = Get(&t.verts[poly.verts[j] * 3]);
            e.b = Get(&t.verts[poly.verts[k] * 3]);
            e.area = poly.getArea();
            e.border = border;
            out.push_back(e);
        }
    });
}

void NavMesh::PolygonsOf(std::vector<NavPolygon>& out) const {
    if (!impl_->mesh) return;
    impl_->EachPoly([&](const dtMeshTile& t, int p) {
        const dtPoly& poly = t.polys[p];
        if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) return;
        NavPolygon g;
        g.area = poly.getArea();
        for (int j = 0; j < poly.vertCount; ++j) g.points.push_back(Get(&t.verts[poly.verts[j] * 3]));
        out.push_back(std::move(g));
    });
}

}  // namespace rm::navigation
