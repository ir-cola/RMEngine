#pragma once
// ナビゲーション - このプラグインの .cpp だけが読む (Recast / Detour の型が出る: ほかのプラグインは読まない)。
// 多角形の印と、費用 (NavFilter) を Detour の dtQueryFilter にすることを 1 か所に (憲法 2: Bake・Query・Crowd が同じ物を使う)。

#include "rm_engine/navigation/runtime/Query.h"

#pragma warning(push, 0)
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourTileCacheBuilder.h"
#pragma warning(pop)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace rm::navigation::detail {

inline constexpr unsigned short kWalk = 1;     // 歩ける (焼いたとき)
inline constexpr unsigned short kBlocked = 2;  // 通れないエリア (NavMesh::SetBlockedAreas)

inline void ApplyFilter(const NavFilter& f, dtQueryFilter& q) {
    for (int i = 0; i < DT_MAX_AREAS && i < 64; ++i) {
        const float c = f.costs[static_cast<size_t>(i)];
        q.setAreaCost(i, std::isfinite(c) && c > 0 ? std::min(c, 1e6f) : 1.f);
    }
    q.setIncludeFlags(kWalk);
    q.setExcludeFlags(kBlocked);
}

// つなぎの線を Detour の並びに (dtNavMeshCreateParams の offMeshCon*。全部の区画に渡す: 始めが区画にある物だけが入る)。
struct LinkArrays {
    std::vector<float> verts, radius;
    std::vector<unsigned short> flags;
    std::vector<unsigned char> areas, dirs;
    std::vector<unsigned int> ids;

    void Build(const std::vector<NavLinkInput>& links) {
        for (const NavLinkInput& l : links) {
            if (!std::isfinite(l.a.x + l.a.y + l.a.z + l.b.x + l.b.y + l.b.z + l.radius)) continue;
            verts.insert(verts.end(), {l.a.x, l.a.y, l.a.z, l.b.x, l.b.y, l.b.z});
            radius.push_back(std::max(0.05f, l.radius));
            flags.push_back(kWalk);
            areas.push_back(l.area == kAreaNone ? kAreaNormal : std::min(l.area, kAreaNormal));
            dirs.push_back(l.both ? DT_OFFMESH_CON_BIDIR : 0);
            ids.push_back(l.id);
        }
    }
    void Attach(dtNavMeshCreateParams& p) const {
        p.offMeshConCount = static_cast<int>(ids.size());
        if (p.offMeshConCount == 0) return;
        p.offMeshConVerts = verts.data();
        p.offMeshConRad = radius.data();
        p.offMeshConFlags = flags.data();
        p.offMeshConAreas = areas.data();
        p.offMeshConDir = dirs.data();
        p.offMeshConUserID = ids.data();
    }
};

// 層を圧縮しない (そのまま写す) DetourTileCache の圧縮器 (焼くときと読むときで同じ物)。
struct RawCompressor final : dtTileCacheCompressor {
    int maxCompressedSize(const int size) override { return size; }
    dtStatus compress(const unsigned char* in, const int size, unsigned char* out, const int max, int* outSize) override {
        if (size > max) return DT_FAILURE | DT_BUFFER_TOO_SMALL;
        std::memcpy(out, in, static_cast<size_t>(size));
        *outSize = size;
        return DT_SUCCESS;
    }
    dtStatus decompress(const unsigned char* in, const int size, unsigned char* out, const int max, int* outSize) override {
        if (size > max) return DT_FAILURE | DT_BUFFER_TOO_SMALL;
        std::memcpy(out, in, static_cast<size_t>(size));
        *outSize = size;
        return DT_SUCCESS;
    }
};

}  // namespace rm::navigation::detail
