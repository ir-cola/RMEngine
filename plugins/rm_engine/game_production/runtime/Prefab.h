#pragma once
// Prefab: エンティティとその下の全部を 1 つのファイルとして残し、シーンに何回でも置けるもの。
// Prefab を変えると、置いた全部が追いかけて変わる - 置いた側で変えたもの (上書き = overrides)
// は除く。
//
//   Prefab ファイル (.rmprefab, JSON)
//     { "kind": "rm_engine.game_production.prefab", "format": 1,
//       "entities": [ <ルート>, <その下 ...> ] }      シーンのエンティティの形。親が先
//
//   シーンに置いた Prefab
//     すべてのエンティティが "prefab": { "source": <ファイル内での id> } を持つ。ルートはさらに
//     "asset": "<.rmprefab のアセット id>" と "overrides": {
//        "fields":            [ {"source": 1, "component": "", "index": 0, "field": "name", "value": "Cube 2"},
//                               {"source": 1, "component": "rm::space3d::Transform", "index": 0, "field": "position", "value": [...]} ],
//        "addedComponents":   [ {"source": 2, "component": {"type": ..., "data": {...}}} ],
//        "removedComponents": [ {"source": 2, "component": "rm::...", "index": 0} ],
//        "removedEntities":   [ 3 ] }
//
//   component "" = エンティティ自身のフィールド (name, 有効)。フィールドは、エンティティ / 部品の
//   最上位で 1 つずつ比べる (vector のフィールドは 1 つ分)。ルート自身のフィールドと、その
//   "placement" 部品 (Transform - ComponentInfo::placement) のフィールドは、いつも上書きに数える:
//   置いたものは、Prefab が変わっても名前と場所を保つ。
//
// Prefab のファイルはアセット id で名前を付ける (AssetRef: 動かしても名前を変えてもリンクは切れない)。
// アセットに id が無かった頃のデータは "path" (プロジェクトのフォルダから) を持つ - 同じように読む。
//
// 入れ子の Prefab: Prefab の元になるエンティティの中に置いた Prefab は、自分自身の Prefab のまま残る。
// 外側のファイルが持つのはそのルートだけで、そのリンク ("prefab": {"source", "asset", "overrides"} -
// 変えたもの。その Prefab に対して) を持つ。中身はいつも自分のファイルから来る。
//   * シーンでは、入れ子のルートは両方になる: 置いた Prefab のルートであり、外側のエンティティの
//     1 つでもある (PrefabLink::outer = 外側のファイルでの id)
//   * 外側の上書きは外側自身のエンティティを扱う (たとえば入れ子のルートの場所)。入れ子の Prefab の
//     内側の変更は、入れ子のルート自身の上書きになる
//   * 内側の Prefab を変えると、置いた全部に届く (入れ子のものも。そのファイルから作られるため)。
//     外側を変えると、外側のエンティティを作り直し、そこにあった入れ子のルートは、自分の上書きを
//     持ったまま残す (エディターが先にそれらを求め直す)
//   * エディターが入れ子のものを埋める (Scene::NestedPrefabs → 自分のファイルから ApplyPrefab)。
//     一番外側から。自分自身の中に入ってしまう Prefab は断る
//
// 純粋 (ファイルを触らない) - tests/TestPrefab.cpp でテストする。ファイルの読み書きはエディターの仕事。

#include "base/Json.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::game_production {

inline constexpr char kPrefabKind[] = "rm_engine.game_production.prefab";
inline constexpr char kPrefabExtension[] = ".rmprefab";
inline constexpr int kPrefabFormat = 1;  // このバージョンが書く "format"

bool IsPrefab(const json::Value& v);
// 新しいバージョンが書いたもの ("format" が上の kPrefabFormat より大きい): 読まず、上書きもしない。
bool NewerPrefab(const json::Value& v);

// `root` の Prefab とその下の全部。置いた Prefab のエンティティは、source をファイルでの id として
// 持つ (書き戻したあとも、ほかの置いたものと合うように)。ほかはその後ろに id を振る。`sources`
// (渡されたとき): シーンの id -> ファイル内の id (Scene::LinkPrefab 用)。
// 中に置いた Prefab は、ルートとそのリンク、今の PrefabLink::overrides として書く (呼んだ側が
// 先に求めておく)。その中身は書かない (★ その内側に置いた普通のエンティティも書かない)。
// `previous`: もとのファイル (書き直すとき) - このバージョンが知らないキーは残す (憲法 6)。
json::Value MakePrefab(const Scene& scene, int root, std::unordered_map<int, int>* sources = nullptr, const json::Value* previous = nullptr);

// `prefab` (ファイルの内容) の中に入れ子になった Prefab のアセット id。順番どおり、1 つずつ。
std::vector<std::string> NestedAssets(const json::Value& prefab);

// 置いた Prefab `root` が `prefab` (上の形) とどう違うか。`placement`: ルートのフィールドが
// いつも上書きに数えられる部品の型。
json::Value Overrides(const Scene& scene, int root, const json::Value& prefab, std::span<const std::string> placement = {});
// `overrides` が持つ変更の数 (インスペクターに出る)。
size_t CountOverrides(const json::Value& overrides);

// アセット id から見た Prefab ファイルの内容 (エディターのファイル、書き出したゲームの .rmpak)。null: 無い。
using PrefabReader = std::function<const json::Value*(const AssetRef& asset)>;
inline constexpr size_t kMaxPrefabNesting = 16;  // Prefab の中の Prefab の中の ...
// 置いた Prefab `root` の中に入れ子になった Prefab を、それぞれのファイルから (それぞれの上書き
// つきで) 埋め、さらにその中に入れ子になったものも埋める。`chain`: `root` が入っている Prefab
// (自分も含む) - 自分自身の中に入ってしまうものは埋めない (`warnings` で知らせる。`title` がその名前)。
void FillNestedPrefabs(Scene& scene, int root, std::vector<std::string>& chain, const PrefabReader& read, reflect::Warnings& warnings,
                       const std::function<std::string(const AssetRef&)>& title);
// Prefab `asset` を、中に入れ子になったものごと `parent` (0: 一番上) の下にコピーしたもの -
// ゲーム中の rm::Spawn(prefab) が作るもの。0: そんな Prefab が無い (またはエンティティが無い)。
int SpawnPrefab(Scene& scene, const AssetRef& asset, int parent, const PrefabReader& read, reflect::Warnings& warnings);

// ファイル名の語幹 (フォルダも拡張子も無し): "Assets/Enemies/Bat.rmprefab" -> "Bat"。
std::string PrefabName(std::string_view path);

}  // namespace rm::game_production
