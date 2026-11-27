#pragma once
// アニメーション - 状態とルール (.rmanim。docs/ANIMATION.md の 4-6 / 4-7 / 4-8: 案 B「ルールの並び」)。
// 「いつ・どの状態へ・何秒で移るか」を、上から調べるルールの並びで書く。3D も 2D も知らない: 状態が指すのは
// クリップの名前だけ (3D のモデルのクリップ。後で 2D のコマ送りも)。
//
//   パラメーター  数 / 整数 / 真偽 / トリガー。スクリプトが入れる (rm::Animator::SetFloat ...)
//   状態          名前・クリップ・ループ・速さ・出口 (自由 / 自分のルールだけ / 終わるまで自分のルールだけ)
//   ルール        元 (空: どこでも)・条件 (小さな式。空: いつも)・先・フェードの秒・⚡ (出口を越えて調べる)・やり直す
//
// 中身はこの RM_STRUCT をそのまま JSON にしたファイル (憲法 1・9): 保存・読み込み・インスペクター・Undo は
// 型の表から作られる。計算は Condition.h (式) と Rules.h (ルール)。

#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::animation {

inline constexpr char kControllerExtension[] = ".rmanim";
inline constexpr char kControllerKind[] = "rm_engine.animator_rules";

RM_ENUM(Label="パラメーターの種類")
enum class ParamType {
    RM_ITEM(Label="数") Float,
    RM_ITEM(Label="整数") Int,
    RM_ITEM(Label="真偽") Bool,
    RM_ITEM(Label="トリガー") Trigger,
};

RM_STRUCT(Label="パラメーター")
struct AnimParam {
    RM_PROPERTY(Edit, Label="名前", Tooltip="ルールの条件で使う名前 (speed・grounded・jump …)。スクリプトはこの名前で値を入れます") std::string name;
    RM_PROPERTY(Edit, Label="種類", Tooltip="数: 小数 / 整数 / 真偽: 0 か 1 / トリガー: 立てると、それを使ったルールが当たったとき (か 0.2 秒たったとき) に消える")
    ParamType type = ParamType::Float;
    RM_PROPERTY(Edit, Label="初めの値", Tooltip="ゲームが始まったときの値 (真偽は 0 / 1。トリガーは使わない)") float value = 0.f;
};

RM_ENUM(Label="出口")
enum class StateExit {
    RM_ITEM(Label="自由") Free,
    RM_ITEM(Label="自分のルールだけ") Own,
    RM_ITEM(Label="終わるまで自分のルールだけ") OwnUntilEnd,
};

RM_ENUM(Label="状態の種類")
enum class StateKind {
    RM_ITEM(Label="クリップ") Clip,
    RM_ITEM(Label="1D で混ぜる") Blend1D,
    RM_ITEM(Label="2D で混ぜる") Blend2D,
};

// 混ぜる状態の 1 つのクリップと、その置き場所 (1D: x だけ / 2D: x と y)。
RM_STRUCT(Label="混ぜるクリップ")
struct BlendMotion {
    RM_PROPERTY(Edit, Label="クリップ", Tooltip="混ぜるクリップの名前 (アニメーターのモデルのクリップ)") std::string clip;
    RM_PROPERTY(Edit, Label="x", Tooltip="置き場所 (1D: パラメーターがこの値のとき、このクリップだけになる)", Min=-1000000, Max=1000000, Step=0.1) float x = 0.f;
    RM_PROPERTY(Edit, Label="y", Tooltip="置き場所の y (2D のとき)", Min=-1000000, Max=1000000, Step=0.1) float y = 0.f;
};

RM_STRUCT(Label="状態")
struct AnimState {
    RM_PROPERTY(Edit, Label="名前", Tooltip="ルールの「元」「先」で使う名前 (Idle・Move・Jump …)") std::string name;
    RM_PROPERTY(Edit, Label="クリップ", Tooltip="この状態で流すクリップの名前 (アニメーターのモデルのクリップ)", ShowIf="kind=Clip") std::string clip;
    RM_PROPERTY(Edit, Label="ループ", Tooltip="外すと 1 回流して最後のコマで止まります (条件の ended が真になる)") bool loop = true;
    RM_PROPERTY(Edit, Label="速さ", Tooltip="再生の速さの倍率", Min=-10, Max=10, Step=0.05) float speed = 1.f;
    RM_PROPERTY(Edit, Label="出口", Tooltip="自由: 全部のルールを調べる / 自分のルールだけ: 元がこの状態のルールと ⚡ のルールだけ (ジャンプの途中で歩きに戻らない) / 終わるまで自分のルールだけ: クリップが終わるまでは自分のルールだけ、終わったら全部 (攻撃・着地)")
    StateExit exit = StateExit::Free;
    // (ここから下は段階 4。前のものは並びを変えない: 1 つずつ並べて作るコードがある)
    RM_PROPERTY(Edit, Label="種類", Tooltip="クリップ: 1 つのクリップを流す / 1D で混ぜる: パラメーター 1 つの値で、数直線に置いたクリップを混ぜる (歩き ↔ 走り) / 2D で混ぜる: パラメーター 2 つの値で、平面に置いたクリップを混ぜる (8 方向の移動)")
    StateKind kind = StateKind::Clip;
    RM_PROPERTY(Edit, Label="パラメーター (x)", Tooltip="混ぜる位置を決めるパラメーター (数)", ShowIf="kind=Blend1D|Blend2D") std::string paramX;
    RM_PROPERTY(Edit, Label="パラメーター (y)", Tooltip="2D の y を決めるパラメーター (数)", ShowIf="kind=Blend2D") std::string paramY;
    RM_PROPERTY(Edit, Label="混ぜるクリップ", Tooltip="置いたクリップ。足はそろう (どれも同じ割合の時刻で読む)", ShowIf="kind=Blend1D|Blend2D")
    std::vector<BlendMotion> motions;
};

RM_STRUCT(Label="ルール")
struct AnimRule {
    RM_PROPERTY(Edit, Label="元", Tooltip="今この状態のときだけ調べる (状態の名前)。空: どこでも") std::string from;
    RM_PROPERTY(Edit, Label="条件", Tooltip="真のとき移る。C++ と同じ書き方: speed > 0.1 / jump / grounded && !attack / t > 0.5 (今の状態の中の割合 0..1) / ended (1 回のクリップが終わった・ループが 1 周した)。空: いつも")
    std::string condition;
    RM_PROPERTY(Edit, Label="先", Tooltip="移る先の状態の名前") std::string to;
    RM_PROPERTY(Edit, Label="フェード (秒)", Tooltip="今の姿勢から先の状態へ、この秒数かけて混ぜる", Min=0, Max=10, Step=0.01, Decimals=2) float fade = 0.2f;
    RM_PROPERTY(Edit, Label="割り込む (⚡)", Tooltip="今の状態の出口が「自分のルールだけ」でも調べる (死ぬ・やられる)") bool interrupt = false;
    RM_PROPERTY(Edit, Label="やり直す", Tooltip="先が今の状態と同じでも、始めから流し直す (同じ攻撃を続ける)") bool restart = false;
};

RM_ENUM(Label="重ね方")
enum class LayerMode {
    RM_ITEM(Label="上書き") Override,
    RM_ITEM(Label="足す") Additive,
};

// 足すレイヤーが差を取る基準 (段階 6。docs/ANIMATION.md 7-5)。
RM_ENUM(Label="足す基準")
enum class AdditiveBase {
    RM_ITEM(Label="休みの姿勢") Rest,
    RM_ITEM(Label="クリップの最初のコマ") FirstFrame,
};

// 重ねるレイヤー (段階 4): 自分の状態とルールを持ち、下の姿勢の上に重ねる (走りながら上半身で撃つ・呼吸を足す)。
RM_STRUCT(Label="レイヤー")
struct AnimLayer {
    RM_PROPERTY(Edit, Label="名前") std::string name;
    RM_PROPERTY(Edit, Label="重み", Tooltip="どれだけ重ねるか (0..1)。下の「重みのパラメーター」があれば、そちらの値", Min=0, Max=1, Step=0.01) float weight = 1.f;
    RM_PROPERTY(Edit, Label="重みのパラメーター", Tooltip="重みをこのパラメーターの値にする (空: 上の数)") std::string weightParam;
    RM_PROPERTY(Edit, Label="重ね方", Tooltip="上書き: このレイヤーの姿勢にする / 足す: 休みの姿勢からの差を足す (呼吸・揺れ)") LayerMode mode = LayerMode::Override;
    RM_PROPERTY(Edit, Label="足す基準", Tooltip="足すとき、何からの差を足すか。休みの姿勢: モデルの休みの姿勢から。クリップの最初のコマ: そのクリップの始めの姿勢から (表情・息のクリップを、どの姿勢にも足せる)") AdditiveBase additiveBase = AdditiveBase::Rest;
    RM_PROPERTY(Edit, Label="マスク", Tooltip="この骨と、その下の骨だけに重ねる (骨の名前。空: 全部)") std::vector<std::string> mask;
    RM_PROPERTY(Edit, Label="状態", Tooltip="このレイヤーの状態 (1 つ目から始まる)") std::vector<AnimState> states;
    RM_PROPERTY(Edit, Label="ルール", Tooltip="このレイヤーのルール (パラメーターは全部のレイヤーで同じ)") std::vector<AnimRule> rules;
};

RM_STRUCT(Label="アニメーター (状態とルール)")
struct AnimController {
    RM_PROPERTY() std::string kind = kControllerKind;  // ファイルの種類 (インスペクターには出ない)
    RM_PROPERTY() int format = 1;
    RM_PROPERTY(Edit, Label="パラメーター", Tooltip="ルールの条件で使う値。スクリプトが入れます") std::vector<AnimParam> params;
    RM_PROPERTY(Edit, Label="状態", Tooltip="1 つ目の状態から始まります") std::vector<AnimState> states;
    RM_PROPERTY(Edit, Label="ルール", Tooltip="毎フレーム上から調べて、元が合って条件が真の最初の 1 行で移ります。上ほど強い")
    std::vector<AnimRule> rules;
    RM_PROPERTY(Edit, Label="レイヤー", Tooltip="上の状態とルール (体) の上に、並びの順に重ねる") std::vector<AnimLayer> layers;
};

// ファイルの中身 (JSON) を読む。読めない・種類が違う: nullopt (`error` に理由)。`unknowns`: 知らないキー (書き戻す)。
std::optional<AnimController> ReadController(std::string_view bytes, std::string* error = nullptr, reflect::Unknowns* unknowns = nullptr);
// ファイルの中身 (JSON)。知らないキー `unknowns` を書き戻す (憲法 6)。
std::string WriteController(const AnimController& c, const reflect::Unknowns* unknowns = nullptr);

}  // namespace rm::animation
