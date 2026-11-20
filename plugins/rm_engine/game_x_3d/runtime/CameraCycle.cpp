#include "rm_engine/game_x_3d/runtime/CameraCycle.h"

#include "rm_engine/game_x_3d/runtime/Camera.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rm::game_x_3d {

void RunCameraCycle(game_production::SystemContext& ctx) {
    float seconds = 0;
    game_production::ForEachComponent(ctx.scene, reflect::TypeOf<CameraCycle>().name, [&](game_production::SceneEntity&, game_production::Component& c) {
        if (seconds <= 0) seconds = std::max(static_cast<const CameraCycle*>(c.value.Get())->seconds, 0.1f);
    });
    if (seconds <= 0) return;
    // この実行で、時計が間隔の倍数を通ったか。
    if (std::floor((ctx.time + ctx.dt) / seconds) <= std::floor(ctx.time / seconds)) return;
    std::vector<uint64_t> cameras;
    game_production::ForEachComponent(ctx.scene, reflect::TypeOf<Camera>().name, [&](game_production::SceneEntity& e, game_production::Component&) {
        const uint64_t id = static_cast<uint64_t>(e.data.id);
        if (cameras.empty() || cameras.back() != id) cameras.push_back(id);
    });
    if (cameras.empty()) return;
    const auto now = std::find(cameras.begin(), cameras.end(), ctx.camera);
    ctx.camera = now == cameras.end() || now + 1 == cameras.end() ? cameras.front() : *(now + 1);
}

}  // namespace rm::game_x_3d
