#pragma once
// ナビゲーション - 焼く入力と焼いた物の型。3D もゲーム制作も知らない (設計 docs/NAVIGATION.md 3 章: 純粋)。
//
//   NavInput   焼く三角形 (ワールドの点。表は「エンジンの決まり」= 外から見て反時計回り: 3D 空間の Mesh.h と同じ) と、
//              三角形ごとのエリアの番号・エリアの形 (その中の床をそのエリアにする)
//   NavAgent   体の形 (半径・高さ・登れる坂・段差) と細かさ (箱の大きさ)
//   NavData    焼いた物: 区画 (タイル) ごとの Detour のデータ。.rmnav (NavFile.h) に書く
//
// エリアの番号 (NavInput::areas・Detour の多角形のエリア):
//   0          歩けない (壁や天井として当たるだけ。Recast の RC_NULL_AREA)
//   1 .. 62    エリアの種類 (沼・道路 ...)
//   63         ふつう (Recast の RC_WALKABLE_AREA)
// 費用と「通れない」は道を探すとき (Query.h の NavFilter) に決めるので、変えても焼き直さなくてよい。

#include "base/Math.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rm::navigation {

inline constexpr uint8_t kAreaNone = 0;     // 歩けない
inline constexpr uint8_t kAreaNormal = 63;  // ふつう
inline constexpr int kMaxAreaKinds = 62;    // 1 .. 62

// ワールドの軸に沿った箱。
struct NavBounds {
    Vec3 min{0, 0, 0}, max{0, 0, 0};
    bool Valid() const { return max.x > min.x && max.y > min.y && max.z > min.z; }
};

// 体の形 (焼くときの決まり)。長さは m、坂は度。
struct NavAgent {
    float radius = 0.3f;      // 壁からこれだけ離れる
    float height = 1.8f;      // 天井がこれより低い所は通らない
    float maxSlope = 45.f;    // 登れる坂
    float maxClimb = 0.3f;    // 登れる段差
    float cellSize = 0.1f;    // 箱の横の大きさ (小さいほど細かく、焼くのが遅い)
    float cellHeight = 0.05f; // 箱の縦の大きさ
    bool operator==(const NavAgent&) const = default;
};

// エリアの形: 縦の柱 (真上から見た凸の多角形 x, z と、下と上の高さ)。その中の歩ける床がそのエリアになる。
struct NavAreaVolume {
    std::vector<Vec2> outline;  // (x, z)。凸
    float minY = 0, maxY = 0;
    uint8_t area = kAreaNormal;
};

struct NavInput {
    std::vector<Vec3> points;
    std::vector<uint32_t> indices;  // 三角形ごとに 3 つ。表は外から見て反時計回り (エンジンの決まり)
    std::vector<uint8_t> areas;     // 三角形ごと (上の番号)
    std::vector<NavAreaVolume> volumes;

    size_t Triangles() const { return indices.size() / 3; }
    void AddTriangle(Vec3 a, Vec3 b, Vec3 c, uint8_t area) {
        const uint32_t base = static_cast<uint32_t>(points.size());
        points.push_back(a), points.push_back(b), points.push_back(c);
        indices.push_back(base), indices.push_back(base + 1), indices.push_back(base + 2);
        areas.push_back(area);
    }
};

// 焼いた区画 1 つ (Detour の dtCreateNavMeshData が作るバイト列そのまま)。
struct NavTile {
    int x = 0, z = 0;
    std::string data;
    bool operator==(const NavTile&) const = default;
};

struct NavData {
    NavBounds bounds;        // 焼いた範囲
    NavAgent agent;          // 焼いたときの体の形
    float tileSize = 0;      // 区画の一辺 (m)
    int tilesX = 0, tilesZ = 0;
    int maxPolysPerTile = 0; // 一番多い区画の多角形の数 (読むときに Detour に伝える)
    std::vector<NavTile> tiles;  // 歩ける所のある区画だけ。z、x の順

    bool Empty() const { return tiles.empty(); }
};

// 64 ビットの FNV-1a (形の指紋: 焼き直しが要るか)。同じバイトなら、どの PC でも同じ値。
class Hasher {
public:
    void Bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            h_ ^= p[i];
            h_ *= 1099511628211ull;
        }
    }
    void Text(std::string_view s) {
        const uint64_t n = s.size();
        Bytes(&n, sizeof n);
        Bytes(s.data(), s.size());
    }
    void Float(float f) {
        if (f == 0) f = 0;  // (-0 と +0 を同じに)
        Bytes(&f, sizeof f);
    }
    void Int(int64_t v) { Bytes(&v, sizeof v); }
    uint64_t Value() const { return h_; }

private:
    uint64_t h_ = 14695981039346656037ull;
};

}  // namespace rm::navigation
