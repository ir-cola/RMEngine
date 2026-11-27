#include "rm_engine/navigation/runtime/Query.h"

#include "rm_engine/navigation/runtime/DetourShared.h"

#pragma warning(push, 0)
#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
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
};

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
    std::bitset<64> blocked;
    size_t tiles = 0, polys = 0;

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
    if (data.tiles.empty()) return true;
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
    impl_->tiles = data.tiles.size();
    impl_->polys = polys;
    return true;
}

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
    if (!impl_->mesh) return;
    dtNavMesh* m = impl_->mesh.get();
    impl_->EachPoly([&](const dtMeshTile& t, int p) {
        const dtPoly& poly = t.polys[p];
        if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) return;
        const unsigned short flags = static_cast<unsigned short>(kWalk | (blocked[poly.getArea()] ? kBlocked : 0));
        m->setPolyFlags(m->getPolyRefBase(&t) | static_cast<dtPolyRef>(p), flags);
    });
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
