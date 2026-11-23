#pragma once
// 世界のセーブ: 部品「セーブする」(Saveable) が付いたエンティティの位置・有効・部品の値を取り
// (CaptureWorld)、読み直したシーンに当てる (ApplyWorld)。ファイル (save_<slot>.json の "world") は
// Globals.h が書き、スクリプトの rm::Save::WriteWorld / ReadWorld (Scripts.cpp) が使う。
// 設計: docs/SAVE.md。
//
// ゲーム制作は 3D / 2D を知らない: 「位置」は置き場所の部品 (ComponentInfo::placement - 3D の
// Transform と Transform 2D) の値。部品の値はシーンのファイルと同じ形 ({"type", "data"}。リフレクション
// のまま) で書くので、型ごとのセーブのコードは無い (憲法 1・9)。
//
// ゲームの中で作った物は id で見分ける: シーンを読んだときの次の id (`base`) より大きい id のもの
// (Scene は id を使い回さない)。Prefab から作った物は、読むときに同じ Prefab を置き直す。
//
// 純粋 (ファイルを触らない) - tests/TestWorldSave.cpp でテストする。

#include "base/Json.h"
#include "core/RuntimeRegistry.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rm::game_production {

// 世界のセーブに入れる物の印。付けた物だけが残る (読んだ後、付いていてセーブに無い物は「ゲームの中で
// 消した物」として消える)。
RM_STRUCT(Label="セーブする")
struct Saveable {
    RM_PROPERTY(Edit, Label="位置・回転・大きさ", Tooltip="置き場所の部品 (Transform / Transform 2D) の値と親をセーブします")
    bool placement = true;
    RM_PROPERTY(Edit, Label="有効", Tooltip="有効かどうかをセーブします")
    bool active = true;
    RM_PROPERTY(Edit, Label="全部の部品の値", Tooltip="スクリプトも含めた全部の部品の値と、名前・タグ・レイヤーをセーブします。ゲームの中で足した部品も足し直します")
    bool everything = false;
};

inline constexpr int64_t kWorldFormat = 1;

// セーブしたシーン: 名前 (ゲームのシーンの一覧の名前) とアセットの id ("": 分からない)。
struct WorldScene {
    std::string name;
    std::string id;
};

// 置き場所の部品の型 (ComponentInfo::placement を持つもの) の名前。
std::vector<std::string> PlacementTypes(const RuntimeRegistry& registry);

// `scene` の、「セーブする」が付いた物の状態 (セーブの "world" の中身: docs/SAVE.md 3 章)。
//   base: そのシーンを読んだときの Scene::NextId (これ以上の id はゲームの中で作った物。0: 分からない)
//   placement: 置き場所の部品の型
//   leaving: このフレームの終わりに消える物 (と、その下の全部): 入れない
// (前のセーブの "world" の知らないキーは、ファイルに書くとき WriteSave が残す。)
json::Value CaptureWorld(const Scene& scene, int base, std::span<const std::string> placement, const WorldScene& sceneName,
                         std::span<const int> leaving = {});

// セーブしたシーンの名前と id。
WorldScene SavedScene(const json::Value& world);
// ゲームのシーンの一覧 (名前と id。同じ順) の中のセーブしたシーンの場所: id が合うもの、無ければ
// 名前が合うもの。nullopt: 一覧に無い。
std::optional<size_t> FindSavedScene(const json::Value& world, std::span<const std::string> names, std::span<const std::string> ids);

struct WorldApplied {
    int restored = 0;  // 値を当てた物
    int made = 0;      // 作り直した物 (Prefab の中の物も 1 つずつ)
    int removed = 0;   // 消した物 (ゲームの中で消した、「セーブする」の付いた物と、その下の物)
};
// 読んだばかりのシーン `scene` (部品の型は決めてある) に `world` (CaptureWorld が書いたもの) を当てる:
// ゲームの中で作った物を作り直し (Prefab は `prefab` から)、値を当て、ゲームの中で消した物を消す。
// 無い物・使えない部品・形の違う値は飛ばして `warnings` に書く (落ちない)。
WorldApplied ApplyWorld(Scene& scene, const json::Value& world, const PrefabReader& prefab, reflect::Warnings& warnings);

}  // namespace rm::game_production
