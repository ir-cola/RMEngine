#include "rm_engine/game_x_3d/runtime/Light.h"

#include "rm_engine/space3d/runtime/View.h"

namespace rm::game_x_3d {

space3d::Ambient ToRenderAmbient(const AmbientLight& ambient) { return {ambient.sky, ambient.ground, ambient.intensity}; }

space3d::Light ToRenderLight(const Light& light, const space3d::Mat4& world) {
    space3d::Light l;
    l.type = light.kind == LightKind::Point ? space3d::LightType::Point
             : light.kind == LightKind::Spot ? space3d::LightType::Spot
                                             : space3d::LightType::Directional;
    l.position = space3d::TransformPoint(world, {0, 0, 0});
    l.direction = space3d::Normalize(space3d::TransformDirection(world, {0, 0, 1}));
    l.color = light.color;
    l.intensity = light.intensity;
    l.range = light.range;
    l.spotAngle = light.spotAngle;
    l.castShadow = light.castShadows;
    return l;
}

}  // namespace rm::game_x_3d
