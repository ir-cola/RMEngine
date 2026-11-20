#include "rm_engine/game_x_2d/runtime/SpriteAnimation.h"

#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/game_x_2d/runtime/sdk/RM2DAbi.h"
#include "rm_engine/space2d/runtime/Atlas.h"

#include <algorithm>
#include <limits>

namespace rm::game_x_2d {

void RunSpriteAnimation(game_production::SystemContext& ctx) {
    const std::string sprite = reflect::TypeOf<Sprite>().name;
    game_production::ForEachComponent(ctx.scene, reflect::TypeOf<SpriteAnimation>().name,
                                      [&](game_production::SceneEntity& e, game_production::Component& c) {
                                          auto& anim = *static_cast<SpriteAnimation*>(c.value.Get());
                                          if (!anim.paused && !anim.stopped) anim.time += ctx.dt;
                                          game_production::Component* s = e.FindComponent(sprite);
                                          if (!s || !s->Known()) return;
                                          auto& sp = *static_cast<Sprite*>(s->value.Get());
                                          sp.cell = space2d::AnimationCell(anim.frames, sp.columns * sp.rows, anim.framesPerSecond,
                                                                           anim.time, anim.loop, anim.startStep);
                                      });
}

namespace {

game_production::SceneEntity* EntityOf(void* ctx, uint64_t id) {
    auto& w = *static_cast<game_production::ScriptWorld*>(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return nullptr;
    return w.scene->Find(static_cast<int>(id));
}
SpriteAnimation* AnimOf(void* ctx, uint64_t id) {
    game_production::SceneEntity* e = EntityOf(ctx, id);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<SpriteAnimation>().name) : nullptr;
    return c && c->Known() ? static_cast<SpriteAnimation*>(c->value.Get()) : nullptr;
}
// スプライトのセルの数 (自分の コマの並び を持たないアニメーション用)。
int Cells(void* ctx, uint64_t id) {
    game_production::SceneEntity* e = EntityOf(ctx, id);
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<Sprite>().name) : nullptr;
    if (!c || !c->Known()) return 1;
    const auto& sp = *static_cast<const Sprite*>(c->value.Get());
    return std::max(1, sp.columns * sp.rows);
}
int StepOf(void* ctx, uint64_t id, const SpriteAnimation& a) {
    return space2d::AnimationStep(a.frames, Cells(ctx, id), a.framesPerSecond, a.time, a.loop, a.startStep);
}

int Has(void* ctx, uint64_t id) { return AnimOf(ctx, id) ? 1 : 0; }
void Play(void* ctx, uint64_t id, int fromStart) {
    if (SpriteAnimation* a = AnimOf(ctx, id)) {
        if (fromStart || a->stopped) {
            a->time = 0;
            a->startStep = 0;
        }
        a->paused = a->stopped = false;
    }
}
void Pause(void* ctx, uint64_t id, int paused) {
    if (SpriteAnimation* a = AnimOf(ctx, id)) a->paused = paused != 0;
}
void Stop(void* ctx, uint64_t id) {
    if (SpriteAnimation* a = AnimOf(ctx, id)) {
        a->time = 0;
        a->startStep = 0;
        a->stopped = true;
    }
}
float Speed(void* ctx, uint64_t id) {
    const SpriteAnimation* a = AnimOf(ctx, id);
    return a ? a->framesPerSecond : 0.f;
}
void SetSpeed(void* ctx, uint64_t id, float fps) {
    SpriteAnimation* a = AnimOf(ctx, id);
    if (!a) return;
    // (今いるステップはそのまま: 古い速さで再生した分は数えに入れる)
    a->startStep = StepOf(ctx, id, *a);
    a->time = 0;
    a->framesPerSecond = std::clamp(fps, 0.f, 240.f);
}
int Loop(void* ctx, uint64_t id) {
    const SpriteAnimation* a = AnimOf(ctx, id);
    return a && a->loop ? 1 : 0;
}
void SetLoop(void* ctx, uint64_t id, int loop) {
    if (SpriteAnimation* a = AnimOf(ctx, id)) a->loop = loop != 0;
}
void SetFrames(void* ctx, uint64_t id, const int32_t* frames, uint32_t count) {
    SpriteAnimation* a = AnimOf(ctx, id);
    if (!a) return;
    a->frames.assign(frames, frames + (frames ? count : 0));
    a->time = 0;
    a->startStep = 0;
}
int32_t Frame(void* ctx, uint64_t id) {
    const SpriteAnimation* a = AnimOf(ctx, id);
    return a ? StepOf(ctx, id, *a) : 0;
}
void SetFrame(void* ctx, uint64_t id, int32_t step) {
    if (SpriteAnimation* a = AnimOf(ctx, id)) {
        a->time = 0;
        a->startStep = std::max(0, step);
    }
}
int Playing(void* ctx, uint64_t id) {
    const SpriteAnimation* a = AnimOf(ctx, id);
    if (!a || a->paused || a->stopped || a->framesPerSecond <= 0) return 0;
    const int n = a->frames.empty() ? Cells(ctx, id) : static_cast<int>(a->frames.size());
    return a->loop || StepOf(ctx, id, *a) < n - 1 ? 1 : 0;
}

constexpr RmSpriteAnimationApi kTable{&Has, &Play, &Pause, &Stop, &Speed, &SetSpeed, &Loop, &SetLoop, &SetFrames, &Frame, &SetFrame, &Playing};

}  // namespace

const game_production::ScriptApiPart& ScriptSpriteAnimationPart() {
    static const game_production::ScriptApiPart part{RM_PART_SPRITE_ANIMATION, RM_PART_SPRITE_ANIMATION_VERSION, &kTable};
    return part;
}

}  // namespace rm::game_x_2d
