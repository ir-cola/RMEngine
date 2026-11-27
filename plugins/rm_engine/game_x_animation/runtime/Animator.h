#pragma once
// ゲーム制作 × アニメーション - 部品 アニメーター (docs/ANIMATION.md 段階 1): 同じエンティティの
// 「メッシュ (3D モデル)」の骨のあるモデルを、そのモデルのクリップで動かす。クリップは名前で指す
// (モデルを読み直しても、同じ名前のクリップを流す)。
//
//   Play・書き出したゲーム          始めから流す がついたものすべて (外したものは最初のコマで止まる)
//   止まっているエディター          選んだもの: シーンビューで流れる (再生 / 一時停止・時間のつまみ)
//                                    選んでいないもの: クリップの最初のコマ (クリップが無ければバインドの形)
//
// 姿勢はゲーム制作 × 3D の口 rm_engine.game_x_3d.mesh_pose に答えて渡す (AnimatorPoser.h)。
// ゲームが動いている間は、システム「アニメーション」(AnimationSystem.h) が 1 フレームに 1 回進めたもの
// (AnimatorRun.h)。スクリプトからは RMAnim.h の rm::Animator で動かす。

#include "core/reflect/Reflect.h"

#include <memory>
#include <string>

namespace rm::game_x_animation {

struct AnimatorRun;

// ルートモーション (docs/ANIMATION.md 7-1) の使い方。どれでも、使うクリップの姿勢からは根の動きを消す。
RM_ENUM(Label="ルートモーション")
enum class RootMotionUse {
    RM_ITEM(Label="動かす") Move,             // Transform へ (物理のキャラクターがあれば、その速さへ)
    RM_ITEM(Label="その場で") InPlace,        // 取り出して捨てる
    RM_ITEM(Label="スクリプトに渡す") Script, // 動かさない。スクリプトが RootDelta / RootTurn で読む
};

RM_STRUCT(Label="アニメーター")
struct Animator {
    RM_PROPERTY(Edit, Label="状態とルール", Tooltip="状態とルールのファイル (.rmanim。アセットのパネルの右クリック →「作成」→「アニメーター (状態とルール)」)。入れると、ゲームの間はルールが状態を決め、その状態のクリップ・ループ・速さで流します (下の 3 つは、そのとき今の状態のものになります)", AssetKind=".rmanim")
    AssetRef rules;
    RM_PROPERTY(Edit, Label="クリップ", Tooltip="流すクリップ (同じエンティティの「メッシュ (3D モデル)」のモデルが持つもの)。空なら、モデルの最初のクリップ")
    std::string clip;
    RM_PROPERTY(Edit, Label="ループ", Tooltip="外すと、最後まで流して止まります") bool loop = true;
    RM_PROPERTY(Edit, Label="速さ", Tooltip="再生の速さの倍率 (2 で倍の速さ。負の数で逆に流れます)", Min=-10, Max=10, Step=0.05) float speed = 1;
    RM_PROPERTY(Edit, Label="始めから流す", Tooltip="外すと、ゲームでは最初のコマで止まったままです (止まっているシーンビューでは、選ぶと流れます)")
    bool playOnStart = true;
    RM_PROPERTY(Edit, Label="ルートモーション", Tooltip="クリップの根の骨の動き (モデルのインスペクターの「クリップとイベント」で、クリップごとに「使う」を入れたもの) をどうするか。動かす: エンティティが動きます (物理のキャラクターがあれば、その速さになります。動かすクリップに重みがある間は、スクリプトの Character::Move より強い)。その場で: 動きを捨てます。スクリプトに渡す: 動かさず、スクリプトの RootDelta / RootTurn で読めます")
    RootMotionUse rootMotion = RootMotionUse::Move;
    // ゲームが動いている間の再生 (システム「アニメーション」が作る。Play するコピーだけが持つ)。
    RM_TRANSIENT() std::shared_ptr<AnimatorRun> run;
};

// 部品 骨に付ける (docs/ANIMATION.md 3-7): このエンティティを、アニメーターを持つ祖先 (親・その親 ...) の
// モデルの骨に、毎フレーム合わせる (Transform の位置と回転を書く。大きさは変えない)。剣・帽子・エフェクト・
// カメラを手や頭に。ゲームが動いている間だけ (止まっているエディターでは置いた所のまま)。
RM_STRUCT(Label="骨に付ける")
struct BoneAttach {
    RM_PROPERTY(Edit, Label="骨", Tooltip="付ける骨の名前 (アニメーターを持つ祖先の「メッシュ (3D モデル)」のモデルの骨から選びます)")
    std::string bone;
    RM_PROPERTY(Edit, Label="位置のずれ", Tooltip="骨から見た位置 (骨の向きで。単位はモデルと同じ)") Vec3 position{0, 0, 0};
    RM_PROPERTY(Edit, Label="回転のずれ (度)", Tooltip="骨から見た回転 (X → Y → Z の順)") Vec3 rotation{0, 0, 0};
    RM_PROPERTY(Edit, Label="回転も合わせる", Tooltip="外すと、位置だけが骨について行きます (向きはそのまま)") bool followRotation = true;
    RM_TRANSIENT() std::string told;  // 付けられないと知らせた理由 (同じことは 1 度だけ)
};

// --- IK (docs/ANIMATION.md 7-2-1)。骨の名前はモデルごとに違うので、.rmanim ではなく部品にする (6 章の 14) ---
RM_ENUM(Label="IK の種類")
enum class IkKind {
    RM_ITEM(Label="2 本の骨") TwoBone,  // 先の骨 (足首・手首) を目標へ: 親 (膝・肘) と親の親 (腿・二の腕) を曲げる
    RM_ITEM(Label="向ける") LookAt,     // 骨 (頭・首) の前を目標へ
};
RM_ENUM(Label="IK の目標")
enum class IkGoal {
    RM_ITEM(Label="地面") Ground,          // 足を坂に: 物理の光線で地面を探す (つなぎ アニメーション × 物理)
    RM_ITEM(Label="エンティティ") Entity,  // 目標のエンティティの位置 (+ ずれ)
    RM_ITEM(Label="スクリプト") Script,    // スクリプトの SetIkTarget / LookAt
};

RM_STRUCT(Label="IK の鎖")
struct IkChain {
    RM_PROPERTY(Edit, Label="名前", Tooltip="スクリプトが呼ぶ名前 (左足・右手・頭 …)") std::string name = "IK";
    RM_PROPERTY(Edit, Label="種類", Tooltip="2 本の骨: 先の骨を目標へ届かせ、親と親の親を曲げます。向ける: 骨の前を目標へ向けます") IkKind kind = IkKind::TwoBone;
    RM_PROPERTY(Edit, Label="骨", Tooltip="2 本の骨: 先の骨 (足首・手首)。向ける: 向ける骨 (頭・首)") std::string bone;
    RM_PROPERTY(Edit, Label="曲げる向き", Tooltip="2 本の骨が曲がる向き (モデルの空間。膝は前 (0, 0, 1)・肘は後ろ (0, 0, -1))。0: 今の姿勢の曲がり方のまま") Vec3 pole{0, 0, 1};
    RM_PROPERTY(Edit, Label="目標", Tooltip="地面: 足を坂や段に合わせます (物理の光線)。エンティティ: 下の欄のエンティティへ。スクリプト: スクリプトの SetIkTarget / LookAt") IkGoal goal = IkGoal::Ground;
    RM_PROPERTY(Edit, Label="目標のエンティティ", Tooltip="目標が「エンティティ」のとき、その位置へ (下のずれを、そのエンティティの向きで足す)") EntityRef target;
    RM_PROPERTY(Edit, Label="ずれ", Tooltip="目標からのずれ (目標のエンティティの向きで)") Vec3 offset{0, 0, 0};
    RM_PROPERTY(Edit, Label="重み", Tooltip="0: IK を使わない (クリップのまま) … 1: 目標に合わせる", Min=0, Max=1, Step=0.05) float weight = 1;
    RM_PROPERTY(Edit, Label="重みのパラメーター", Tooltip="書くと、状態とルール (.rmanim) のそのパラメーターの値 (0..1) を重みに掛けます (階段で切る・狙うときだけ)") std::string weightParam;
    RM_PROPERTY(Edit, Label="足の高さ", Tooltip="地面のとき: 足首から足の裏まで (足首がこれより地面に近づかない)", Min=0, Step=0.01) float footHeight = 0.08f;
    RM_PROPERTY(Edit, Label="回す上限 (度)", Tooltip="向ける: これより回しません。地面: 足を面に合わせて傾けるのは 30 度まで", Min=0, Max=180, Step=1) float maxAngle = 70;
};

RM_STRUCT(Label="IK")
struct AnimatorIk {
    RM_PROPERTY(Edit, Label="鎖", Tooltip="IK をかける骨の鎖 (足・手・頭 …)。同じエンティティのアニメーターの姿勢の上にかけます") std::vector<IkChain> chains;
    RM_PROPERTY(Edit, Label="腰の骨", Tooltip="地面の鎖があるとき、低い方の足が届くように下げる骨 (空: 下げない)") std::string pelvis;
    RM_PROPERTY(Edit, Label="腰を下げる上限", Tooltip="腰を下げるのはここまで (m)", Min=0, Step=0.01) float pelvisDrop = 0.3f;
};

}  // namespace rm::game_x_animation
