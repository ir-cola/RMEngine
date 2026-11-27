#pragma once
// ゲーム制作 × ナビゲーション - Play の間のナビの世界 (設計 docs/NAVIGATION.md 5-3・9 章)。Play しているシーンが持つ
// (Scene::PlayState "rm_engine.game_x_navigation.world": Play のコピーと一緒に捨てる。物理の世界と同じ)。
//
//   システム「ナビ」(固定の刻み・order -10: スクリプトの Tick の後、物理の前):
//     1 最初だけ: 部品「ナビメッシュ」の焼いた物 (.rmnav) を読む (エディター: ファイル / 書き出したゲーム: .rmpak の中)。
//       ナビメッシュごとに群れ (NavCrowd) を 1 つ
//     2 エリアの費用と通れないエリア (シーンの設定 + スクリプトが変えた物)
//     3 部品「ナビの動く者」: 今の位置を Transform から読む (スクリプトや物理が動かした分: 動いていたら置き直す)・
//       頼まれた行き先・速さ
//     4 群れを進める (道を探す: 分けて探す・角へ向かう・面の上を進む・よける) → 動かす: 口 mover の持ち主がいれば
//       その道で (ルートモーション: 位置も向きも書かない / キャラクター: 速さを渡し、向きだけ回す)。いなければ Transform に書く
//     5 着いた (残りが「止まる距離」より短い) → 止まって、スクリプトへイベント NavArrived
//
// 動かす人は 1 人だけ (口 mover の order の小さい持ち主。いなければナビが Transform を)。ほかの誰かが動かす物は、毎刻み
// その位置へ群れの者を移す (通路は保つ)。純粋 (描画もファイルの書き込みもない)。tests/TestNavigation.cpp でテストする。

#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_navigation/runtime/Components.h"
#include "rm_engine/game_x_navigation/runtime/NavPorts.h"
#include "rm_engine/navigation/runtime/Crowd.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::game_x_navigation {

inline constexpr char kNavArrivedEvent[] = "NavArrived";

class NavWorld {
public:
    struct Mesh {
        int volume = 0;  // 部品「ナビメッシュ」のエンティティ
        std::string name;
        int agentType = 0;
        navigation::NavBounds bounds;
        std::unique_ptr<navigation::NavMesh> mesh;
        std::unique_ptr<navigation::NavCrowd> crowd;
        int agents = 0;
    };
    struct Agent {
        int mesh = -1, index = -1;  // どのナビメッシュの群れの何番 (-1: まだ / 外)
        bool hasTarget = false, stopped = false, arrived = false, pending = false;
        Vec3 target{0, 0, 0};
        float speed = -1;           // スクリプトが変えた速さ (-1: 部品のもの)
        Vec3 written{0, 0, 0};      // 前の刻みに Transform に書いた位置 (ワールド)
        navigation::AgentView view;
        float remaining = 0;
        bool warnedOff = false;
        const NavMoverInfo* mover = nullptr;  // 動かす道 (null: ナビが Transform を)
        const NavMoverInfo* noted = nullptr;  // 知らせた道
        float yaw = 0;                               // 体の向き (ワールドの Y の角度。度。+Z が 0)
    };

    // システム「ナビ」の 1 刻み。
    void Step(game_production::SystemContext& ctx);
    // 焼いた物を読む (Step が最初に呼ぶ。テストは直に)。
    void Load(game_production::Scene& scene, const game_production::AssetAccess* assets);
    bool Loaded() const { return loaded_; }

    // --- スクリプトから (RMNav.h)。次の刻みで効く ---
    void SetDestination(int entity, Vec3 target);
    void Stop(int entity);
    void Resume(int entity);
    void SetSpeed(int entity, float speed);
    // その場所へ (道を捨てて、ナビメッシュの上の一番近い点)。false: 近くにナビメッシュが無い。
    bool Warp(game_production::Scene& scene, int entity, Vec3 position);
    const Agent* FindAgent(int entity) const;
    // 名前で指すエリア (無い名前: false。警告は 1 回)。
    bool SetAreaCost(const game_production::Scene& scene, std::string_view name, float cost);
    bool SetAreaOpen(const game_production::Scene& scene, std::string_view name, bool open);

    // `at` を含むナビメッシュ (型 `agentType`。0: どれでも)。無ければ最初のもの (読んでいない: null)。
    Mesh* MeshAt(Vec3 at, int agentType = 0);
    const std::vector<Mesh>& Meshes() const { return meshes_; }
    const std::unordered_map<int, Agent>& Agents() const { return agents_; }
    // 今の費用 (シーンの設定とスクリプトの変更)。
    navigation::NavFilter Filter(const NavSettings& s) const;
    std::bitset<64> Blocked(const NavSettings& s) const;

private:
    void Apply(game_production::Scene& scene);
    void Place(Agent& a, int entity, Vec3 at, const navigation::AgentParams& params, const NavAgent& c, std::string_view name);

    bool loaded_ = false;
    std::vector<Mesh> meshes_;
    std::unordered_map<int, Agent> agents_;
    std::map<int, float> costs_;  // エリアの id -> 費用 (スクリプトが変えた物)
    std::map<int, bool> open_;    // エリアの id -> 通れる (スクリプトが変えた物)
    std::vector<std::string> warnedNames_;
};

// 再生中のシーンのナビの世界 (最初に呼んだときに作る)。
NavWorld& NavWorldOf(game_production::Scene& scene);
// あればそれ (null: 無い。何も作らない)。
NavWorld* FindNavWorld(game_production::Scene& scene);

// システム「ナビ」(SystemPhase::Fixed)。
void RunNavigation(game_production::SystemContext& ctx);

// 向きを `toward` (水平の向き) へ、多くて `maxDegrees` だけ回した Y の角度 (度。+Z が 0、+X が 90)。
float TurnToward(float yawDegrees, Vec3 toward, float maxDegrees);

}  // namespace rm::game_x_navigation
