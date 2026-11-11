#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"

#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/runtime/Atlas.h"

namespace rm::game_x_2d {

void RunSpriteAnimation(game_production::SystemContext& ctx) {
    const std::string sprite = reflect::TypeOf<Sprite>().name;
    game_production::ForEachComponent(ctx.scene, reflect::TypeOf<SpriteAnimation>().name,
                                      [&](game_production::SceneEntity& e, game_production::Component& c) {
                                          auto& anim = *static_cast<SpriteAnimation*>(c.value.Get());
                                          anim.time += ctx.dt;
                                          game_production::Component* s = e.FindComponent(sprite);
                                          if (!s || !s->Known()) return;
                                          auto& sp = *static_cast<Sprite*>(s->value.Get());
                                          sp.cell = space2d::AnimationCell(anim.frames, sp.columns * sp.rows, anim.framesPerSecond,
                                                                           anim.time, anim.loop);
                                      });
}

}  // namespace rm::game_x_2d
