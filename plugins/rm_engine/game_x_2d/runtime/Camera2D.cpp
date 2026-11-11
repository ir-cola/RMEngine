#include "rm_engine/game_x_2d/runtime/Camera2D.h"

#include <algorithm>

namespace rm::game_x_2d {

space2d::Camera2D ViewOf(const Camera2D& camera, const space2d::Mat3& world, float viewHeight) {
    space2d::Camera2D view;
    view.center = space2d::TransformPoint(world, {0, 0});
    view.rotation = space2d::Decompose(world).rotation;
    view.zoom = std::clamp(viewHeight * 0.5f / std::max(camera.size, 1e-3f), space2d::kMinZoom, space2d::kMaxZoom);
    return view;
}

Vec2 HalfExtent(const Camera2D& camera, float aspect) {
    const float h = std::max(camera.size, 1e-3f);
    return {h * std::max(aspect, 1e-3f), h};
}

Color BackgroundOf(const Camera2D& camera, Color gameBackground) { return camera.ownBackground ? camera.background : gameBackground; }

}  // namespace rm::game_x_2d
