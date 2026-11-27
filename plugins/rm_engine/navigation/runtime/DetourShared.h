#pragma once
// ナビゲーション - このプラグインの .cpp だけが読む (Recast / Detour の型が出る: ほかのプラグインは読まない)。
// 多角形の印と、費用 (NavFilter) を Detour の dtQueryFilter にすることを 1 か所に (憲法 2: Bake・Query・Crowd が同じ物を使う)。

#include "rm_engine/navigation/runtime/Query.h"

#pragma warning(push, 0)
#include "DetourNavMeshQuery.h"
#pragma warning(pop)

#include <algorithm>
#include <cmath>

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

}  // namespace rm::navigation::detail
