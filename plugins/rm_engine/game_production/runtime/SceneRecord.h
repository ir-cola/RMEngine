#pragma once
// シーンの形の 1 つの変更が何をしたか。Undo 用 - 触ったエンティティの、前と後として持つ
// (Unity の Undo.RecordObject のように)。シーン全体を 2 回持つのではない。
//
//   SceneRecord r = SceneRecord::Begin(scene, touched);   // 「これら (とその下の全部) が変わる」
//   ... 変更: Create / Delete / Move / Paste / Duplicate / AddComponent / ApplyPrefab ...
//   r.End(scene);                                         // + Begin のあとに作られた全エンティティ (新しい id)
//   r.Undo(scene);  r.Redo(scene);                        // そのエンティティだけが戻る
//
// エンティティについて持つもの: あるかないか、リストの中での場所 (= 兄弟の順番)、SaveEntity が
// 書く全部 (RM_PROPERTY のフィールド、親、部品、Prefab のリンク)。
//
// 唯一の決まり: `touched` は、その変更が変える、自分で作るもの以外のすべてのエンティティを
// 挙げなければならない。SaveEntity が書く何かが変わるか、外される / 入れられるとき、エンティティは
// 「変わった」。(ほかのものがリストの中で動くのは、前にあるエンティティが来たり去ったりする
// からだけ - それは構わない: それらの間の順番は変わらない。) Begin は touched のエンティティを、
// その下の全部ごと取る。シーンの操作が変えるものはこれで足りる (Prefab を置き直すと、その
// サブツリー全体が変わる)。
// エディターは Debug ビルドで、編集のたびにこの決まりを調べる (編集の前後のシーン全体を、
// 記録の Undo / Redo と比べる)。tests/TestSceneRecord.cpp が、何千回ものランダムな変更を走らせる。
//
// id は二度と使わない (Scene::NextId は上がるだけ) ので、「Begin のあとに作られた」は、Begin の
// ときの次の id から End のときの次の id までの id になる。
//
// 純粋 - ゲーム制作のランタイム側 (エディターなし、ファイルなし)。

#include "rm_engine/game_production/runtime/Scene.h"

#include <span>
#include <vector>

namespace rm::game_production {

class SceneRecord {
public:
    static SceneRecord Begin(const Scene& scene, std::span<const int> touched);
    void End(const Scene& scene);

    void Undo(Scene& scene) const { scene.RestoreStates(ids_, before_); }
    void Redo(Scene& scene) const { scene.RestoreStates(ids_, after_); }

    // 変更が何も変えなかったときは false (そのときは履歴に何も入れない)。
    bool Changed() const { return before_ != after_; }
    // 持っているメモリのおおよその量 (触ったエンティティの前と後をテキストで)。
    size_t Bytes() const { return bytes_; }
    size_t Entities() const { return ids_.size(); }

private:
    std::vector<int> ids_;
    std::vector<Scene::EntityState> before_, after_;
    int nextAtBegin_ = 0;
    size_t bytes_ = 0;
};

}  // namespace rm::game_production
