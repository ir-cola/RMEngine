#include "rm_engine/game_x_animation/runtime/AnimationSystem.h"

#include "base/Log.h"
#include "base/WorkQueue.h"
#include "rm_engine/animation3d/runtime/Thinning.h"
#include "platform/Shell.h"
#include "rm_engine/animation/runtime/Rules.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"
#include "rm_engine/game_x_animation/runtime/AnimatorIk.h"
#include "rm_engine/game_x_animation/runtime/AnimatorSprings.h"
#include "rm_engine/game_x_animation/runtime/GroundProbe.h"
#include "rm_engine/game_x_animation/runtime/RootMotionSink.h"
#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include "rm_engine/space3d/runtime/MeshFile.h"

#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace rm::game_x_animation {

namespace {

using game_production::Component;
using game_production::SceneEntity;

// 読んだモデルの骨とクリップ (アセット id ごと)。同じモデルのアニメーターは 1 つを分け合う。使っている
// アニメーター (AnimatorRun) が無くなれば (ゲームが終わった) 忘れるので、次の Play はファイルを読み直す。
std::unordered_map<std::string, game_x_3d::SkinnedModel>& Loaded() {
    static std::unordered_map<std::string, game_x_3d::SkinnedModel> loaded;
    return loaded;
}

// モデルを読む (エディター: そのファイルと取り込みの設定。ゲーム: 焼いたもの)。骨が無い・読めない: skin が null。
game_x_3d::SkinnedModel LoadSkinned(const game_production::AssetAccess* assets, const AssetRef& model, std::string* err) {
    auto& loaded = Loaded();
    std::erase_if(loaded, [](const auto& kv) { return kv.second.skin.use_count() <= 1; });
    if (const auto it = loaded.find(model.guid); it != loaded.end()) return it->second;
    std::optional<space3d::MeshData> mesh;
    if (assets && assets->file) {
        if (const auto file = assets->file(model); !file.empty()) mesh = space3d::LoadMeshFile(file, err);
    }
    if (!mesh && assets && assets->bytes) {
        if (const auto bytes = assets->bytes(model)) mesh = space3d::ReadBakedMesh(*bytes, err);
        // クリップは別のファイル (.rmclip)。読めない物はログに出して、ほかのクリップで動かす。
        if (mesh) {
            for (const std::string& problem : space3d::LoadClipFiles(*mesh, assets->path)) log::Warn("モデル {} の{}", model.guid, problem);
        }
    }
    if (!mesh) return {};
    game_x_3d::SkinnedModel skinned = game_x_3d::TakeSkinned(*mesh);
    if (skinned.skin) loaded[model.guid] = skinned;
    return skinned;
}

// 状態とルールのファイルを読んで解く (ゲーム: 書き出した .rmpak のもの / エディター: そのファイル)。読めない: null。
std::shared_ptr<const animation::CompiledController> LoadRules(const game_production::AssetAccess* assets, const AssetRef& ref, std::string* err) {
    std::optional<std::string> bytes;
    if (assets && assets->bytes) bytes = assets->bytes(ref);
    if (!bytes && assets && assets->file) {
        if (const auto file = assets->file(ref); !file.empty()) bytes = platform::ReadFileBytes(file, err);
    }
    if (!bytes) {
        if (err && err->empty()) *err = "ファイルが見つかりません";
        return nullptr;
    }
    const auto controller = animation::ReadController(*bytes, err);
    if (!controller) return nullptr;
    return std::make_shared<const animation::CompiledController>(animation::Compile(*controller));
}

const game_x_3d::MeshRenderer* MeshOn(const SceneEntity& e) {
    const Component* c = e.FindComponent(reflect::TypeOf<game_x_3d::MeshRenderer>().name);
    return c && c->Known() ? static_cast<const game_x_3d::MeshRenderer*>(c->value.Get()) : nullptr;
}

// ルートモーション (docs/ANIMATION.md 7-1): 根が動いた分をワールドにし、「動かす」なら行き先 (物理のキャラクター:
// 口 root_motion) に渡すか、Transform の位置に足す。回転はいつも Transform に書く。
void ApplyRootMotion(game_production::SystemContext& ctx, SceneEntity& e, const Animator& a, AnimatorRun& run) {
    run.rootWorld = space3d::TransformDirection(game_x_3d::WorldMatrixOf(ctx.scene, e.data.id), run.root.move);
    run.rootDt = ctx.dt;
    if (a.rootMotion != RootMotionUse::Move) {
        run.rootMoved = false;
        return;
    }
    const bool active = run.rootActive;
    if (!active && !run.rootMoved) return;
    // 行き先に尋ねる (止まったフレームは 0 で 1 度だけ)。
    bool taken = false;
    if (ctx.registry) {
        for (const auto& s : ctx.registry->Contributions<RootMotionSinkInfo>(kRootMotionPoint)) {
            if (s.item->take && s.item->take(ctx.scene, e.data.id, active ? run.rootWorld : Vec3{0, 0, 0}, ctx.dt)) {
                taken = true;
                break;
            }
        }
    }
    run.rootMoved = active;
    if (!active) return;
    Component* tc = e.FindComponent(reflect::TypeOf<space3d::Transform>().name);
    if (!tc || !tc->Known()) return;
    auto& t = *static_cast<space3d::Transform*>(tc->value.Get());
    t = RootMotionTransform(t, game_x_3d::WorldMatrixOf(ctx.scene, e.data.id, false), run.rootWorld, run.root.turn, !taken);
    run.rootTravel += std::hypot(run.rootWorld.x, run.rootWorld.z);
    if (!run.toldRoot && run.rootTravel > 0.1f) {
        // (書き出したゲームの検査 tools/check_export.ps1 の game_anim がこの行を探す)
        log::Info("アニメーター: 「{}」をルートモーションで {:.2f} m 動かしました{}", e.data.name, run.rootTravel, taken ? " (キャラクターの速さで)" : "");
        run.toldRoot = true;
    }
}

}  // namespace

bool DrivenByRootMotion(const game_production::Scene& scene, int entity) {
    const SceneEntity* e = scene.Find(entity);
    const Component* c = e ? e->FindComponent(reflect::TypeOf<Animator>().name) : nullptr;
    if (!c || !c->Known()) return false;
    const Animator& a = *static_cast<const Animator*>(c->value.Get());
    if (a.rootMotion != RootMotionUse::Move || !a.run) return false;
    return a.run->rootActive || a.run->player.RootBoneOf(a.run->player.ClipIndex()) >= 0;
}

space3d::Transform RootMotionTransform(const space3d::Transform& current, const space3d::Mat4& parentWorld, Vec3 worldMove, float turnRadians,
                                       bool writePosition) {
    space3d::WorldPlacement place = space3d::Decompose(space3d::WorldMatrix(parentWorld, current), current.rotation);
    if (writePosition && std::isfinite(worldMove.x) && std::isfinite(worldMove.y) && std::isfinite(worldMove.z)) {
        place.position = {place.position.x + worldMove.x, place.position.y + worldMove.y, place.position.z + worldMove.z};
    }
    if (turnRadians != 0 && std::isfinite(turnRadians)) {
        // 自分の Y のまわりに (クリップの根が回った向きと同じ: animation3d::YawQuat)。
        const space3d::Mat4 r = space3d::RotationMatrix(place.rotation) * space3d::QuatMatrix(animation3d::YawQuat(turnRadians));
        place.rotation = space3d::EulerFromRotation(r, place.rotation);
    }
    return space3d::FromWorld(current, parentWorld, place);
}

void RunAnimation(game_production::SystemContext& ctx) {
    const std::string type = reflect::TypeOf<Animator>().name;
    // 3 つに分ける (docs/ANIMATION.md 7-3): 1. 前 (主のスレッド): モデルとルールを読む・ルール・IK と揺れの支度・間引き
    // 2. 並べて (作業の列): 姿勢 (StepAnimator) 3. 後 (主のスレッド・エンティティの順): ルートモーション・イベント。
    struct Job {
        SceneEntity* e = nullptr;
        Animator* a = nullptr;
        AnimatorRun* run = nullptr;
        PoseHook post;
        std::vector<const space3d::ClipEvent*> fired;
    };
    std::vector<Job> jobs;
    game_production::ForEachComponent(ctx.scene, type, [&](SceneEntity& e, Component& c) {
        if (!c.Known()) return;
        Animator& a = *static_cast<Animator*>(c.value.Get());
        AnimatorRun& run = RunOf(a, e.data.id);
        // モデル: 同じエンティティの「メッシュ (3D モデル)」のもの (替えたら読み直す)。
        const game_x_3d::MeshRenderer* mr = MeshOn(e);
        const AssetRef model = mr && mr->shape == game_x_3d::MeshShape::Model ? mr->model : AssetRef{};
        if (model.guid != run.model) {
            run.model = model.guid;
            run.failed = false;
            BindModel(run, nullptr, nullptr);
        }
        if (!run.skin && !run.failed && !model.Empty()) {
            std::string err;
            game_x_3d::SkinnedModel m = LoadSkinned(ctx.assets, model, &err);
            if (!m.skin) {
                run.failed = true;
                log::Warn("アニメーター ({}): モデルに骨がないか、読めません{}", e.data.name, err.empty() ? std::string() : " (" + err + ")");
                return;
            }
            BindModel(run, m.skin, m.clips);
        }
        if (!run.skin) return;
        if (!a.clip.empty() && FindClip(*run.clips, a.clip) < 0 && run.missing != a.clip) {
            log::Warn("アニメーター ({}): モデルにクリップ「{}」がないので、バインドの形で描きます", e.data.name, a.clip);
            run.missing = a.clip;
        }
        // 状態とルール: 替えたら読み直す。行の問題 (式の間違い・無い状態) は 1 度だけ知らせる。
        if (a.rules.guid != run.rulesId) {
            run.rulesId = a.rules.guid;
            run.rulesFailed = false;
            BindRules(run, a, nullptr);
            if (!a.rules.Empty()) {
                std::string err;
                auto rules = LoadRules(ctx.assets, a.rules, &err);
                if (!rules) {
                    run.rulesFailed = true;
                    log::Warn("アニメーター ({}): 状態とルールを読めません ({})", e.data.name, err);
                } else {
                    for (size_t i = 0; i < rules->rules.size(); ++i) {
                        if (!rules->rules[i].problem.empty()) log::Warn("アニメーター ({}): ルール {}: {}", e.data.name, i + 1, rules->rules[i].problem);
                    }
                    if (rules->states.empty()) log::Warn("アニメーター ({}): 状態とルールに状態がありません", e.data.name);
                    BindRules(run, a, std::move(rules));
                }
            }
        }
        StepAnimatorRules(run, a, ctx.dt);
        // 遠い・見えない物 (段階 7): 画面が前のフレームに知らせた大きさで、このフレームに姿勢を作るか決める。
        if (run.sightReports > 0) run.screen = run.sightMax;
        run.sightMax = 0;
        run.sightReports = 0;
        int interval = run.everSeen ? animation3d::PoseInterval(run.screen, a.thinOut) : 1;
        if (interval == 0 && a.whenUnseen == WhenUnseen::Everything) interval = 1;
        run.skipPose = !animation3d::PoseThisFrame(interval, ctx.frame, static_cast<unsigned>(e.data.id));
        // IK (部品「IK」が同じエンティティにあれば) → 揺れる骨 (部品「揺れる骨」): 体とレイヤーの後、行列にする前。
        PoseHook post;
        const AnimatorIk* ikOn = nullptr;
        const SpringBones* springsOn = nullptr;
        if (const Component* sc = e.FindComponent(reflect::TypeOf<SpringBones>().name); sc && sc->Known()) {
            springsOn = static_cast<const SpringBones*>(sc->value.Get());
        }
        if (const Component* ikc = e.FindComponent(reflect::TypeOf<AnimatorIk>().name); ikc && ikc->Known()) {
            const AnimatorIk& ik = *static_cast<const AnimatorIk*>(ikc->value.Get());
            if (!ik.chains.empty()) ikOn = &ik;
        }
        if (ikOn || springsOn) {
            {
                IkWorld world;
                world.entityWorld = game_x_3d::WorldMatrixOf(ctx.scene, e.data.id);
                world.dt = ctx.dt;
                const int self = e.data.id;
                if (ctx.registry) {
                    for (const auto& g : ctx.registry->Contributions<GroundProbeInfo>(kGroundPoint)) {
                        if (!g.item->cast) continue;
                        auto cast = g.item->cast;
                        world.ground = [&ctx, cast, self](Vec3 from, Vec3 dir, float max) { return cast(ctx.scene, from, dir, max, self); };
                        break;
                    }
                }
                world.entity = [&ctx](const EntityRef& target, Vec3 offset) -> std::optional<Vec3> {
                    if (target.id == 0 || target.id > 0x7fffffffu || !ctx.scene.Find(static_cast<int>(target.id))) return std::nullopt;
                    return space3d::TransformPoint(game_x_3d::WorldMatrixOf(ctx.scene, static_cast<int>(target.id)), offset);
                };
                // (作業の列のスレッドで呼ばれる: 読むのはシーン (書く物は無い) と、このアニメーターの run だけ)
                post = [runp = &run, ikOn, springsOn, world, dt = ctx.dt](animation3d::Pose& pose) mutable {
                    bool changed = ikOn && ApplyAnimatorIk(*runp, *ikOn, world, pose);
                    if (springsOn) changed = ApplyAnimatorSprings(*runp, *springsOn, world.entityWorld, dt, pose) || changed;
                    return changed;
                };
            }
        }
        jobs.push_back({&e, &a, &run, std::move(post), {}});
    });
    // 2. 姿勢: アニメーターどうしは関わらないので並べる (同じ結果になる: tests/TestGameAnimation.cpp)。
    const float dt = ctx.dt;
    SharedWorkQueue().ParallelFor(jobs.size(), [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            Job& j = jobs[i];
            StepAnimator(*j.run, *j.a, dt, j.fired, &space3d::NextPoseVersion, &j.post);
        }
    });
    // 3. 後: エンティティの順に。
    for (Job& j : jobs) {
        SceneEntity& e = *j.e;
        const Animator& a = *j.a;
        AnimatorRun& run = *j.run;
        ApplyRootMotion(ctx, e, a, run);  // (イベントの OnEvent はこの後: 足音は動いた後の位置で鳴る)
        if (ctx.events) {
            for (const space3d::ClipEvent* ev : j.fired) ctx.events->push_back({e.data.id, ev->name, ev->number, ev->text, e.data.id});
        }
        if (!run.toldPlaying && run.moving && !run.paused && run.player.ClipIndex() >= 0 &&
            run.player.Time() != StartTime(run.player.speed, Duration(run), run.player.loop)) {
            // (書き出したゲームの検査 tools/check_export.ps1 の game_anim がこの行を探す)
            log::Info("アニメーター: 「{}」がクリップ「{}」で骨 {} 本を動かしました ({:.2f} 秒)", e.data.name,
                      (*run.clips)[static_cast<size_t>(run.player.ClipIndex())].name, run.bones.size(), run.player.Time());
            run.toldPlaying = true;
        }
    }
}

space3d::Transform AttachedTransform(const space3d::Transform& current, const space3d::Mat4& parentWorld, const space3d::Mat4& boneWorld,
                                     const BoneAttach& attach) {
    const space3d::Mat4 want = boneWorld * space3d::LocalMatrix({attach.position, attach.rotation, {1, 1, 1}});
    const space3d::WorldPlacement now = space3d::Decompose(space3d::WorldMatrix(parentWorld, current), current.rotation);
    space3d::WorldPlacement place = space3d::Decompose(want, now.rotation);
    place.scale = now.scale;  // (大きさは変えない: 骨の大きさを剣に移さない)
    if (!attach.followRotation) place.rotation = now.rotation;
    return space3d::FromWorld(current, parentWorld, place);
}

void RunBoneAttach(game_production::SystemContext& ctx) {
    const std::string type = reflect::TypeOf<BoneAttach>().name;
    const std::string animator = reflect::TypeOf<Animator>().name;
    const std::string transform = reflect::TypeOf<space3d::Transform>().name;
    game_production::ForEachComponent(ctx.scene, type, [&](SceneEntity& e, Component& c) {
        if (!c.Known()) return;
        BoneAttach& b = *static_cast<BoneAttach*>(c.value.Get());
        auto tell = [&](std::string why) {
            if (b.told == why) return;
            log::Warn("骨に付ける ({}): {}", e.data.name, why);
            b.told = std::move(why);
        };
        if (b.bone.empty()) return;
        // アニメーターを持つ一番近い祖先。
        const SceneEntity* owner = nullptr;
        const Animator* a = nullptr;
        size_t steps = 0;
        for (const SceneEntity* p = ctx.scene.Find(e.data.parent); p && steps++ <= ctx.scene.Entities().size(); p = ctx.scene.Find(p->data.parent)) {
            if (const Component* ac = p->FindComponent(animator); ac && ac->Known()) {
                owner = p;
                a = static_cast<const Animator*>(ac->value.Get());
                break;
            }
        }
        if (!owner) {
            tell("親 (かその上) にアニメーターがありません");
            return;
        }
        if (!a->run || !a->run->skin) return;  // (まだ読んでいない・読めない: アニメーターが知らせる)
        const space3d::Mat4* bone = BoneModelMatrix(*a->run, b.bone);
        if (!bone) {
            if (a->run->skin->skeleton.Find(b.bone) < 0) tell(std::format("「{}」のモデルに骨「{}」がありません", owner->data.name, b.bone));
            return;
        }
        Component* tc = e.FindComponent(transform);
        if (!tc || !tc->Known()) {
            tell("Transform がありません");
            return;
        }
        auto& t = *static_cast<space3d::Transform*>(tc->value.Get());
        const space3d::Mat4 boneWorld = game_x_3d::WorldMatrixOf(ctx.scene, owner->data.id) * *bone;
        t = AttachedTransform(t, game_x_3d::WorldMatrixOf(ctx.scene, e.data.id, false), boneWorld, b);
        b.told.clear();
    });
}

}  // namespace rm::game_x_animation
