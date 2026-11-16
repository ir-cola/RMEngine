#pragma once
// パーティクル - an effect (.rmparticle, design docs/ANIMATION_PARTICLES.md 3): emitters, each a list of
// modules run in order on the GPU. A module is HLSL (.rmfx - ModuleSource.h): the built-in ones are
// written the same way (BuiltinModules.h), so there is one way of running them (rule 3).
//
//   エフェクト「炎」
//    └ エミッター「火」   how many, how often, how they are drawn
//        ├ 形: 円すい      ← a module: its `param`s are its fields (ParticleValue: the values set here)
//        ├ はじめの値
//        └ 色の変わり方
//
// Saved as JSON (reflect: rule 9). A module's values are kept by the param's name, so a module whose
// params change (a new version, an edited .rmfx) keeps what still fits; a value of a param it no longer
// has is kept as written (it comes back if the param does).

#include "base/Ref.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::particles {

RM_ENUM(Label="重ね方")
enum class ParticleBlend {
    RM_ITEM(Label="足して光らせる") Add,
    RM_ITEM(Label="普通に重ねる") Alpha,
};

RM_ENUM(Label="粒の形")
enum class ParticleShape {
    RM_ITEM(Label="ぼんやりした丸") SoftCircle,
    RM_ITEM(Label="くっきりした丸") Circle,
    RM_ITEM(Label="四角") Square,
};

// One param's value (a float uses x; float3 x y z; a colour x y z w = r g b a; a bool x != 0).
RM_STRUCT(Label="モジュールの値")
struct ParticleValue {
    RM_PROPERTY() std::string name;
    RM_PROPERTY() float x = 0;
    RM_PROPERTY() float y = 0;
    RM_PROPERTY() float z = 0;
    RM_PROPERTY() float w = 0;
};

RM_STRUCT(Label="モジュール")
struct ParticleModule {
    // A built-in module's id (BuiltinModules: "initial", "shape_cone" ...); empty: the file's.
    RM_PROPERTY() std::string builtin;
    RM_PROPERTY(Edit, Label="モジュールのファイル", Tooltip="HLSL で書いたモジュール (.rmfx)", AssetKind=".rmfx") AssetRef file;
    RM_PROPERTY(Edit, Label="使う", Tooltip="外すと、このモジュールを飛ばします") bool enabled = true;
    RM_PROPERTY() std::vector<ParticleValue> values;  // by param name; a param not here has its default
};

RM_STRUCT(Label="エミッター")
struct ParticleEmitter {
    RM_PROPERTY(Edit, Label="名前") std::string name = "エミッター";
    RM_PROPERTY(Edit, Label="使う") bool enabled = true;
    RM_PROPERTY(Edit, Label="粒の数の上限", Tooltip="同時に出ている粒の数の上限。越えると古い粒から消えます (GPU のメモリ: 1 万個で 1 MB ほど)", Min=1, Max=1000000)
    int capacity = 2000;
    RM_PROPERTY(Edit, Label="1 秒に出す数", Min=0, Max=1000000) float rate = 50;
    RM_PROPERTY(Edit, Label="始めに一度に出す数", Min=0, Max=1000000) int burst = 0;
    RM_PROPERTY(Edit, Label="1 回の長さ (秒)", Tooltip="この間だけ出します。くり返すなら、その後また始めから", Min=0.01, Max=100000) float duration = 5;
    RM_PROPERTY(Edit, Label="くり返す") bool loop = true;
    RM_PROPERTY(Edit, Label="重ね方") ParticleBlend blend = ParticleBlend::Add;
    RM_PROPERTY(Edit, Label="粒の形") ParticleShape shape = ParticleShape::SoftCircle;
    RM_PROPERTY() std::vector<ParticleModule> modules;  // run from the top (shown and edited in the パーティクル tab)
};

RM_STRUCT(Label="エフェクト")
struct ParticleEffect {
    RM_PROPERTY() std::vector<ParticleEmitter> emitters;
};

// The whole file (kind and format around the effect: what ReadEffect / WriteEffect read and write).
RM_STRUCT(Label="エフェクトのファイル")
struct EffectFile {
    RM_PROPERTY() std::string kind;
    RM_PROPERTY() int format = 1;
    RM_PROPERTY() ParticleEffect effect;
};

// What a new effect starts with: one emitter of a soft, rising glow (形: 円すい, はじめの値, 力,
// 色の変わり方, 大きさの変わり方).
ParticleEffect NewEffect();

// The file (.rmparticle): {"kind": "rm_engine.particle_effect", "format": 1, "effect": {...}}.
inline constexpr char kEffectExtension[] = ".rmparticle";
inline constexpr char kEffectKind[] = "rm_engine.particle_effect";
// nullopt (and *error, Japanese) when it is not one. Keys this version does not know go to `unknowns`
// (written back by WriteEffect: rule 6).
std::optional<ParticleEffect> ReadEffect(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns = nullptr);
std::string WriteEffect(const ParticleEffect& effect, const reflect::Unknowns* unknowns = nullptr);

}  // namespace rm::particles
