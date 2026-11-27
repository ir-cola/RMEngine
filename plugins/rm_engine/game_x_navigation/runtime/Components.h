#pragma once
// ゲーム制作 × ナビゲーション - シーンの設定「ナビ」と部品 (データだけ。設計 docs/NAVIGATION.md 4 章)。
//
//   シーンの設定「ナビ」 (NavSettings)   エージェントの型 (体の形ごとに 1 つ) とエリアの種類。「ナビ」タブで変える
//   ナビメッシュ (NavMeshVolume)         どこを焼くかの箱・どの型で・何を焼くか・焼いた物 (.rmnav)
//   ナビの形 (NavGeometry)              焼く物を変える: 入れない / 歩けない (壁だけ) / エリアを決める
//   ナビのエリア (NavArea)              箱 / 円柱の中の床を、そのエリアにする (沼・道路・入ってはいけない所)
//   ナビの動く者 (NavAgent)             ナビメッシュの上を行き先まで歩く (段階 2。スクリプトの RMNav.h で行き先を決める)
//   つなぎの線 (NavLink)                ここから終わりへ「跳べる・降りられる・登れる」線 (段階 4。焼くときに入る)
//   ナビの障害物 (NavObstacle)          置いた箱など。止まったら、まわりの区画を作り直して道から外す (段階 4)
//
// 型とエリアは番号 (id) で指す (並べ替えても消しても、ほかは変わらない。気候の種類と同じ)。部品の欄は
// インスペクターのドロップダウン (ComponentLook::choices) で選ぶので、番号を文字で持つ ("1")。
// 体の値 (半径・高さ・坂・段差) は型だけが持つ (憲法 2: 物理の「キャラクター」と 2 か所に書かない)。

#include "core/reflect/Reflect.h"

#include <string>
#include <vector>

namespace rm::game_x_navigation {

RM_ENUM(Label="焼く細かさ")
enum class NavDetail {
    RM_ITEM(Label="荒い") Coarse,
    RM_ITEM(Label="ふつう") Normal,
    RM_ITEM(Label="細かい") Fine,
};

RM_STRUCT(Label="エージェントの型")
struct NavAgentType {
    RM_PROPERTY() int id = 1;  // 保存するが見せない: 部品「ナビメッシュ」が指すもの (使い回さない)
    RM_PROPERTY(Edit, Label="名前") std::string name = "人";
    RM_PROPERTY(Edit, Label="半径 (m)", Tooltip="体の太さ。壁からこれだけ離れて歩きます", Min=0.05, Max=50, Step=0.05, Decimals=2) float radius = 0.3f;
    RM_PROPERTY(Edit, Label="高さ (m)", Tooltip="天井がこれより低い所は通りません", Min=0.1, Max=100, Step=0.1, Decimals=2) float height = 1.8f;
    RM_PROPERTY(Edit, Label="登れる坂 (度)", Tooltip="これより急な坂は歩けない所になります", Min=0, Max=89, Step=1, Decimals=0) float maxSlope = 45.f;
    RM_PROPERTY(Edit, Label="登れる段差 (m)", Tooltip="この高さまでの段は歩いて上がれます", Min=0, Max=10, Step=0.05, Decimals=2) float stepHeight = 0.3f;
    RM_PROPERTY(Edit, Label="焼く細かさ", Tooltip="焼くときの箱の大きさ (荒い: 半径の 1/2・ふつう: 1/3・細かい: 1/5)。細かいほど狭い所も通れますが、焼くのが遅くなります")
    NavDetail detail = NavDetail::Normal;
};

RM_STRUCT(Label="エリアの種類")
struct NavAreaKind {
    RM_PROPERTY() int id = 1;  // 0: ふつう (いつもある)。1 .. 62
    RM_PROPERTY(Edit, Label="名前") std::string name;
    RM_PROPERTY(Edit, Label="色", Tooltip="ナビのタブとシーンビューで、このエリアを見せる色 (ゲームには出ません)") Color color{0.36f, 0.72f, 0.52f, 1.f};
    RM_PROPERTY(Edit, Label="通る費用", Tooltip="1: ふつう。2: 2 倍遠いものとして道を選びます (沼・草むらを避ける)", Min=1, Max=1000, Step=0.5, Decimals=1) float cost = 1.f;
    RM_PROPERTY(Edit, Label="通れない", Tooltip="道を探すときに、このエリアを通りません (焼き直さなくても効きます)") bool blocked = false;
};

std::vector<NavAgentType> DefaultAgentTypes();
std::vector<NavAreaKind> DefaultAreaKinds();

RM_STRUCT(Label="ナビ")
struct NavSettings {
    RM_PROPERTY() std::vector<NavAgentType> agentTypes = DefaultAgentTypes();
    RM_PROPERTY() std::vector<NavAreaKind> areas = DefaultAreaKinds();
    RM_PROPERTY(Edit, Label="動く者の上限", Tooltip="1 つのナビメッシュの上を同時に歩ける「ナビの動く者」の数。超えた物は歩かず、コンソールに知らせます", Min=1, Max=1000)
    int maxAgents = 128;
};

RM_STRUCT(Label="ナビメッシュ")
struct NavMeshVolume {
    RM_PROPERTY(Edit, Label="範囲の大きさ (m)", Tooltip="焼く箱の幅・高さ・奥行き。Transform の位置と大きさを掛けます (回したときは、それを囲むワールドの軸の箱)", Min=0.1, Max=100000)
    Vec3 size{20, 6, 20};
    RM_PROPERTY(Edit, Label="ずらし", Tooltip="エンティティの位置から箱の中心まで") Vec3 offset{0, 0, 0};
    RM_PROPERTY(Edit, Label="エージェントの型", Tooltip="この型の体の形で焼きます。型は「ナビ」タブで足せます") std::string agentType = "1";
    RM_PROPERTY(Edit, Label="メッシュを焼く", Tooltip="範囲の中のメッシュ (3D モデル・箱などの形) を焼く物に入れます") bool meshes = true;
    RM_PROPERTY(Edit, Label="コライダーを焼く", Tooltip="範囲の中の動かないコライダー (剛体の無い物・動き方が「動かない」の物) を焼く物に入れます") bool colliders = true;
    RM_PROPERTY(Edit, Label="焼いた物", Tooltip="「ナビ」タブの「焼く」で作るファイル (.rmnav)。シーンと一緒に保存され、書き出したゲームに入ります", AssetKind=".rmnav")
    AssetRef baked;
    RM_PROPERTY(Edit, Label="動く障害物を使う", Tooltip="「ナビの障害物」が止まったら、ゲームの中でまわりの区画を作り直して穴をあけます (焼いた物が大きくなります。変えたら焼き直す)")
    bool obstacles = false;
    RM_PROPERTY(Edit, Label="自動で焼き直す", Tooltip="形を変えて 1 秒たつと、変わった区画だけを焼き直します (「ナビ」タブを開いている間)") bool autoBake = false;
};

RM_ENUM(Label="ナビでの扱い")
enum class NavGeometryMode {
    RM_ITEM(Label="入れない") Exclude,
    RM_ITEM(Label="歩けない (壁だけ)") Wall,
    RM_ITEM(Label="エリアを決める") Area,
};

RM_STRUCT(Label="ナビの形")
struct NavGeometry {
    RM_PROPERTY(Edit, Label="扱い", Tooltip="入れない: 焼く物に入れません (飾り) / 歩けない: 当たる壁にはなるが、上は歩けません / エリアを決める: 上の床をそのエリアにします")
    NavGeometryMode mode = NavGeometryMode::Wall;
    RM_PROPERTY(Edit, Label="エリア", Tooltip="上の床にするエリアの種類", ShowIf="mode=Area") std::string area = "1";
    RM_PROPERTY(Edit, Label="中の物にも", Tooltip="このエンティティの下 (子) の物にも同じ扱いを") bool children = true;
};

RM_ENUM(Label="ナビのエリアの形")
enum class NavAreaShape {
    RM_ITEM(Label="箱") Box,
    RM_ITEM(Label="円柱") Cylinder,
};

RM_STRUCT(Label="ナビのエリア")
struct NavArea {
    RM_PROPERTY(Edit, Label="形", Tooltip="円柱は縦 (Y) が高さ") NavAreaShape shape = NavAreaShape::Box;
    RM_PROPERTY(Edit, Label="大きさ (m)", Tooltip="箱: 幅・高さ・奥行き / 円柱: X が直径、Y が高さ。Transform の大きさを掛けます", Min=0.01, Max=100000) Vec3 size{4, 2, 4};
    RM_PROPERTY(Edit, Label="エリア", Tooltip="中の床にするエリアの種類") std::string area = "1";
    RM_PROPERTY(Edit, Label="閉じている", Tooltip="扉: 中を通りません (焼き直さずにすぐ効きます。スクリプトの rm::Nav::SetAreaClosed でも開け閉めできます)") bool closed = false;
};

RM_ENUM(Label="渡り方")
enum class NavLinkMove {
    RM_ITEM(Label="歩く") Walk,
    RM_ITEM(Label="跳ぶ") Jump,
    RM_ITEM(Label="スクリプトにまかせる") Script,
};

// つなぎの線: このエンティティの位置 (始め) から終わりへ。焼くとナビメッシュに入る (Detour のオフメッシュリンク)。
// 動く者は線の始めに来ると、渡り方に従う: 歩く (まっすぐ)・跳ぶ (放物線)・スクリプトにまかせる (イベント NavLink を送って
// 待つ。スクリプトが rm::NavAgent::CompleteLink() を呼ぶと、終わりへ移って続ける)。
RM_STRUCT(Label="つなぎの線")
struct NavLink {
    RM_PROPERTY(Edit, Label="終わり (ずれ)", Tooltip="始め (このエンティティの位置) から見た終わり。Transform の向きと大きさを掛けます") Vec3 end{0, 0, 3};
    RM_PROPERTY(Edit, Label="終わりの物", Tooltip="入れると、その物の位置が終わり (上の「ずれ」は使いません)") EntityRef endEntity;
    RM_PROPERTY(Edit, Label="両方向", Tooltip="外すと、始めから終わりへだけ (飛び降りるだけの段差など)") bool bothWays = true;
    RM_PROPERTY(Edit, Label="幅 (m)", Tooltip="始めと終わりの、ナビメッシュにつながる丸の直径", Min=0.1, Max=100, Step=0.1, Decimals=2) float width = 1.f;
    RM_PROPERTY(Edit, Label="エリア", Tooltip="この線を通る費用はこのエリアのもの") std::string area = "0";
    RM_PROPERTY(Edit, Label="渡り方") NavLinkMove move = NavLinkMove::Walk;
    RM_PROPERTY(Edit, Label="跳ぶ高さ (m)", Tooltip="始めと終わりを結ぶ線から、一番高い所までの高さ", Min=0, Max=100, Step=0.1, Decimals=2, ShowIf="move=Jump") float jumpHeight = 1.f;
    RM_PROPERTY(Edit, Label="使える", Tooltip="外すと、この線を通る道を探しません (焼き直しません。スクリプトの rm::Nav::SetLinkEnabled でも)") bool enabled = true;
};

RM_ENUM(Label="障害物の形")
enum class NavObstacleShape {
    RM_ITEM(Label="円柱") Cylinder,
    RM_ITEM(Label="箱") Box,
};

// ナビの障害物: 動いている間は動く者がよける相手。止まって「止まったと見る時間」がたつと、まわりの区画を作り直して穴をあける
// (焼いたナビメッシュの「動く障害物を使う」が入っているときだけ)。動き出すと穴を閉じる。
RM_STRUCT(Label="ナビの障害物")
struct NavObstacle {
    RM_PROPERTY(Edit, Label="形") NavObstacleShape shape = NavObstacleShape::Box;
    RM_PROPERTY(Edit, Label="大きさ (m)", Tooltip="箱: 幅・高さ・奥行き / 円柱: X が直径、Y が高さ。Transform の大きさを掛けます。エンティティの位置が真ん中", Min=0.05, Max=10000)
    Vec3 size{1, 1, 1};
    RM_PROPERTY(Edit, Label="止まったら穴をあける", Tooltip="外すと、よける相手になるだけ") bool carve = true;
    RM_PROPERTY(Edit, Label="止まったと見る時間 (秒)", Min=0, Max=60, Step=0.1, Decimals=1) float stillTime = 0.5f;
};

RM_ENUM(Label="よけ方")
enum class NavAvoid {
    RM_ITEM(Label="しない") None,
    RM_ITEM(Label="ふつう") Normal,
    RM_ITEM(Label="ていねい") Careful,
};

// ナビメッシュの上を歩く物 (Transform のある物。エンティティの位置が足元)。行き先はスクリプトが決める
// (RMNav.h の rm::NavAgent(self).SetDestination)。頼んだこと (行き先・止まる) は保存しない: Play の間のナビの世界が持つ
// (NavWorld.h)。物理の「キャラクター」があればキャラクターで歩き (口 mover)、無ければ Transform を直に動かす。
RM_STRUCT(Label="ナビの動く者")
struct NavAgent {
    RM_PROPERTY(Edit, Label="エージェントの型", Tooltip="この型で焼いたナビメッシュの上を歩きます (体の半径・高さは型のもの)") std::string agentType = "1";
    RM_PROPERTY(Edit, Label="速さ (m/s)", Min=0, Max=1000, Step=0.1, Decimals=2) float speed = 3.5f;
    RM_PROPERTY(Edit, Label="加速 (m/s²)", Tooltip="速さを変える速さ。大きいほどすぐ止まり、すぐ曲がります", Min=0, Max=10000, Step=0.5, Decimals=1) float acceleration = 8.f;
    RM_PROPERTY(Edit, Label="回る速さ (度/秒)", Tooltip="進む向きへ体 (+Z) を回す速さ。0: 向きを変えない", Min=0, Max=100000, Step=10, Decimals=0) float turnSpeed = 360.f;
    RM_PROPERTY(Edit, Label="止まる距離 (m)", Tooltip="行き先までこれより近くなったら着いたことにして止まります (スクリプトに NavArrived)", Min=0, Max=1000, Step=0.05, Decimals=2)
    float stoppingDistance = 0.1f;
    RM_PROPERTY(Edit, Label="着く前にゆるめる", Tooltip="行き先の手前で、止まれるように速さを落とします。切ると全速のまま着きます") bool slowDown = true;
    RM_PROPERTY(Edit, Label="よける", Tooltip="まわりの動く者をよけます。ていねい: 試す向きが多く、混んだ所でもぶつかりにくい (少し重い)")
    NavAvoid avoid = NavAvoid::Normal;
    RM_PROPERTY(Edit, Label="よける優先度", Tooltip="0〜99。小さいほど道をゆずられ (まっすぐ進み)、大きいほどよく道をゆずります", Min=0, Max=99)
    int priority = 50;
};

}  // namespace rm::game_x_navigation
