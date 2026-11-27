#pragma once
// ナビゲーション - 動く者の群れ (中身は DetourCrowd。型はこのプラグインの .cpp の中だけ: pimpl)。設計 docs/NAVIGATION.md 5-3。
//
// 1 刻みごとに (Update): 行き先が変わった・通路がふさがれた → 道を探す (分けて探す: 1 刻みに調べる数に上限) →
// 通路の先の角へ向かう「望む速さ」(着く前にゆるめる) → 加速の上限で今の速さを変える → ナビメッシュの面の上を進める
// (壁を抜けない)。よける (AgentParams::avoid): まわりの動く者を見て、望む速さの近くの速さをいくつも試し、ぶつかりにくい物を
// 選ぶ (DetourCrowd の速さの候補を試すやり方。RVO = お互いが半分ずつよける考えに近い)。重なりは押し離す。
//
// 1 つの群れは 1 つのナビメッシュ (NavMesh) の上。NavMesh はこの群れより長く生きること。1 つのスレッドから使う。
// 純粋 (ファイルも描画もない)。tests/TestNavigation.cpp でテストする。

#include "rm_engine/navigation/runtime/Query.h"

#include <memory>
#include <vector>

namespace rm::navigation {

struct AgentParams {
    float radius = 0.3f;
    float height = 1.8f;
    float maxSpeed = 3.5f;         // m/s
    float maxAcceleration = 8.f;   // m/s²
    int avoid = 0;                 // よける: 0 しない / 1 ふつう / 2 ていねい (試す速さの候補が多い)
    float separation = 1.f;        // 離れる強さ (0 .. 4。大きいほど道をゆずる)
};

struct AgentView {
    enum class Target { None, Pending, Moving, Failed };
    Target target = Target::None;
    bool onMesh = false;     // ナビメッシュの上にいる
    bool partial = false;    // 行き先まで行けない (一番近い所へ)
    Vec3 position{0, 0, 0};
    Vec3 velocity{0, 0, 0};  // 今の速さ
    Vec3 desired{0, 0, 0};   // 望む速さ
    Vec3 goal{0, 0, 0};      // 通路の終わり (行き先か、行ける一番近い所)
};

class NavCrowd {
public:
    NavCrowd();
    ~NavCrowd();
    NavCrowd(const NavCrowd&) = delete;
    NavCrowd& operator=(const NavCrowd&) = delete;

    // `mesh` の上に、多くて `maxAgents` 人 (半径は `maxRadius` まで)。
    bool Init(NavMesh& mesh, int maxAgents, float maxRadius);
    // 費用 (エリアの番号ごと)。通れないエリアは NavMesh::SetBlockedAreas。
    void SetFilter(const NavFilter& filter);

    // 足す (`position` の近くのナビメッシュの上に置く)。-1: いっぱい・ナビメッシュの外。
    int Add(Vec3 position, const AgentParams& params);
    void Remove(int agent);
    void SetParams(int agent, const AgentParams& params);
    // 行き先 (ナビメッシュの上の一番近い点へ)。false: 近くにナビメッシュが無い。
    bool SetTarget(int agent, Vec3 target);
    void ClearTarget(int agent);  // 止まる (その場で)
    // ほかの誰か (物理のキャラクター・ルートモーション) が動かした所へ、通路を保ったまま移す。
    void MoveTo(int agent, Vec3 position);

    void Update(float dt);

    AgentView View(int agent) const;
    // 今の所から通路の終わりまでの角の点 (始めは今の所)。
    void Path(int agent, std::vector<Vec3>& out) const;
    // 通路に沿った残りの距離 (行き先が無い: 0)。
    float Remaining(int agent) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace rm::navigation
