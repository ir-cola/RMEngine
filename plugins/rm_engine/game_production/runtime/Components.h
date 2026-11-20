#pragma once
// ゲームのエンティティの部品 (ECS の C)。核はこの言葉を知らない: 部品とは何かはゲーム制作が決め、
// 他のプラグイン - コネクター - が、ゲーム制作がそれを知らないまま部品の型を出す (設計
// RME_PLATFORM 4-f: ゲーム制作は 3D 空間を include しない。"ゲーム制作 × 3D" が 3D 空間の
// Transform を出す)。
//
//   // コネクターのランタイム側 (runtime/Register.cpp - core/RuntimeRegistry.h) で:
//   static const game_production::ComponentInfo kTransform{&reflect::TypeOf<space3d::Transform>, true};
//   registry.Contribute(game_production::kComponentPoint, kTransform);
//
// 部品の型は、リフレクトされた構造体ならなんでもよい。ファイルでは C++ の名前 (TypeDesc::name) で
// 呼ぶ。今出ていない型 (そのプラグインがない) の部品は、読んだままで保持し、変えずに書き戻す。

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Object.h"
#include "core/RuntimeRegistry.h"

#include <span>
#include <string>
#include <vector>

namespace rm::game_production {

// PluginRegistry の口: 中身は ComponentInfo (static)。
inline constexpr char kComponentPoint[] = "rm_engine.game_production.component";

// 部品の型がゲームにとって何か (ランタイム側: 書き出したゲームも同じ一覧を読む)。
// エディターでの見せ方 (階層の絵、ローカル / ワールド) はエディター側のもの: ComponentLook
// (game_production/editor/ComponentLook.h)。同じプラグインの RegisterEditor が出す。
struct ComponentInfo {
    const reflect::TypeDesc& (*type)();  // 例: &reflect::TypeOf<space3d::Transform>
    bool onNewEntities = false;          // 新しいエンティティすべてが 1 つ持つ
    // 1 つのエンティティが複数持ってよい (インスペクターでそれぞれ別のブロックになり、順に動く)。
    bool multiple = false;
    // 置かれた Prefab の場所 (Transform): 置いた Prefab のルートでは、Prefab が変わっても、値は
    // いつも置いたもの自身のままになる (Prefab.h)。
    bool placement = false;
    // 属する空間 (SceneViewInfo::space、例 "3d"): `onNewEntities` は「ゲームの空間がこれである間の
    // 新しいエンティティすべて」という意味になる (2D のゲームの新しいエンティティには、3D の
    // Transform でなく Transform 2D がつく)。null: どの空間でも。
    const char* space = nullptr;
    // これを持つエンティティを通して、ゲームは見られる (3D の Camera、Camera 2D): 切り替えた先の
    // シーンは、この中からカメラを選ぶ (SceneCamera)。
    bool camera = false;
    // インスペクターの 部品を足す でこれが並ぶフォルダ ("3D"、"2D"、"UI"、"物理" ... UTF-8、
    // 表示するときに翻訳する。フォルダの中のフォルダは "/" で区切る)。null: "その他"。スクリプトは
    // "スクリプト" に入る。
    const char* folder = nullptr;
};

// PluginRegistry の口: 中身は SceneSettingInfo (static)。
inline constexpr char kSceneSettingPoint[] = "rm_engine.game_production.scene_setting";

// シーン自身が持つ値で、エンティティのものではない (物理の 重力、3D の 空): 出ている型ごとに
// すべてのシーンが 1 つずつ持ち (変えるまでは既定値)、シーンのファイルに保存される。インスペクターは、
// シーン (階層の一番上の行) を選んだときにこれを出す。システムと描画側は Scene::Setting<T>() で読む。
struct SceneSettingInfo {
    const reflect::TypeDesc& (*type)();
    // それが対象のゲームの空間 (SceneViewInfo::space、例 "3d": 空): インスペクターは、その空間の
    // ゲームでだけ出す (どちらでも保持され、保存される)。null: どの空間でも。
    const char* space = nullptr;
};

struct Component {
    std::string type;          // TypeDesc::name、例 "rm::space3d::Transform"
    reflect::Object value;     // 今その型が出ていないときは空
    reflect::Unknowns unknowns;  // "data" の中にある、新しいバージョンのキー
    json::Value raw;           // 読んだままの部品 (知らないキー - 型が出ていないときは全部)

    bool Known() const { return !value.Empty(); }
};

// シーンや Prefab のファイルに書かれている部品の型 (Scene::Save の形: "entities" と、それぞれの
// "components" [{"type": ...}]、置いた Prefab に足した部品)。それぞれ 1 回、見つけた順に。
void ComponentTypesIn(const json::Value& sceneOrPrefab, std::vector<std::string>& out);

// 部品の型 `used` を出しているプラグイン (プロジェクトが使うものの記録: 設計 RME_PLATFORM 5-a、
// 条件 1 - 書き出したゲームはそれらをリンクする)。それぞれ 1 回、`offered` の順に。今は誰も出して
// いない型 (そのプラグインがない) は、何も返さない。
std::vector<PluginId> PluginsOffering(const std::vector<std::string>& used, const std::vector<Contributed<ComponentInfo>>& offered);

}  // namespace rm::game_production
