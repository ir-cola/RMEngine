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

}  // namespace rm::game_x_animation
