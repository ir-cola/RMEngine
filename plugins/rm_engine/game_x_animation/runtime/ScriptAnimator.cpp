// スクリプト API のアニメーション部分 (RMAnim.h / RMAnimAbi.h): 再生しているシーンのアニメーター (その欄と、
// ゲームの間の再生 Animator::run) を直に動かす。進めるのはシステム「アニメーション」(AnimationSystem.cpp)。
// RmApi::ctx はゲーム制作の ScriptWorld。

#include "rm_engine/game_x_animation/runtime/ScriptAnimator.h"

#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/game_x_animation/runtime/AnimatorIk.h"
#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"
#include "rm_engine/game_x_animation/runtime/sdk/RMAnimAbi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace rm::game_x_animation {

namespace {

struct Found {
    Animator* a = nullptr;
    int id = 0;
    game_production::Scene* scene = nullptr;
};

Found AnimatorOf(void* ctx, uint64_t id) {
    auto& w = *static_cast<game_production::ScriptWorld*>(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return {};
    game_production::SceneEntity* e = w.scene->Find(static_cast<int>(id));
    game_production::Component* c = e ? e->FindComponent(reflect::TypeOf<Animator>().name) : nullptr;
    if (!c || !c->Known()) return {};
    return {static_cast<Animator*>(c->value.Get()), static_cast<int>(id), w.scene};
}

int Has(void* ctx, uint64_t id) { return AnimatorOf(ctx, id).a ? 1 : 0; }
int Play(void* ctx, uint64_t id, const char* clip, float fade) {
    const Found f = AnimatorOf(ctx, id);
    if (!f.a) return 0;
    return PlayClip(RunOf(*f.a, f.id), *f.a, clip ? clip : "", fade) ? 1 : 0;
}
int HasClip(void* ctx, uint64_t id, const char* clip) {
    const Found f = AnimatorOf(ctx, id);
    if (!f.a || !f.a->run || !f.a->run->clips) return 0;
    return FindClip(*f.a->run->clips, clip ? clip : "") >= 0 ? 1 : 0;
}
uint32_t ClipName(void* ctx, uint64_t id, char* buf, uint32_t cap) {
    const Found f = AnimatorOf(ctx, id);
    if (!f.a) return 0;
    const std::string& s = f.a->clip;
    if (buf && cap > 0) std::memcpy(buf, s.data(), std::min<size_t>(s.size(), cap));
    return static_cast<uint32_t>(s.size());
}
void SetPaused(void* ctx, uint64_t id, int paused) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a) RunOf(*f.a, f.id).paused = paused != 0;
}
int Paused(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run && f.a->run->paused ? 1 : 0;
}
float Speed(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a ? f.a->speed : 0.f;
}
void SetSpeed(void* ctx, uint64_t id, float speed) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a && std::isfinite(speed)) f.a->speed = std::clamp(speed, -10.f, 10.f);
}
int Loop(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->loop ? 1 : 0;
}
void SetLoop(void* ctx, uint64_t id, int loop) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a) f.a->loop = loop != 0;
}
float Time(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run ? f.a->run->player.Time() : 0.f;
}
void SetTime(void* ctx, uint64_t id, float seconds) {
    const Found f = AnimatorOf(ctx, id);
    if (!f.a || !f.a->run || !f.a->run->started) return;
    f.a->run->player.loop = f.a->loop;
    f.a->run->player.SetTime(seconds);
    f.a->run->dirty = true;
}
float ClipDuration(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run ? Duration(*f.a->run) : 0.f;
}
int ClipEnded(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run && Ended(*f.a->run, *f.a) ? 1 : 0;
}
int Bone(void* ctx, uint64_t id, const char* name, float* position, float* rotation) {
    auto& w = *static_cast<game_production::ScriptWorld*>(ctx);
    if (!w.scene || id == 0 || id > static_cast<uint64_t>(std::numeric_limits<int>::max())) return 0;
    const Found f = AnimatorOf(ctx, id);
    const space3d::Mat4* bone = f.a && f.a->run ? BoneModelMatrix(*f.a->run, name ? name : "") : nullptr;
    space3d::Mat4 world = game_x_3d::WorldMatrixOf(*w.scene, static_cast<int>(id));
    if (bone) world = world * *bone;
    const space3d::BoneTRS trs = space3d::DecomposeTRS(world);
    if (position) {
        position[0] = trs.t.x;
        position[1] = trs.t.y;
        position[2] = trs.t.z;
    }
    if (rotation) {
        rotation[0] = trs.r.x;
        rotation[1] = trs.r.y;
        rotation[2] = trs.r.z;
        rotation[3] = trs.r.w;
    }
    return bone ? 1 : 0;
}

// 状態とルール。
int SetParam(void* ctx, uint64_t id, const char* name, float value, int trigger) {
    const Found f = AnimatorOf(ctx, id);
    if (!f.a) return 0;
    return SetRuleParam(RunOf(*f.a, f.id), name ? name : "", value, trigger != 0) ? 1 : 0;
}
float Param(void* ctx, uint64_t id, const char* name) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run ? RuleParam(*f.a->run, name ? name : "") : 0.f;
}
uint32_t State(void* ctx, uint64_t id, char* buf, uint32_t cap) {
    const Found f = AnimatorOf(ctx, id);
    if (!f.a || !f.a->run) return 0;
    const std::string_view s = StateName(*f.a->run);
    if (buf && cap > 0) std::memcpy(buf, s.data(), std::min<size_t>(s.size(), cap));
    return static_cast<uint32_t>(s.size());
}
float StateTimeOf(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run ? StateFraction(*f.a->run) : 0.f;
}
int PlayStateOf(void* ctx, uint64_t id, const char* state, float fade) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && PlayState(RunOf(*f.a, f.id), *f.a, state ? state : "", fade) ? 1 : 0;
}
constexpr RmAnimRulesApi kRules{&SetParam, &Param, &State, &StateTimeOf, &PlayStateOf};

// ルートモーションとレイヤーの重み。
void SetRootUse(void* ctx, uint64_t id, int use) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a && use >= 0 && use <= static_cast<int>(RootMotionUse::Script)) f.a->rootMotion = static_cast<RootMotionUse>(use);
}
int RootUse(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a ? static_cast<int>(f.a->rootMotion) : 0;
}
int RootDeltaOf(void* ctx, uint64_t id, float* move) {
    const Found f = AnimatorOf(ctx, id);
    const Vec3 d = f.a && f.a->run ? f.a->run->rootWorld : Vec3{0, 0, 0};
    if (move) {
        move[0] = d.x;
        move[1] = d.y;
        move[2] = d.z;
    }
    return f.a && f.a->run && f.a->run->rootActive ? 1 : 0;
}
float RootTurnOf(void* ctx, uint64_t id) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run ? f.a->run->root.turn * 57.29577951308232f : 0.f;
}
void SetLayerWeightOf(void* ctx, uint64_t id, const char* layer, float weight) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a) SetLayerWeight(RunOf(*f.a, f.id), layer ? layer : "", weight);
}
float LayerWeightOfApi(void* ctx, uint64_t id, const char* layer) {
    const Found f = AnimatorOf(ctx, id);
    return f.a && f.a->run ? LayerWeightOf(*f.a->run, layer ? layer : "") : 0.f;
}
constexpr RmAnimRootApi kRoot{&SetRootUse, &RootUse, &RootDeltaOf, &RootTurnOf, &SetLayerWeightOf, &LayerWeightOfApi};

// IK。
Vec3 Point(const float* p) { return p ? Vec3{p[0], p[1], p[2]} : Vec3{0, 0, 0}; }
void SetIkTargetOf(void* ctx, uint64_t id, const char* chain, const float* position, float weight) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a) SetIkTarget(RunOf(*f.a, f.id), chain ? chain : "", Point(position), weight);
}
void SetIkWeightOf(void* ctx, uint64_t id, const char* chain, float weight) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a) SetIkWeight(RunOf(*f.a, f.id), chain ? chain : "", weight);
}
void LookAtOf(void* ctx, uint64_t id, const float* position, float weight) {
    const Found f = AnimatorOf(ctx, id);
    if (f.a) SetLookAt(RunOf(*f.a, f.id), Point(position), weight);
}
constexpr RmAnimIkApi kIk{&SetIkTargetOf, &SetIkWeightOf, &LookAtOf};

constexpr RmAnimApi kTable{&Has,   &Play, &HasClip, &ClipName, &SetPaused, &Paused, &Speed,      &SetSpeed,
                           &Loop,  &SetLoop, &Time, &SetTime,  &ClipDuration, &ClipEnded, &Bone};

}  // namespace

const game_production::ScriptApiPart& ScriptAnimatorPart() {
    static const game_production::ScriptApiPart part{RM_PART_ANIM, RM_PART_ANIM_VERSION, &kTable};
    return part;
}

const game_production::ScriptApiPart& ScriptAnimatorRulesPart() {
    static const game_production::ScriptApiPart part{RM_PART_ANIM_RULES, RM_PART_ANIM_RULES_VERSION, &kRules};
    return part;
}

const game_production::ScriptApiPart& ScriptAnimatorRootPart() {
    static const game_production::ScriptApiPart part{RM_PART_ANIM_ROOT, RM_PART_ANIM_ROOT_VERSION, &kRoot};
    return part;
}

const game_production::ScriptApiPart& ScriptAnimatorIkPart() {
    static const game_production::ScriptApiPart part{RM_PART_ANIM_IK, RM_PART_ANIM_IK_VERSION, &kIk};
    return part;
}

}  // namespace rm::game_x_animation
