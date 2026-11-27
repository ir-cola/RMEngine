#pragma once
// ナビゲーション - 焼いたナビメッシュ (NavData) の上で道を探す。中身は Detour (型はこのプラグインの .cpp の中だけ: pimpl)。
//
//   FindPath      一番近い多角形を探し → A* (多角形から隣へ、費用の小さい順に広げる) → 通る多角形の並び (通路) →
//                 ファネル (通路の中で角だけを曲がるようにひもを張る) → 角の点の並び
//   ClosestPoint  ナビメッシュの上の一番近い点
//   Raycast       a から b へ、ナビメッシュの上をまっすぐ歩けるか
//   RandomPoint   歩ける所のでたらめな点
//
// エリアの費用 (NavFilter) は探すたびに渡す。「通れない」エリアは SetBlockedAreas、閉じた扉 (エリアの形) は
// SetClosedVolumes、切ったつなぎの線は SetLinkEnabled (どれも多角形の印を変える: 焼き直さない)。
// 動く障害物を使うナビメッシュ (NavData::obstacles) は DetourTileCache で層から作り、AddObstacle の所だけ区画を作り直す
// (UpdateObstacles: 1 回に作り直す区画の数に上限)。作り直した区画にも印をかけ直す。1 つの NavMesh は 1 つのスレッドから使う。
// 純粋 (ファイルも描画もない)。tests/TestNavigation.cpp でテストする。

#include "rm_engine/navigation/runtime/NavTypes.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rm::navigation {

// エリアの番号 (NavTypes.h) ごとの費用 (1 = ふつう。2 = 2 倍遠く感じる)。
struct NavFilter {
    std::array<float, 64> costs;
    NavFilter() { costs.fill(1.f); }
};

struct NavPath {
    enum class Status {
        Found,      // 行き先まで
        Partial,    // 行き先まで行けない: 一番近い所まで
        StartOff,   // 始めがナビメッシュの外
        EndOff,     // 終わりがナビメッシュの外
        NoMesh,     // ナビメッシュが無い (焼いていない・歩ける所が無い)
    };
    Status status = Status::NoMesh;
    std::vector<Vec3> points;  // 角の点 (始め、曲がる所、終わり)
    float length = 0;          // 点をつないだ長さ (m)
    bool Ok() const { return status == Status::Found || status == Status::Partial; }
};

// 多角形の縁 1 本 (見せる用: 縁 = 隣が無い (壁)、内 = 隣の多角形との境目)。
struct NavEdge {
    Vec3 a, b;
    uint8_t area = kAreaNormal;
    bool border = false;
};

// 多角形 1 つ (見せる用: 真上から塗る)。
struct NavPolygon {
    std::vector<Vec3> points;
    uint8_t area = kAreaNormal;
};

class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // `data` の区画を Detour に載せる。前に載せたものは捨てる。歩ける所が無くても true (Empty)。
    bool Load(const NavData& data, std::string* error);
    bool Empty() const;
    size_t Tiles() const;
    size_t Polygons() const;

    // 道。`searchHalf`: 始めと終わりから多角形を探す箱の半分の大きさ (高さは体の高さくらい)。
    NavPath FindPath(Vec3 start, Vec3 end, const NavFilter& filter = {}, Vec3 searchHalf = {2, 4, 2}) const;
    std::optional<Vec3> ClosestPoint(Vec3 p, Vec3 searchHalf = {2, 4, 2}, const NavFilter& filter = {}) const;
    // まっすぐ歩けるとき true。当たったときは `hit` に当たった所の割合 (0 .. 1)。
    bool Raycast(Vec3 a, Vec3 b, const NavFilter& filter = {}, float* hit = nullptr) const;
    // `seed` が同じなら同じ点 (テスト用。ゲームは時刻などで変える)。
    std::optional<Vec3> RandomPoint(Vec3 center, float radius, uint32_t seed, const NavFilter& filter = {}) const;

    // 通れないエリア (番号ごと)。多角形の印を変える (すぐ効く)。
    void SetBlockedAreas(const std::bitset<64>& blocked);
    // 閉じた扉: この柱の中に真ん中がある多角形を通らない。
    void SetClosedVolumes(std::vector<NavAreaVolume> closed);
    // つなぎの線 (NavLinkInput::id) を通る / 通らない。
    void SetLinkEnabled(uint32_t id, bool enabled);
    const std::vector<NavLinkInput>& Links() const;

    // --- 動く障害物 (NavData::obstacles のときだけ) ---
    struct Obstacle {
        bool box = false;
        Vec3 center{0, 0, 0};  // 円柱: 底の真ん中 / 箱: 真ん中
        float radius = 0.5f, height = 2.f;
        Vec3 half{0.5f, 1.f, 0.5f};
        float yawRadians = 0;
    };
    bool HasObstacles() const;
    // 足す (体の半径の分だけ広げる)。0: 足せない (障害物を使わない・いっぱい)。区画は UpdateObstacles で作り直す。
    uint32_t AddObstacle(const Obstacle& o);
    void RemoveObstacle(uint32_t id);
    // 頼まれた区画を、多くて `maxTiles` 作り直す。作り直したら true。
    bool UpdateObstacles(float dt, int maxTiles);

    void Edges(std::vector<NavEdge>& out) const;
    void PolygonsOf(std::vector<NavPolygon>& out) const;

    // Detour の dtNavMesh (このプラグインの Crowd.cpp だけが使う。載せていない: null)。
    void* Native();

    struct Impl;

private:
    bool LoadLayers(const NavData& data, std::string* error);
    std::unique_ptr<Impl> impl_;
};

}  // namespace rm::navigation
