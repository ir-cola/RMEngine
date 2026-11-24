#include "rm_engine/weather/runtime/WeatherBlocks.h"

#include <algorithm>

namespace rm::weather {

void WeatherBlocksOf(const game_production::Scene& scene, std::span<const int> movers,
                     const std::function<const space3d::Mat4*(int entity)>& world,
                     const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out) {
    out.clear();
    if (!world || !modelOf) return;
    auto moves = [&](int id) {
        // そのものか、祖先のどれかが動く (キャラクターの子の見た目のモデル)。親の輪があっても止まる。
        for (int step = 0; id != 0 && step < 256; ++step) {
            if (std::find(movers.begin(), movers.end(), id) != movers.end()) return true;
            const game_production::SceneEntity* e = scene.Find(id);
            if (!e) break;
            id = e->data.parent;
        }
        return false;
    };
    for (const game_production::SceneEntity& e : scene.Entities()) {
        const space3d::Mat4* w = world(e.data.id);
        const space3d::GpuMesh* mesh = w ? modelOf(e.data.id) : nullptr;
        if (!mesh) continue;  // 見えない (メッシュが無い・切ってある): 雨も雪も通り抜ける
        out.push_back({mesh, *w, moves(e.data.id)});
    }
}

void MakeWeatherBlocks(const game_production::Scene& scene, const RuntimeRegistry& registry,
                       const std::function<const space3d::Mat4*(int entity)>& world,
                       const std::function<const space3d::GpuMesh*(int entity)>& modelOf, std::vector<space3d::MapShape>& out,
                       std::vector<int>& scratch) {
    scratch.clear();
    for (const auto& c : registry.Contributions<WeatherMoversInfo>(kWeatherMoversPoint)) {
        if (c.item->gather) c.item->gather(scene, scratch);
    }
    WeatherBlocksOf(scene, scratch, world, modelOf, out);
}

}  // namespace rm::weather
