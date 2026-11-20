#pragma once
// パーティクル - エフェクト (.rmparticle、設計 docs/ANIMATION_PARTICLES.md 3): エミッターが並び、
// それぞれがモジュールの列を順に GPU で動かす。モジュールは HLSL (.rmfx - ModuleSource.h) で、
// 組み込みのものも同じ書き方なので、動かし方は 1 つだけ (憲法 3)。
//
//   エフェクト「炎」
//    └ エミッター「火」   いくつ、どのくらいの頻度で、どう描くか
//        ├ 形: 円すい      ← モジュール: その `param` がフィールド (ParticleValue: ここで決めた値)
//        ├ はじめの値
//        └ 色の変わり方
//
// JSON で保存する (reflect: 憲法 9)。モジュールの値は param の名前で持つので、param が変わった
// モジュール (新しい版、編集した .rmfx) でも、合うものはそのまま残る。もう無い param の値も
// 書かれたまま残す (その param が戻れば戻る)。

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
    RM_ITEM(Label="星 (キラキラ)") Star,
    RM_ITEM(Label="煙 (むら)") Smoke,
};

RM_ENUM(Label="コマの進め方")
enum class ParticleSheet {
    RM_ITEM(Label="寿命に合わせて進める") OverLife,
    RM_ITEM(Label="粒ごとにランダムな 1 コマ") Random,
};

// param 1 つの値 (float は x; float3 は x y z; 色は x y z w = r g b a; bool は x != 0)。
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
    // 組み込みモジュールの ID (BuiltinModules: "initial"、"shape_cone" など); 空: ファイルのもの。
    RM_PROPERTY() std::string builtin;
    RM_PROPERTY(Edit, Label="モジュールのファイル", Tooltip="HLSL で書いたモジュール (.rmfx)", AssetKind=".rmfx") AssetRef file;
    RM_PROPERTY(Edit, Label="使う", Tooltip="外すと、このモジュールを飛ばします") bool enabled = true;
    RM_PROPERTY() std::vector<ParticleValue> values;  // param の名前ごと; ここに無い param は初期値
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
    RM_PROPERTY(Edit, Label="絵", Tooltip="粒に貼る画像 (無ければ「粒の形」)。白っぽい絵なら、色がそのまま付きます", AssetKind="image") AssetRef image;
    RM_PROPERTY(Edit, Label="絵のコマ (横の数)", Tooltip="1 枚の画像にコマが並んでいるとき (煙や炎の連番): 横に並んだ数", Min=1, Max=64) int sheetColumns = 1;
    RM_PROPERTY(Edit, Label="絵のコマ (縦の数)", Min=1, Max=64) int sheetRows = 1;
    RM_PROPERTY(Edit, Label="コマの進め方") ParticleSheet sheet = ParticleSheet::OverLife;
    RM_PROPERTY(Edit, Label="粒を描く", Tooltip="外すと、粒は描かずトレイルだけを描きます") bool dots = true;
    RM_PROPERTY(Edit, Label="速さの向きに伸ばす (秒)", Tooltip="粒を、速さ × この秒だけ動く向きに長くします (火花・雨)。0 で丸いまま", Min=0, Max=10, Step=0.005)
    float stretch = 0;
    RM_PROPERTY(Edit, Label="トレイル", Tooltip="粒が通った跡を帯で描きます (剣の軌跡・流れ星)") bool trail = false;
    RM_PROPERTY(Edit, Label="トレイルの長さ (秒)", Tooltip="どれだけ前までの跡を描くか", Min=0.01, Max=60, Step=0.01) float trailTime = 0.5f;
    RM_PROPERTY(Edit, Label="トレイルの点の数", Tooltip="多いほどなめらか (GPU のメモリ: 粒の数 × 点の数 × 16 バイト)", Min=2, Max=64) int trailPoints = 12;
    RM_PROPERTY(Edit, Label="トレイルの太さ", Tooltip="粒の大きさに掛けます。先へ行くほど細く・薄くなります", Min=0, Max=100, Step=0.05) float trailWidth = 1;
    RM_PROPERTY() std::vector<ParticleModule> modules;  // 上から順に動かす (パーティクルタブで見せて編集する)
};

RM_STRUCT(Label="エフェクト")
struct ParticleEffect {
    RM_PROPERTY() std::vector<ParticleEmitter> emitters;
};

// ファイル全体 (エフェクトの周りの種類と形式: ReadEffect / WriteEffect が読み書くもの)。
RM_STRUCT(Label="エフェクトのファイル")
struct EffectFile {
    RM_PROPERTY() std::string kind;
    RM_PROPERTY() int format = 1;
    RM_PROPERTY() ParticleEffect effect;
};

// 新しいエフェクトの初期状態: やわらかく上る光のエミッター 1 つ (形: 円すい、はじめの値、力、
// 色の変わり方、大きさの変わり方)。
ParticleEffect NewEffect();

// ファイル (.rmparticle): {"kind": "rm_engine.particle_effect", "format": 1, "effect": {...}}。
inline constexpr char kEffectExtension[] = ".rmparticle";
inline constexpr char kEffectKind[] = "rm_engine.particle_effect";
// エフェクトでなければ nullopt (と *error に日本語)。この版が知らないキーは `unknowns` に入れる
// (WriteEffect が書き戻す: 憲法 6)。
std::optional<ParticleEffect> ReadEffect(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns = nullptr);
std::string WriteEffect(const ParticleEffect& effect, const reflect::Unknowns* unknowns = nullptr);

}  // namespace rm::particles
