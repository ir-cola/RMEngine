#pragma once
// ゲーム制作 × ナビゲーション - シーンからナビの物を読む (純粋: ファイルも描画もない。tests/TestNavigation.cpp)。
//
//   Volumes        部品「ナビメッシュ」のある物と、そのワールドの範囲 (箱を Transform で置き、囲むワールドの軸の箱)
//   CollectInput   範囲の焼く物: 口 source の形 → ナビの形・エリアの決まり → 焼く入力 (NavInput)
//   Fingerprint    形の指紋: 焼いたときと違えば「古い」
//
// 焼く物にしない物: 切ってある物 (自分か親)・動く物 (口 source の moves。自分か親)・ナビの形が「入れない」。

#include "rm_engine/game_x_navigation/runtime/Components.h"
#include "rm_engine/game_x_navigation/runtime/NavPorts.h"
#include "rm_engine/navigation/runtime/Query.h"

#include <bitset>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace rm::game_x_navigation {

Worlds WorldsOf(const game_production::Scene& scene);

// 型。`id` が無い (消した) ときは最初の型 (型が 1 つも無いときは null)。
const NavAgentType* FindAgentType(const NavSettings& s, std::string_view id);
const NavAreaKind* FindAreaKind(const NavSettings& s, int id);
// 焼くときの体の形 (細かさから箱の大きさ)。
navigation::NavAgent AgentOf(const NavAgentType& t);
// エリアの種類の番号 (文字: "0"・"3") -> 焼く番号 (NavTypes.h: 0 = ふつう -> 63、1 .. 62 はそのまま)。読めない: ふつう。
uint8_t AreaCodeOf(std::string_view areaId);
int AreaIdOfCode(uint8_t code);  // 逆 (ふつう -> 0)
// 道を探すときの費用と、通れないエリア (番号ごと)。
navigation::NavFilter FilterOf(const NavSettings& s);
std::bitset<64> BlockedOf(const NavSettings& s);
// 新しい型・エリアの番号 (今のどれとも違う。エリアは 1 .. 62、無ければ 0)。
int NewAgentTypeId(const NavSettings& s);
int NewAreaKindId(const NavSettings& s);
// エリアの種類の、決まった順の色 (足したときの最初の色)。
Color AreaKindColor(size_t n);

struct NavVolume {
    int entity = 0;
    std::string name;
    bool active = true;  // 自分も親も有効
    NavMeshVolume volume;
    navigation::NavBounds bounds;  // ワールド
};
std::vector<NavVolume> Volumes(const game_production::Scene& scene, const Worlds& worlds);
std::optional<NavVolume> FindVolume(const game_production::Scene& scene, const Worlds& worlds, int entity);

// 焼く物を集める。`sources` は口 source に差し出されたもの (ボリュームの欄で切ってある物は飛ばす)。
// `stop` が立ったら false。
bool CollectInput(const game_production::Scene& scene, const Worlds& worlds, const NavVolume& volume, std::span<const NavSourceInfo* const> sources,
                  const game_production::AssetAccess* assets, std::stop_token stop, navigation::NavInput& out);

// 形の指紋 (体の形・範囲・焼く物の部品とその位置・ナビの形とエリアの部品)。
uint64_t Fingerprint(const game_production::Scene& scene, const Worlds& worlds, const NavVolume& volume, const NavAgentType& type,
                     std::span<const NavSourceInfo* const> sources, const game_production::AssetAccess* assets);

// 形 (モデル自身の点と三角形) をワールドへ置いて `out` に足す (口 source が使う)。鏡写しの行列 (負の大きさ) は
// 表が裏返るので、三角形の向きも戻す。
void AddMeshTo(NavShape& out, std::span<const Vec3> local, std::span<const uint32_t> indices, const space3d::Mat4& world);

// 指紋に部品を足す (口 source の fingerprint が使う): エンティティの番号・ワールド行列・部品の値 (保存する形の JSON)。
void HashComponent(navigation::Hasher& h, const game_production::SceneEntity& e, const game_production::Component& c, const Worlds& worlds);

}  // namespace rm::game_x_navigation
