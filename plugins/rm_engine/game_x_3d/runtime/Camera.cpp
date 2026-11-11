#include "rm_engine/game_x_3d/runtime/Camera.h"

#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <utility>

namespace rm::game_x_3d {

namespace {

// Near and far as they can be drawn: near > 0, far beyond near.
std::pair<float, float> Clip(const Camera& c) {
    const float nearZ = std::max(c.nearClip, 0.0001f);
    return {nearZ, std::max(c.farClip, nearZ * 1.001f + 0.001f)};
}

}  // namespace

space3d::Mat4 ProjectionOf(const Camera& camera, float aspect) {
    const float a = std::max(aspect, 1e-4f);
    const auto [nearZ, farZ] = Clip(camera);
    if (camera.projection == CameraProjection::Orthographic) {
        const float h = std::max(camera.orthographicSize, 1e-4f);
        return space3d::Orthographic(-h * a, h * a, -h, h, nearZ, farZ);
    }
    return space3d::Perspective(std::clamp(camera.fieldOfView, 1.f, 179.f), a, nearZ, farZ);
}

Camera ShownNear(const Camera& camera, float farthest) {
    Camera c = camera;
    const auto [nearZ, farZ] = Clip(camera);
    c.nearClip = nearZ;
    c.farClip = std::max(std::min(farZ, farthest), nearZ * 1.001f + 0.001f);
    return c;
}

Color BackgroundOf(const Camera& camera, Color gameBackground) { return camera.ownBackground ? camera.background : gameBackground; }

}  // namespace rm::game_x_3d
